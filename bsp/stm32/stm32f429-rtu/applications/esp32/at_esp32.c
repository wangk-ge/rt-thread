/****************************************************************************
 *
 * File Name
 *  at_esp32.c
 * Author
 *  
 * Date
 *  2020/05/01
 * Descriptions:
 * ESP32模块AT通信接口实现
 *
 ******************************************************************************/
/*----------------------------------------------------------------------------*
**                             Dependencies                                   *
**----------------------------------------------------------------------------*/
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <rtthread.h>
#include <at.h>
#include <drv_gpio.h>

#include "util.h"
#include "strref.h"
#include "common.h"
#include "cmd.h"
#include "app.h"
#include "at_esp32.h"

/**---------------------------------------------------------------------------*
 **                            Debugging Flag                                 *
 **---------------------------------------------------------------------------*/
#define LOG_TAG              "at_esp32"
#define LOG_LVL              LOG_LVL_DBG
#include <rtdbg.h>

/**---------------------------------------------------------------------------*
**                             Compiler Flag                                  *
**----------------------------------------------------------------------------*/
#ifdef __cplusplus
extern   "C"
{
#endif

/*----------------------------------------------------------------------------*
**                             Mcaro Definitions                              *
**----------------------------------------------------------------------------*/
#define AT_ESP32_CLIENT_BUF_SIZE 	(128)
#define AT_ESP32_POWER_PIN 			GET_PIN(E, 3) // esp32电源开关控制引脚
#define AT_ESP32_CONNECT_TIME 		(5000) // 等待连接esp32超时时间(tick)
#define AT_ESP32_MSG_QUEUE_LEN		(8)
#define AT_ESP32_THREAD_PRIORITY 	(RT_MAIN_THREAD_PRIORITY + 1) // ESP32数据处理线程优先级(优先级低于主线程)
#define AT_ESP32_THREAD_STACK_SIZE  (2048)

/*----------------------------------------------------------------------------*
**                             Data Structures                                *
**----------------------------------------------------------------------------*/
/* 消息类型定义 */
typedef enum
{
    AT_ESP32_MSG_NONE = 0,
    AT_ESP32_MSG_DATA_RECV, // 收到数据
    AT_ESP32_MSG_CONN, // 已连接
	AT_ESP32_MSG_DISCONN, // 已断开
} at_esp32_msg_type;

/* 消息类型定义 */
typedef struct
{
    at_esp32_msg_type msg; // 消息类型
    uint8_t msg_data[31]; // 消息数据
	uint8_t data_len; // 消息数据长度
} at_esp32_message;

/*----------------------------------------------------------------------------*
**                             Local Vars                                     *
**----------------------------------------------------------------------------*/
static at_client_t esp32_at_client = RT_NULL;

static rt_thread_t esp32_at_thread = RT_NULL;
static rt_mq_t esp32_at_mq = RT_NULL;

static uint32_t esp32_mtu = 23; // 最大传输单元(字节)

static void esp32_at_urc_write_func(struct at_client *client, const char *data, rt_size_t size);
static void esp32_at_urc_bleconn_func(struct at_client *client, const char *data, rt_size_t size);
static void esp32_at_urc_bledisconn_func(struct at_client *client, const char *data, rt_size_t size);
static void esp32_at_urc_blecfgmtu_func(struct at_client *client, const char *data, rt_size_t size);

static const struct at_urc esp32_at_urc_table[] =
{
    {"+WRITE:", "\r\n", esp32_at_urc_write_func},
	{"+BLECONN:", "\r\n", esp32_at_urc_bleconn_func},
	{"+BLEDISCONN:", "\r\n", esp32_at_urc_bledisconn_func},
    {"+BLECFGMTU:", "\r\n", esp32_at_urc_blecfgmtu_func},
};

static char esp32_ble_addr[32] = "";

/*----------------------------------------------------------------------------*
**                             Local Function                                 *
**----------------------------------------------------------------------------*/
/* 发送消息到处理线程 */
static rt_err_t at_esp32_send_msg(at_esp32_message *esp32_msg)
{
    rt_err_t ret = rt_mq_send(esp32_at_mq, esp32_msg, sizeof(at_esp32_message));
    if (ret != RT_EOK)
    {
        LOG_E("%s call rt_mq_send error(%d)", __FUNCTION__, ret);
    }
    return ret;
}

/* +WRITE:<conn_index>,<srv_index>,<char_index>,[<desc_index>],<len>,<value> URC处理 */
static void esp32_at_urc_write_func(struct at_client *client, const char *data, rt_size_t size)
{
	LOG_D("%s() data=%.*s", __FUNCTION__, (int)size, data);
    
    uint8_t *data_ptr = NULL; // 数据
    uint32_t data_len = 0; // 数据总长度(字节数)
    uint32_t sent_len = 0; // 已发送长度(字节数)

    /* 拆分参数列表并检查参数个数 */
    c_str_ref param_list[7] = {{0}};
    uint32_t param_count = 0;
    c_str_ref str_ref = { size - STR_LEN("+WRITE:\r\n"), data + STR_LEN("+WRITE:") };
    param_count = strref_split(&str_ref, ',', param_list, ARRAY_SIZE(param_list));
    if (param_count != 6)
    {
        LOG_E("%s invalid urc!", __FUNCTION__);
        return;
    }
    
    /* 数据长度 */
    data_len = strref_to_u32(&(param_list[4]));
    if (data_len != param_list[5].len)
	{
        LOG_E("%s invalid data length(%d)!", __FUNCTION__, data_len);
        return;
    }
    
    /* 数据 */
    data_ptr = (uint8_t*)(param_list[5].c_str);
    
    /* 数据发送到处理线程进行处理 */
    while (sent_len < data_len)
    {
        at_esp32_message esp32_msg = {
            .msg = AT_ESP32_MSG_DATA_RECV,
        };
        rt_err_t ret = RT_EOK;
        uint8_t pkt_len = MIN(data_len - sent_len, sizeof(esp32_msg.msg_data));
        
        memcpy(esp32_msg.msg_data, data_ptr + sent_len, pkt_len);
        esp32_msg.data_len = pkt_len;
        
        ret = at_esp32_send_msg(&esp32_msg);
        if (ret != RT_EOK)
        {
            LOG_E("%s at_esp32_send_msg(AT_ESP32_MSG_DATA_RECV) failed(%d)!", __FUNCTION__, ret);
            break;
        }
        sent_len += pkt_len;
    }
}

/* +BLECONN=<conn_index>,<remote_address>[,<addr_type>] */
static void esp32_at_urc_bleconn_func(struct at_client *client, const char *data, rt_size_t size)
{
	LOG_D("%s() data=%.*s", __FUNCTION__, (int)size, data);
	
	/* 消息发送到处理线程进行处理 */
	at_esp32_message esp32_msg = {
		.msg = AT_ESP32_MSG_CONN,
	};
	rt_err_t ret = at_esp32_send_msg(&esp32_msg);
	if (ret != RT_EOK)
	{
		LOG_E("%s at_esp32_send_msg(AT_ESP32_MSG_CONN) failed(%d)!", __FUNCTION__, ret);
	}
}

/* +BLEDISCONN=<conn_index> */
static void esp32_at_urc_bledisconn_func(struct at_client *client, const char *data, rt_size_t size)
{
	LOG_D("%s() data=%.*s", __FUNCTION__, (int)size, data);
	
	/* 消息发送到处理线程进行处理 */
	at_esp32_message esp32_msg = {
		.msg = AT_ESP32_MSG_DISCONN,
	};
	rt_err_t ret = at_esp32_send_msg(&esp32_msg);
	if (ret != RT_EOK)
	{
		LOG_E("%s at_esp32_send_msg(AT_ESP32_MSG_DISCONN) failed(%d)!", __FUNCTION__, ret);
	}
}

/* +BLECFGMTU:<conn_index>,<mtu_size> */
static void esp32_at_urc_blecfgmtu_func(struct at_client *client, const char *data, rt_size_t size)
{
    LOG_D("%s() data=%.*s", __FUNCTION__, (int)size, data);
    
    uint32_t mtu = 0;
    
    /* 拆分参数列表并检查参数个数 */
    c_str_ref param_list[3] = {{0}};
    uint32_t param_count = 0;
    c_str_ref str_ref = { size - STR_LEN("+BLECFGMTU:\r\n"), data + STR_LEN("+BLECFGMTU:") };
    param_count = strref_split(&str_ref, ',', param_list, ARRAY_SIZE(param_list));
    if (param_count != 2)
    {
        LOG_E("%s invalid urc!", __FUNCTION__);
        return;
    }
    
    /* MTU大小 */
    mtu = strref_to_u32(&(param_list[1]));
    if (mtu < 23)
	{
        LOG_E("%s invalid mtu(%d)!", __FUNCTION__, mtu);
        return;
    }
    
    esp32_mtu = (uint32_t)mtu;
}

/*************************************************
* Function: esp32_ble_send
* Description: 通过ESP32 BLE发送数据
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
static void esp32_ble_send(const uint8_t *data, uint32_t data_len)
{
    LOG_D("%s() data_len=%d", __FUNCTION__, data_len);
    
    size_t cur_pkt_size = 0, sent_size = 0;
	at_response_t resp = app_alloc_at_resp(1, rt_tick_from_millisecond(5000));
    RT_ASSERT(resp != RT_NULL)

	/* set AT client end sign to deal with '>' sign.*/
    at_obj_set_end_sign(esp32_at_client, '>');

    while (sent_size < data_len)
    {
        /* 确保每包数据长度不会超过MTU大小(除去ATT的opcode一个字节以及ATT的handle 2个字节 */
        if ((data_len - sent_size) < (esp32_mtu - 3))
        {
            cur_pkt_size = (data_len - sent_size);
        }
        else
        {
            cur_pkt_size = (esp32_mtu - 3);
        }

        /* send the "AT+BLEGATTSNTFY" commands to AT server than receive the '>' response on the first line. */
        if (at_obj_exec_cmd(esp32_at_client, resp, "AT+BLEGATTSNTFY=0,1,6,%d", (int)cur_pkt_size) < 0)
        {
			LOG_E("%s at_obj_exec_cmd(AT+BLEGATTSNTFY=0,1,6,%d) failed!", __FUNCTION__, (int)cur_pkt_size);
            break;
        }
        
        rt_thread_mdelay(5);
        
        /* send the real data */
        if (at_client_obj_send(esp32_at_client, (const char*)data + sent_size, cur_pkt_size) <= 0)
        {
			LOG_E("%s at_client_obj_send(%d) failed!", __FUNCTION__, (int)cur_pkt_size);
            break;
        }
        
        rt_thread_mdelay(5);

        sent_size += cur_pkt_size;
    }
	
	app_free_at_resp(resp);
}

/*************************************************
* Function: cmd_resp
* Description: 发送CMD响应
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
static void cmd_resp(const uint8_t *data, uint32_t data_len)
{
    LOG_D("%s() %.*s", __FUNCTION__, data_len, (const char*)data);
    
    esp32_ble_send(data, data_len);
}

/* 数据处理线程 */
static void at_esp32_thread_entry(void *param)
{
    LOG_D("%s()", __FUNCTION__);
	
	/* 主消息循环 */
    while (1)
    {
        at_esp32_message esp32_msg = {AT_ESP32_MSG_NONE};
        rt_err_t ret = rt_mq_recv(esp32_at_mq, &esp32_msg, sizeof(esp32_msg), RT_WAITING_FOREVER);
        if (RT_EOK != ret)
        { // 出现严重错误
            LOG_E("%s recv at esp32 msg failed(%d)!", __FUNCTION__, ret);
            break;
        }
        
        switch (esp32_msg.msg)
        {
            case AT_ESP32_MSG_DATA_RECV: // 收到数据
			{
				/* 数据输入到CMD模块进行解析 */
				cmd_input(esp32_msg.msg_data, (uint32_t)esp32_msg.data_len);
				break;
			}
			case AT_ESP32_MSG_CONN: // 已连接
			{
				/* 查询MTU大小 */
                
                at_response_t resp = app_alloc_at_resp(0, rt_tick_from_millisecond(1000));
				RT_ASSERT(resp != RT_NULL)
				/* 启动BLE广播 */
				ret = at_obj_exec_cmd(esp32_at_client, resp, "AT+BLECFGMTU?");
				if (ret != RT_EOK)
				{
					LOG_E("%s at_obj_exec_cmd(AT+BLECFGMTU?) failed(%d)!", __FUNCTION__, ret);
				}
                else
                {
                    int mtu = 0;
                    at_resp_parse_line_args_by_kw(resp, "+BLECFGMTU:", "+BLECFGMTU:%*d,%d", &mtu);
                    if (mtu >= 23)
                    {
                        esp32_mtu = (uint32_t)mtu;
                    }
                }
				app_free_at_resp(resp);
				break;
			}
			case AT_ESP32_MSG_DISCONN: // 已断开
			{
				at_response_t resp = app_alloc_at_resp(0, rt_tick_from_millisecond(1000));
				RT_ASSERT(resp != RT_NULL)
				/* 启动BLE广播 */
				ret = at_obj_exec_cmd(esp32_at_client, resp, "AT+BLEADVSTART");
				if (ret != RT_EOK)
				{
					LOG_E("%s at_obj_exec_cmd(AT+BLEADVSTART) failed(%d)!", __FUNCTION__, ret);
				}
				app_free_at_resp(resp);
				break;
			}
            default:
            {
                LOG_W("%s recv unknown msg(%u)!", __FUNCTION__, esp32_msg.msg);
                break;
            }
        }
    }
    
    esp32_at_thread = RT_NULL;
}

/*----------------------------------------------------------------------------*
**                             Public Function                                *
**----------------------------------------------------------------------------*/
/*************************************************
* Function: at_esp32_init
* Description: 模块初始化
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
rt_err_t at_esp32_init(void)
{
	LOG_D("%s()", __FUNCTION__);
	
	rt_err_t ret = RT_EOK;
	at_response_t resp = RT_NULL;
	
	/* initialize AT client */
    ret = at_client_init(AT_ESP32_UART_DEVICE_NAME, AT_ESP32_CLIENT_BUF_SIZE);
    if (ret != RT_EOK)
    {
        LOG_E("%s at_client_init(%s) failed(%d)!", __FUNCTION__, AT_ESP32_UART_DEVICE_NAME, ret);
        //ret = -RT_ERROR;
		goto __exit;
    }
	
	esp32_at_client = at_client_get(AT_ESP32_UART_DEVICE_NAME);
	if (esp32_at_client == RT_NULL)
    {
        LOG_E("%s at_client_get(%s) failed.", __FUNCTION__, AT_ESP32_UART_DEVICE_NAME);
        ret = -RT_ERROR;
		goto __exit;
    }
	
	/* 初始化CMD模块 */
	cmd_init(cmd_resp);
	
#if 1
	/* 开启ESP32模块电源重新上电 */
	//rt_pin_write(AT_ESP32_POWER_PIN, PIN_HIGH); // 拉高关闭电源
	//rt_pin_mode(AT_ESP32_POWER_PIN, PIN_MODE_OUTPUT_OD);
    //rt_thread_mdelay(1000);
    rt_pin_write(AT_ESP32_POWER_PIN, PIN_LOW); // 拉低开启电源
#else
    /* 开启ESP32模块电源(拉低开启) */
	rt_pin_write(AT_ESP32_POWER_PIN, PIN_LOW);
	rt_pin_mode(AT_ESP32_POWER_PIN, PIN_MODE_OUTPUT_OD);
#endif
    
	/* wait esp32 startup finish, send AT every 500ms, if receive OK, SYNC success*/
	ret = at_client_obj_wait_connect(esp32_at_client, AT_ESP32_CONNECT_TIME);
	if (ret != RT_EOK)
	{
		LOG_E("%s at_client_obj_wait_connect(%s) failed.", __FUNCTION__, AT_ESP32_UART_DEVICE_NAME);
		//ret = -RT_ETIMEOUT;
		goto __exit;
	}
	
	resp = app_alloc_at_resp(0, rt_tick_from_millisecond(1000));
    RT_ASSERT(resp != RT_NULL)
    
    /* 软复位 */
	ret = at_obj_exec_cmd(esp32_at_client, resp, "AT+RST");
	if (ret != RT_EOK)
	{
		LOG_E("%s at_obj_exec_cmd(AT+RST) failed(%d)!", __FUNCTION__, ret);
		//ret = -RT_ERROR;
		goto __exit;
	}
    
    /* wait esp32 startup finish, send AT every 500ms, if receive OK, SYNC success*/
	ret = at_client_obj_wait_connect(esp32_at_client, AT_ESP32_CONNECT_TIME);
	if (ret != RT_EOK)
	{
		LOG_E("%s at_client_obj_wait_connect(%s) failed.", __FUNCTION__, AT_ESP32_UART_DEVICE_NAME);
		//ret = -RT_ETIMEOUT;
		goto __exit;
	}
    
    /* 关闭回显 */
	ret = at_obj_exec_cmd(esp32_at_client, resp, "ATE0");
	if (ret != RT_EOK)
	{
		LOG_E("%s at_obj_exec_cmd(ATE0) failed(%d)!", __FUNCTION__, ret);
		//ret = -RT_ERROR;
		goto __exit;
	}
	
	/* 配置ESP32工作于BLE服务器模式 */
	ret = at_obj_exec_cmd(esp32_at_client, resp, "AT+BLEINIT=2");
	if (ret != RT_EOK)
	{
		LOG_E("%s at_obj_exec_cmd(AT+BLEINIT=2) failed(%d)!", __FUNCTION__, ret);
		//ret = -RT_ERROR;
		goto __exit;
	}

	/* 创建BLE服务 */
	ret = at_obj_exec_cmd(esp32_at_client, resp, "AT+BLEGATTSSRVCRE");
	if (ret != RT_EOK)
	{
		LOG_E("%s at_obj_exec_cmd(AT+BLEGATTSSRVCRE) failed(%d)!", __FUNCTION__, ret);
		//ret = -RT_ERROR;
		//goto __exit; // 服务已创建?
	}
	
	/* 启动BLE服务 */
	ret = at_obj_exec_cmd(esp32_at_client, resp, "AT+BLEGATTSSRVSTART");
	if (ret != RT_EOK)
	{
		LOG_E("%s at_obj_exec_cmd(AT+BLEGATTSSRVSTART) failed(%d)!", __FUNCTION__, ret);
		//ret = -RT_ERROR;
		goto __exit;
	}
	
	/* 查询BLE服务和特性 */
	ret = at_obj_exec_cmd(esp32_at_client, resp, "AT+BLEGATTSCHAR?");
	if (ret != RT_EOK)
	{
		LOG_E("%s at_obj_exec_cmd(AT+BLEGATTSCHAR?) failed(%d)!", __FUNCTION__, ret);
		//ret = -RT_ERROR;
		goto __exit;
	}
	
	/* 查询BLE地址 */
	ret = at_obj_exec_cmd(esp32_at_client, resp, "AT+BLEADDR?");
	if (ret != RT_EOK)
	{
		LOG_E("%s at_obj_exec_cmd(AT+BLEADDR?) failed(%d)!", __FUNCTION__, ret);
		//ret = -RT_ERROR;
		goto __exit;
	}
    at_resp_parse_line_args_by_kw(resp, "+BLEADDR:", "+BLEADDR:%s", esp32_ble_addr);
    
	/* 创建消息队列 */
    esp32_at_mq = rt_mq_create("at_esp32", sizeof(at_esp32_message), AT_ESP32_MSG_QUEUE_LEN, RT_IPC_FLAG_FIFO);
    if (RT_NULL == esp32_at_mq)
    {
        LOG_E("%s rt_mq_create(at_esp32) failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
	
	/* 启动BLE广播 */
	ret = at_obj_exec_cmd(esp32_at_client, resp, "AT+BLEADVSTART");
	if (ret != RT_EOK)
	{
		LOG_E("%s at_obj_exec_cmd(AT+BLEADVSTART) failed(%d)!", __FUNCTION__, ret);
		//ret = -RT_ERROR;
		goto __exit;
	}
	
	/* 创建处理线程 */
    esp32_at_thread = rt_thread_create("at_esp32", at_esp32_thread_entry, 
        (void*)RT_NULL, AT_ESP32_THREAD_STACK_SIZE, AT_ESP32_THREAD_PRIORITY, 10);
    if (esp32_at_thread == RT_NULL)
    {
        LOG_E("%s rt_thread_create(at_esp32) failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
	
	/* 启动ESP32处理线程 */
	ret = rt_thread_startup(esp32_at_thread);
	if (ret != RT_EOK)
	{
		LOG_E("%s at_obj_exec_cmd(AT+BLEADVSTART) failed(%d)!", __FUNCTION__, ret);
		esp32_at_thread = RT_NULL;
		//ret = -RT_ERROR;
		goto __exit;
	}
    
    /* register URC data execution function  */
    ret = at_obj_set_urc_table(esp32_at_client, esp32_at_urc_table, ARRAY_SIZE(esp32_at_urc_table));
    if (ret != RT_EOK)
    {
        LOG_E("%s at_obj_set_urc_table failed(%d)!", __FUNCTION__, ret);
		//ret = -RT_ERROR;
        goto __exit;
    }
	
__exit:
	if (resp)
	{
		app_free_at_resp(resp);
	}
	
	if (ret != RT_EOK)
	{
		if (esp32_at_mq)
		{
			rt_mq_delete(esp32_at_mq);
			esp32_at_mq = RT_NULL;
		}
	}
	
	return ret;
}

/*************************************************
* Function: at_esp32_ble_send
* Description: 通过ESP32 BLE发送字符串数据
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
void at_esp32_ble_send(const char *str, int len)
{
    uint8_t *data = (uint8_t*)str;
    uint32_t data_len = (uint32_t)len;
    if (len < 0)
    {
        data_len = strlen(str);
    }
    
    LOG_D("%s() %.*s", __FUNCTION__, data_len, (const char*)data);
    
    esp32_ble_send(data, data_len);
}

/*************************************************
* Function: get_esp32_ble_addr
* Description: 取得ESP32模块BLE地址
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
const char* get_esp32_ble_addr(void)
{
    return esp32_ble_addr;
}

/**---------------------------------------------------------------------------*
 **                         Compiler Flag                                     *
 **---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif
// End of at_esp32.c
