/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-11-06     SummerGift   first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <fal.h>
#include <easyflash.h>
#include <at.h>
#include <arpa/inet.h>
#include <netdev.h>
#include <modbus-rtu.h>
#include <mqtt_client.h>
#include <errno.h>
#include <string.h>
#include <jsmn.h>
#include <at_device.h>
#include "config.h"
#include "common.h"
#include "util.h"
#include "strref.h"

#define LOG_TAG              "main"
#define LOG_LVL              LOG_LVL_DBG
#include <rtdbg.h>

#define TEST_NEW_FETURE // 测试新特性

typedef enum
{
    APP_MSG_NONE = 0,
    APP_MSG_MQTT_CLIENT_START_REQ, // MQTT客户端启动请求
    APP_MSG_DATA_REPORT_REQ, // 数据主动上报请求
    APP_MSG_DATA_ACQUISITION_REQ, // 数据采集请求
    APP_MSG_MQTT_PUBLISH_REQ, // MQTT发布数据请求
    APP_MSG_MQTT_CLIENT_ONLINE, // MQTT客户端器上线
    APP_MSG_MQTT_CLIENT_OFFLINE, // MQTT客户端器下线
    APP_MSG_HTTP_OTA_THREAD_QUIT, // HTTP OTA线程已退出
} app_msg_type;

/* RS485设备名 */
#define RS485_1_DEVICE_NAME "/dev/uart2"
#define RS485_2_DEVICE_NAME "/dev/uart8"
#define RS485_3_DEVICE_NAME "/dev/uart6"
//#define RS485_4_DEVICE_NAME "/dev/uart7"
#define RS485_4_DEVICE_NAME "/dev/uart5"

/* RS485接收使能GPIO引脚 */
#define RS485_1_REN_PIN GET_PIN(E, 10) // PE10(高电平发送,低电平接收)
#define RS485_2_REN_PIN GET_PIN(E, 11) // PE11(高电平发送,低电平接收)
#define RS485_3_REN_PIN GET_PIN(E, 12) // PE12(高电平发送,低电平接收)
#define RS485_4_REN_PIN GET_PIN(E, 13) // PE13(高电平发送,低电平接收)

/* 历史数据保存的最大条数 */
#define HISTORY_DATA_MAX_NUM (4096)

/* 上报数据用的JSON数据缓冲区 */
#define JSON_DATA_BUF_LEN (1024)

/* MQTT订阅主题名缓冲区长度 */
#define MQTT_TOPIC_BUF_LEN (128)

/* 主循环消息队列最大长度 */
#define APP_MSG_QUEUE_LEN (8)

/* MQTT客户端线程栈大小 */
#define MQTT_CLIENT_THREAD_STACK_SIZE (8192)

/* MQTT客户端线程优先级(优先级必须高于主线程) */
#define MQTT_CLIENT_THREAD_PRIORITY (RT_MAIN_THREAD_PRIORITY - 1)

/* HTTP OTA线程优先级(优先级低于主线程) */
#define HTTP_OTA_THREAD_PRIORITY (RT_MAIN_THREAD_PRIORITY + 1)

/* HTTP OTA线程栈大小 */
#define HTTP_OTA_THREAD_STACK_SIZE (4096)

/* 历史数据FIFO队列信息(头部插入、尾部删除) */
typedef struct
{
    uint32_t length; // 队列长度
    uint32_t head_pos; // 头部位置
    uint32_t tail_pos; // 尾部位置
} history_fifo_info;

/* 主消息循环消息类型定义 */
typedef struct
{
    app_msg_type msg; // 消息类型
    void* msg_data; // 消息数据
} app_message;

/* MQTT发布数据信息 */
typedef struct
{
    enum QoS qos;
    char *topic; // 主题
    char *payload; // 数据
    size_t length; // 数据长度
} mqtt_publish_data_info;

/* http OTA升级请求信息 */
typedef struct
{
    char url[256];
    uint8_t md5[16];
    char req_id[32];
    char version[16];
    int firmware_size;
} http_ota_info;

/* RS485接口信息 */
typedef struct
{
    const char *dev_name; // 设备名
	rt_base_t rts_pin; //RTS引脚
	int serial_mode;
} rs485_device_info;

static rs485_device_info rs485_dev_infos[CFG_UART_X_NUM] = 
{
	{RS485_1_DEVICE_NAME, RS485_1_REN_PIN, MODBUS_RTU_RS485},
	{RS485_2_DEVICE_NAME, RS485_2_REN_PIN, MODBUS_RTU_RS485},
	{RS485_3_DEVICE_NAME, RS485_3_REN_PIN, MODBUS_RTU_RS485},
	//{RS485_4_DEVICE_NAME, RS485_4_REN_PIN, MODBUS_RTU_RS485},
	{RS485_4_DEVICE_NAME, RS485_4_REN_PIN, MODBUS_RTU_RS232},
};

/* 用于保护历史数据FIFO队列信息的并发访问 */
static rt_mutex_t history_fifo_mutex = RT_NULL;

/* 主消息循环队列 */
static rt_mq_t app_msg_queue = RT_NULL;

/* 定时上报定时器 */
static rt_timer_t report_timer = RT_NULL;
/* 定时采集定时器 */
static rt_timer_t acquisition_timer = RT_NULL;

/* HTTP OTA线程 */
static rt_thread_t http_ota_thread = RT_NULL;
/* HTTP OTA互斥锁(用于保护http_ota_thread变量) */
static rt_mutex_t http_ota_mutex = RT_NULL;

/* mqtt client */
static mqtt_client mq_client;
static char mqtt_client_id[16] = "";
static char mqtt_server_url[64] = "";

/* MQTT订阅主题名(必须分配具有长生命周期的缓冲区) */
char topic_telemetry_get[MQTT_TOPIC_BUF_LEN] = "";
char topic_config_get[MQTT_TOPIC_BUF_LEN] = "";
char topic_config_set[MQTT_TOPIC_BUF_LEN] = "";
char topic_telemetry_result[MQTT_TOPIC_BUF_LEN] = "";
char topic_upgrade_update[MQTT_TOPIC_BUF_LEN] = "";

/* 下载固件并执行MD5校验,返回值: 0=下载并校验成功, -1=下载失败, -2=校验失败 */
extern int http_ota_fw_download(const char* uri, int size, unsigned char* orign_md5);

/* 重启系统进行升级 */
extern void http_ota_reboot();

/* 发送消息到主消息循环 */
static rt_err_t app_send_msg(app_msg_type msg, void* msg_data)
{
    app_message app_msg = {msg, msg_data};
    rt_err_t ret = rt_mq_send(app_msg_queue, &app_msg, sizeof(app_msg));
    if (ret != RT_EOK)
    {
        LOG_E("%s call rt_mq_send error(%d)", __FUNCTION__, ret);
    }
    return ret;
}

/* 请求(主线程)执行MQTT数据发布 */
static rt_err_t send_mqtt_publish_req(enum QoS qos, char *topic, char *payload, size_t length)
{
    mqtt_publish_data_info *app_msg_data = 
        (mqtt_publish_data_info*)rt_malloc(sizeof(mqtt_publish_data_info));
    if (app_msg_data == RT_NULL)
    {
        LOG_E("%s(%u) failed!", __FUNCTION__, sizeof(mqtt_publish_data_info));
        return -RT_ENOMEM;
    }
    
    app_msg_data->qos = qos;
    app_msg_data->topic = topic;
    app_msg_data->payload = payload;
    app_msg_data->length = length;
    rt_err_t ret = app_send_msg(APP_MSG_MQTT_PUBLISH_REQ, app_msg_data);
    if (ret != RT_EOK)
    {
        rt_free(app_msg_data);
        
        LOG_E("%s(APP_MSG_MQTT_PUBLISH_REQ) error(%d)!", __FUNCTION__, ret);
    }
    
    return ret;
}

/* 取得模组信号强度指示 */
int get_modem_rssi(int *rssi)
{
    struct at_device *device = at_device_get_by_name(AT_DEVICE_NAMETYPE_DEVICE, TB22_DEVICE_NAME);
    return at_device_control(device, AT_DEVICE_CTRL_GET_SIGNAL, rssi);
}

/* 取得客户端唯一编号 */
static uint32_t get_clientid(void)
{
    config_info *cfg = cfg_get();
    return cfg->client_id;
}

/* 取得产品编号 */
static const char* get_productkey(void)
{
    config_info *cfg = cfg_get();
    if (cfg->productkey == NULL)
    {
        return "productKey";
    }
    return cfg->productkey;
}

/* 取得设备ID */
static const char* get_devicecode(void)
{
    config_info *cfg = cfg_get();
    if (cfg->devicecode == NULL)
    {
        return "deviceCode";
    }
    return cfg->devicecode;
}

/* 取得标签ID */
static const char* get_itemid(void)
{
    config_info *cfg = cfg_get();
    if (cfg->itemid == NULL)
    {
        return "";
    }
    return cfg->itemid;
}

/* 采集UARTX数据 */
static uint32_t uart_x_data_acquisition(int x, uint8_t *data_buf, uint32_t buf_len)
{
    LOG_D("%s(UART%d)", __FUNCTION__, x);
    
    /* 内部函数不做参数检查,由调用者保证参数有效性 */
    int index = x - 1;
    
    uint32_t data_len = 0; // 采集的数据长度(字节数)
    
    /* 校验位转换表(0=无'NONE',1=奇'EVEN',2=偶'ODD') */
    const char parity_name[] = {'N', 'E', 'O'};
    
    modbus_t *mb_ctx = NULL;

    /* 配置信息 */
    config_info *cfg = cfg_get();
    
    /* 采集数据 */
    {
        /* 创建MODBUS RTU对象实例 */
        mb_ctx = modbus_new_rtu(rs485_dev_infos[index].dev_name, 
            cfg->uart_x_cfg[index].baudrate, 
            parity_name[cfg->uart_x_cfg[index].parity], 
            cfg->uart_x_cfg[index].wordlength, 
            cfg->uart_x_cfg[index].stopbits);
        if (mb_ctx == NULL)
        {
            LOG_E("%s call modbus_new_rtu error!", __FUNCTION__);
            goto __exit;
        }
        
        /* 配置MODBUS RTU属性 */
        modbus_rtu_set_serial_mode(mb_ctx, rs485_dev_infos[index].serial_mode);
		if (rs485_dev_infos[index].serial_mode == MODBUS_RTU_RS485)
		{
			/* function shall set the Request To Send mode to communicate on a RS485 serial bus. */
			modbus_rtu_set_rts(mb_ctx, rs485_dev_infos[index].rts_pin, MODBUS_RTU_RTS_UP);
		}
        modbus_set_slave(mb_ctx, cfg->uart_x_cfg[index].slaveraddr); // 从机地址
        modbus_set_response_timeout(mb_ctx, 1, 0); // 超时时间:1S
        
        /* 连接MODBUS端口 */
        int iret = modbus_connect(mb_ctx);
        if (iret != 0)
        {
            LOG_E("%s modbus_connect error(%d)!", __FUNCTION__, errno);
            goto __exit;
        }
        
        int i = 0;
        uint8_t variablecnt = cfg->uart_x_cfg[index].variablecnt; // 变量个数
        for (i = 0; i < variablecnt; ++i)
        {
            uint16_t startaddr = cfg->uart_x_cfg[index].startaddr[i]; // 寄存器地址
            uint16_t length = cfg->uart_x_cfg[index].length[i]; // 寄存器个数
            /* Reads the holding registers of remote device and put the data into an array */
            uint16_t read_buf[MODBUS_MAX_READ_REGISTERS] = {0}; // 读取数据缓冲区
            memset(read_buf, 0, sizeof(read_buf)); // 先清零
            int read_bytes = modbus_read_registers(mb_ctx, startaddr, length, read_buf); // 读取寄存器数据
            if (read_bytes != length)
			{
				LOG_W("%s modbus_read_registers(0x%04x,%u) return(%u)!", __FUNCTION__, startaddr, length, read_bytes);
			}
			
            /* 保存到采集数据缓存 */
            int j = 0;
            for (j = 0; j < length; ++j)
            {
                uint16_t data = read_buf[j];
                RT_ASSERT(data_len < buf_len);
                data_buf[data_len++] = (uint8_t)(data >> 8); // 高字节
                RT_ASSERT(data_len < buf_len);
                data_buf[data_len++] = (uint8_t)(data & 0x00FF); // 低字节
            }
        }
    }
    
__exit:
    /* 关闭MODBUS端口 */
    modbus_close(mb_ctx);
    
    /* 释放MODBUS对象实例 */
    modbus_free(mb_ctx);
    
    return data_len;
}

/* 采集数据并保存到Flash */
static rt_err_t data_acquisition_and_save(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    rt_err_t ret = RT_EOK;
    uint8_t data_buf[256] = {0};
    uint32_t data_len = 0;
    char data_key[16] = "";
    
    /* 确保互斥修改FIFO队列 */
    rt_mutex_take(history_fifo_mutex, RT_WAITING_FOREVER);
    
    /* 读取历史数据队列信息 */
    history_fifo_info fifo_info = {
        .length = 0, // 队列长度
        .head_pos = 0, // 头部位置
        .tail_pos = 0 // 尾部位置
    };
    size_t len = ef_get_env_blob("history_fifo_info", &fifo_info, sizeof(fifo_info), NULL);
    if (len != sizeof(fifo_info))
    {
        /* 加载FIFO队列失败,提示将会新建一个(第一次运行?) */
        LOG_W("%s ef_get_env_blob(history_fifo_info) load fail, create new!", __FUNCTION__);
    }
    
    /* 本次采集将保存在FIFO中的位置 */
    uint32_t pos = fifo_info.head_pos;
    
    /* 记录当前时间 */
    time_t now = time(RT_NULL);
    
    int x = 1;
    for (x = 1; x <= CFG_UART_X_NUM; ++x)
    {
        /* 采集UARTX数据 */
        data_len = uart_x_data_acquisition(x, data_buf, sizeof(data_buf));
        /* 保存UARTX数据 */
        snprintf(data_key, sizeof(data_key), "u%dd%u", x, pos); // Key="uXdN"
        EfErrCode ef_ret = ef_set_env_blob(data_key, data_buf, data_len);
        if (ret != EF_NO_ERR)
        { // 保存失败
            /* 输出警告 */
            LOG_W("%s ef_set_env_blob(%s) error!", __FUNCTION__, data_key);
            /* 继续采集其他总线的数据 */
        }
    }
    
    /* 保存时间戳 */
    snprintf(data_key, sizeof(data_key), "d%uts", pos); // Key="dNts"
    EfErrCode ef_ret = ef_set_env_blob(data_key, &now, sizeof(now));
    if (ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(%s) error!", __FUNCTION__, data_key);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    /* 更新FIFO信息 */
    fifo_info.head_pos++; // 指向下一个空位置
    if (fifo_info.head_pos >= HISTORY_DATA_MAX_NUM)
    { // 超出边界
        fifo_info.head_pos = 0; // 回到起点
    }
    fifo_info.length++;
    if (fifo_info.length > HISTORY_DATA_MAX_NUM)
    { // FIFO已满
        /* 删除尾部最旧的数据 */
        fifo_info.tail_pos++;
        if (fifo_info.tail_pos >= HISTORY_DATA_MAX_NUM)
        {  // 超出边界
            fifo_info.tail_pos = 0; // 回到起点
        }
        fifo_info.length = HISTORY_DATA_MAX_NUM;
    }
    /* 保存新的FIFO信息 */
    ef_ret = ef_set_env_blob("history_fifo_info", &fifo_info, sizeof(fifo_info));
    if (ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(history_fifo_info) error!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
__exit:
    
    /* 释放互斥锁 */
    rt_mutex_release(history_fifo_mutex);
    
    return ret;
}

/* 
 * 读取FIFO队列中指定位置的一条历史数据(JSON格式) 
 *
 * 返回读到的数据字节数
 *
 */
static uint32_t read_history_pos_data_json(uint32_t read_pos, char* json_data_buf, uint32_t json_buf_len, bool need_timestamp)
{
    LOG_D("%s(%u)", __FUNCTION__, read_pos);
    
    /* 缓冲区中已读取数据字节数 */
    uint32_t json_data_len = 0;
    
    /* 配置信息 */
    config_info *cfg = cfg_get();
    
    char data_key[16] = "";
    
    json_data_buf[json_data_len++] = '{';
    
    if (need_timestamp)
    {
        /* 读取时间戳 */
        snprintf(data_key, sizeof(data_key), "d%uts", read_pos); // Key="dNts"
        time_t time_stamp = 0;
        size_t len = ef_get_env_blob(data_key, &time_stamp, sizeof(time_stamp), NULL);
        if (len != sizeof(time_stamp))
        {
            LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, data_key);
            return 0;
        }
        
        /* 时间戳编码成JSON格式并写入缓冲区 */
        struct tm* local_time = localtime(&time_stamp);
        json_data_len += rt_snprintf((json_data_buf + json_data_len), (json_buf_len - json_data_len), 
            "\"ts\":\"%04d%02d%02d%02d%02d%02d\",", (local_time->tm_year + 1900), (local_time->tm_mon + 1), 
            local_time->tm_mday, local_time->tm_hour, local_time->tm_min, local_time->tm_sec);
    }
	
    /* 读取UARTX数据 */
    int x = 1;
    for (x = 1; x <= CFG_UART_X_NUM; ++x)
    {
        /* 读取保存的历史数据 */
        uint8_t data_buf[128] = {0};
        snprintf(data_key, sizeof(data_key), "u%dd%u", x, read_pos); // Key="uXdN"
        int len = ef_get_env_blob(data_key, data_buf, sizeof(data_buf), NULL);
        if (len <= 0)
        {
            LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, data_key);
            return 0;
        }
        
#if 0
        /* 转换成JSON格式并写入数据缓冲区 */
        json_data_len += rt_snprintf(json_data_buf + json_data_len, 
            json_buf_len - json_data_len, "\"u%d\":{", x); // 分组名
#endif
        uint32_t data_read_pos = 0; // 变量采集值的读取位置
        int i = 0;
        for (i = 0; i < cfg->uart_x_cfg[x - 1].variablecnt; ++i)
        {
            /* 每个寄存器16bit */
            uint16_t reg_num = cfg->uart_x_cfg[x - 1].length[i]; // 变量寄存器个数
            uint8_t data_type = cfg->uart_x_cfg[x - 1].type[i]; // 变量类型
            uint8_t* var_data = (data_buf + data_read_pos); // 变量数据地址
            uint16_t data_bytes = reg_num * sizeof(uint16_t); // 变量数据字节数
            data_read_pos += data_bytes; // 指向下一个变量起始
#ifndef TEST_NEW_FETURE
            char hex_str_buf[128] = "";
            util_to_hex_str(var_data, data_bytes, hex_str_buf, sizeof(hex_str_buf)); // 转换成HEX字符串格式
            json_data_len += rt_snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                "\"%s\":\"%s\",", cfg->uart_x_cfg[x - 1].variable[i], hex_str_buf);
#else
            switch (data_type)
            {
                case 0x00: // 有符号16位int
                {
                    RT_ASSERT(data_bytes >= sizeof(int16_t));
                    int16_t* int16_data= (int16_t*)var_data; // TODO字节序?
                    json_data_len += rt_snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                        "\"%s\":%d,", cfg->uart_x_cfg[x - 1].variable[i], (int)(*int16_data));
                    break;
                }
                case 0x01: // 无符号16位int
                {
                    RT_ASSERT(data_bytes >= sizeof(uint16_t));
                    uint16_t* uint16_data= (uint16_t*)var_data; // TODO字节序?
                    json_data_len += rt_snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                        "\"%s\":%u,", cfg->uart_x_cfg[x - 1].variable[i], (uint32_t)(*uint16_data));
                    break;
                }
                case 0x02: // 有符号32位int
                {
                    RT_ASSERT(data_bytes >= sizeof(int32_t));
                    int32_t* int32_data= (int32_t*)var_data; // TODO字节序?
                    json_data_len += rt_snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                        "\"%s\":%d,", cfg->uart_x_cfg[x - 1].variable[i], *int32_data);
                    break;
                }
                case 0x03: // 无符号32位int
                {
                    RT_ASSERT(data_bytes >= sizeof(uint32_t));
                    uint32_t* uint32_data= (uint32_t*)var_data; // TODO字节序?
                    json_data_len += rt_snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                        "\"%s\":%u,", cfg->uart_x_cfg[x - 1].variable[i], *uint32_data);
                    break;
                }
                case 0x04: // IEEE754浮点数
                {
                    RT_ASSERT(data_bytes >= sizeof(float));
                    float* float_data = (float*)var_data; // TODO字节序?
                    json_data_len += snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                        "\"%s\":%f,", cfg->uart_x_cfg[x - 1].variable[i], *float_data);
                    break;
                }
                default:
                {
                    RT_ASSERT(0);
                    break;
                }
            }
#endif
        }
#if 0
        if (json_data_len < json_buf_len)
        {
            json_data_buf[json_data_len - 1] = '}'; // 末尾','改成'}'
            json_data_buf[json_data_len++] = ','; // 添加','
        }
#endif
    }
    if (json_data_len < json_buf_len)
    {
        json_data_buf[json_data_len - 1] = '}'; // 末尾','改成'}'
        json_data_buf[json_data_len] = '\0'; // 添加'\0'
    }
    
    return json_data_len;
}

/* 
 * 读取前n个时刻的一条历史数据(JSON格式) 
 *
 *   n=0 读取最近一条历史数据
 *   n>0 读取前n个时刻的一条历史数据
 *
 * 返回实际读取数据字节数
 */
uint32_t read_history_data_json(uint32_t n, char* json_data_buf, uint32_t json_buf_len, bool need_timestamp)
{
    LOG_D("%s(%u)", __FUNCTION__, n);
    
    /* 缓冲区中已读取数据字节数 */
    uint32_t json_data_len = 0;
    
    /* 读取历史数据队列信息 */
    history_fifo_info fifo_info = {
        .length = 0, // 队列长度
        .head_pos = 0, // 头部位置
        .tail_pos = 0 // 尾部位置
    };
    size_t len = ef_get_env_blob("history_fifo_info", &fifo_info, sizeof(fifo_info), NULL);
    if (len != sizeof(fifo_info))
    {
        /* 加载FIFO队列失败(第一次运行?) */
        LOG_E("%s ef_get_env_blob(history_fifo_info) load fail!", __FUNCTION__);
        return 0;
    }
    
    /* 状态检查 */
    if (fifo_info.length <= 0)
    { // 队列为空
        LOG_E("%s history data is empty!", __FUNCTION__);
        return 0;
    }
    
    /* 范围检查 */
    if (n >= fifo_info.length)
    {
        /* 超范围 */
        LOG_E("%s n(%u) not in range[0,%u]!", __FUNCTION__, n, fifo_info.length - 1);
        return 0;
    }
    
    /* 读取前第n条历史数据 */
	n += 1;
    uint32_t read_pos = fifo_info.head_pos; // head_pos指向空位置
    if (fifo_info.head_pos >= n)
    {
        read_pos = fifo_info.head_pos - n;
    }
    else
    {
        read_pos = HISTORY_DATA_MAX_NUM - n;
    }
    
    /* 读取read_pos处的一条历史数据(JSON格式) */
    json_data_len = read_history_pos_data_json(read_pos, json_data_buf, json_buf_len, need_timestamp);
    
    return json_data_len;
}

/* 
 * 清空历史数据
 */
rt_err_t clear_history_data(void)
{
    LOG_D("%s()", __FUNCTION__);

    rt_err_t ret = RT_EOK;
    
    history_fifo_info fifo_info = {
        .length = 0, // 队列长度
        .head_pos = 0, // 头部位置
        .tail_pos = 0 // 尾部位置
    };
    
    /* 确保互斥修改FIFO队列 */
    rt_mutex_take(history_fifo_mutex, RT_WAITING_FOREVER);
    
    /* 清空历史数据队列信息 */
    EfErrCode ef_ret = ef_set_env_blob("history_fifo_info", &fifo_info, sizeof(fifo_info));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(history_fifo_info) error(%d)!", __FUNCTION__, ef_ret);
        ret = -RT_ERROR;
        goto __exit;
    }
    
__exit:

    /* 释放互斥锁 */
    rt_mutex_release(history_fifo_mutex);
    
    return ret;
}

/* 
 * 取得历史数据条目数
 */
uint32_t get_history_data_num(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    /* 读取历史数据队列信息 */
    history_fifo_info fifo_info = {
        .length = 0, // 队列长度
        .head_pos = 0, // 头部位置
        .tail_pos = 0 // 尾部位置
    };
    size_t len = ef_get_env_blob("history_fifo_info", &fifo_info, sizeof(fifo_info), NULL);
    if (len != sizeof(fifo_info))
    {
        /* 加载FIFO队列失败(第一次运行?) */
        LOG_E("%s ef_get_env_blob(history_fifo_info) load fail!", __FUNCTION__);
        return 0;
    }
    
    return fifo_info.length;
}

/* 
 * 读取配置参数(JSON格式) 
 *
 * 返回实际读取数据字节数
 */
static uint32_t read_config_info_json(char* json_data_buf, uint32_t json_buf_len)
{
    LOG_D("%s()", __FUNCTION__);
    
    config_info *cfg = cfg_get();
    char a_ip[32] = "";
    
    inet_ntoa_r(cfg->a_ip, a_ip, sizeof(a_ip));
    char b_ip[32] = "";
    
    inet_ntoa_r(cfg->b_ip, b_ip, sizeof(b_ip));
    int rssi = 0;
    
    get_modem_rssi(&rssi);
    
    rt_int32_t json_data_len = rt_snprintf(json_data_buf, json_buf_len, 
        "{\"cycleSet\":\"%u\",\"acquisitionSet\":\"%u\",\"autoControlSet\":\"0\","
        "\"aIPSet\":\"%s\",\"aPortSet\":\"%u\",\"bIPSet\":\"%s\",\"bPortSet\":\"%u\","
        "\"rssi\":\"%d\",\"version\":\"%s\"}", cfg->cycle, cfg->acquisition, 
        a_ip, cfg->a_port, b_ip, cfg->b_port, rssi, SW_VERSION);
    
    return (uint32_t)json_data_len;
}

/* 
 * 修改配置参数(JSON格式) 
 *
 */
static int set_config_info(c_str_ref *cycle, c_str_ref *acquisition, c_str_ref *autocontrol, 
    c_str_ref *a_ip, c_str_ref *a_port, c_str_ref *b_ip, c_str_ref *b_port, c_str_ref *restart)
{
    LOG_D("%s()", __FUNCTION__);
    
    /* 参数检查和转换 */
    
    /* cycle[可缺省] */
    int cycle_val = 0;
    if (!strref_is_empty(cycle))
    {
        if (!strref_is_int(cycle))
        {
            LOG_E("%s <cycle> not interger!", __FUNCTION__);
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
        cycle_val = strref_to_int(cycle);
        if ((cycle_val < 1) || (cycle_val > 180))
        {
            LOG_E("%s <cycle>(%d) not in range[1,180]!", __FUNCTION__, cycle_val);
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
    }
    
    /* acquisition[可缺省] */
    int acquisition_val = 0;
    if (!strref_is_empty(acquisition))
    {
        if (!strref_is_int(acquisition))
        {
            LOG_E("%s <acquisition> not interger!", __FUNCTION__);
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
        acquisition_val = strref_to_int(acquisition);
        if ((acquisition_val < 1) || (acquisition_val > 30))
        {
            LOG_E("%s <acquisition>(%d) not in range[1,30]!", __FUNCTION__, acquisition_val);
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
    }
    
    /* autocontrol[可缺省] */
    int autocontrol_val = 0;
    if (!strref_is_empty(autocontrol))
    {
        if (!strref_is_int(autocontrol))
        {
            LOG_E("%s <autocontrol> not interger!", __FUNCTION__);
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
        autocontrol_val = strref_to_int(autocontrol);
        if ((autocontrol_val < 0) || (autocontrol_val > 1))
        {
            LOG_E("%s <autocontrol>(%d) not in range[0,1]!", __FUNCTION__, autocontrol_val);
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
    }
    
    /* a_ip[可缺省] */
    in_addr_t a_ip_addr = 0;
    if (!strref_is_empty(a_ip))
    {
        char a_ip_str[32] = "";
        strref_str_cpy(a_ip_str, sizeof(a_ip_str), a_ip);
        a_ip_addr = inet_addr(a_ip_str);
        if (a_ip_addr == IPADDR_NONE)
        {
            LOG_E("%s inet_addr(%s) error!", __FUNCTION__, a_ip_str);
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
    }
    
    /* a_port[可缺省] */
    int a_port_val = 0;
    if (!strref_is_empty(a_port))
    {
        if (!strref_is_int(a_port))
        {
            LOG_E("%s <a_port> not interger!", __FUNCTION__);
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
        a_port_val = strref_to_int(a_port);
        if ((a_port_val < 0) || (a_port_val > 65535))
        {
            LOG_E("%s <a_port>(%d) not in range[0,65535]!", __FUNCTION__, a_port_val);
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
    }
    
    /* b_ip[可缺省] */
    in_addr_t b_ip_addr = 0;
    if (!strref_is_empty(b_ip))
    {
        char b_ip_str[32] = "";
        strref_str_cpy(b_ip_str, sizeof(b_ip_str), b_ip);
        b_ip_addr = inet_addr(b_ip_str);
        if (b_ip_addr == IPADDR_NONE)
        {
            LOG_E("%s inet_addr(%s) error!", __FUNCTION__, b_ip_str);
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
    }
    
    /* b_port[可缺省] */
    int b_port_val = 0;
    if (!strref_is_empty(b_port))
    {
        if (!strref_is_int(b_port))
        {
            LOG_E("%s <b_port> not interger!", __FUNCTION__);
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
        b_port_val = strref_to_int(b_port);
        if ((b_port_val < 0) || (b_port_val > 65535))
        {
            LOG_E("%s <a_port>(%d) not in range[0,65535]!", __FUNCTION__, b_port_val);
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
    }
    
    /* restart[可缺省] */
    int restart_val = 0;
    if (!strref_is_empty(restart))
    {
        if (!strref_is_int(restart))
        {
            LOG_E("%s <restart> not interger!", __FUNCTION__);
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
        restart_val = strref_to_int(restart);
        if ((restart_val < 0) || (restart_val > 1))
        {
            LOG_E("%s <restart>(%d) not in range[0,1]!", __FUNCTION__, restart_val);
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
    }
    
    /* 修改配置参数 */
    
    /* cycle */
    if (!strref_is_empty(cycle))
    {
        EfErrCode ef_ret = ef_set_env_blob("cycle", &cycle_val, sizeof(cycle_val));
        if (ef_ret != EF_NO_ERR)
        {
            LOG_E("%s ef_set_env_blob(cycle,0x%x) error(%d)!", __FUNCTION__, cycle_val, ef_ret);
            /* 500 error 系统内部异常 */
            return 500; // TODO恢复已成功的部分设置
        }
    }
    
    /* acquisition */
    if (!strref_is_empty(acquisition))
    {
        EfErrCode ef_ret = ef_set_env_blob("acquisition", &acquisition_val, sizeof(acquisition_val));
        if (ef_ret != EF_NO_ERR)
        {
            LOG_E("%s ef_set_env_blob(acquisition,0x%x) error(%d)!", __FUNCTION__, acquisition_val, ef_ret);
            /* 500 error 系统内部异常 */
            return 500; // TODO恢复已成功的部分设置
        }
    }
    
    /* a_ip */
    if (!strref_is_empty(a_ip))
    {
        EfErrCode ef_ret = ef_set_env_blob("a_ip", &a_ip_addr, sizeof(a_ip_addr));
        if (ef_ret != EF_NO_ERR)
        {
            LOG_E("%s ef_set_env_blob(a_ip,0x%x) error(%d)!", __FUNCTION__, a_ip_addr, ef_ret);
            /* 500 error 系统内部异常 */
            return 500; // TODO恢复已成功的部分设置
        }
    }
    
    /* a_port */
    if (!strref_is_empty(a_port))
    {
        EfErrCode ef_ret = ef_set_env_blob("a_port", &a_port_val, sizeof(a_port_val));
        if (ef_ret != EF_NO_ERR)
        {
            LOG_E("%s ef_set_env_blob(a_port,0x%x) error(%d)!", __FUNCTION__, a_port_val, ef_ret);
            /* 500 error 系统内部异常 */
            return 500; // TODO恢复已成功的部分设置
        }
    }
    
    /* b_ip */
    if (!strref_is_empty(b_ip))
    {
        EfErrCode ef_ret = ef_set_env_blob("b_ip", &b_ip_addr, sizeof(b_ip_addr));
        if (ef_ret != EF_NO_ERR)
        {
            LOG_E("%s ef_set_env_blob(b_ip,0x%x) error(%d)!", __FUNCTION__, b_ip_addr, ef_ret);
            /* 500 error 系统内部异常 */
            return 500; // TODO恢复已成功的部分设置
        }
    }
    
    /* b_port */
    if (!strref_is_empty(b_port))
    {
        EfErrCode ef_ret = ef_set_env_blob("b_port", &b_port_val, sizeof(b_port_val));
        if (ef_ret != EF_NO_ERR)
        {
            LOG_E("%s ef_set_env_blob(b_port,0x%x) error(%d)!", __FUNCTION__, b_port_val, ef_ret);
            /* 500 error 系统内部异常 */
            return 500; // TODO恢复已成功的部分设置
        }
    }
    
    /* 200 success 请求成功 */
    return 200;
}

static void tbss_netdev_status_callback(struct netdev *netdev, enum netdev_cb_type type)
{
    LOG_D("%s(%d)", __FUNCTION__, type);
    
    switch (type)
    {
        case NETDEV_CB_ADDR_IP:                 /* IP address */
            LOG_D("NETDEV_CB_ADDR_IP");
            break;
        case NETDEV_CB_ADDR_NETMASK:            /* subnet mask */
            LOG_D("NETDEV_CB_ADDR_NETMASK");
            break;
        case NETDEV_CB_ADDR_GATEWAY:            /* netmask */
            LOG_D("NETDEV_CB_ADDR_GATEWAY");
            break;
        case NETDEV_CB_ADDR_DNS_SERVER:         /* dns server */
            LOG_D("NETDEV_CB_ADDR_DNS_SERVER");
            break;
        case NETDEV_CB_STATUS_UP:               /* changed to 'up' */
            LOG_D("NETDEV_CB_STATUS_UP");
            break;
        case NETDEV_CB_STATUS_DOWN:             /* changed to 'down' */
            LOG_D("NETDEV_CB_STATUS_DOWN");
            break;
        case NETDEV_CB_STATUS_LINK_UP:          /* changed to 'link up' */
            LOG_D("NETDEV_CB_STATUS_LINK_UP");
            app_send_msg(APP_MSG_MQTT_CLIENT_START_REQ, RT_NULL); // 发送MQTT客户端启动请求
            break;
        case NETDEV_CB_STATUS_LINK_DOWN:        /* changed to 'link down' */
            LOG_D("NETDEV_CB_STATUS_LINK_DOWN");
            break;
        case NETDEV_CB_STATUS_INTERNET_UP:      /* changed to 'internet up' */
            LOG_D("NETDEV_CB_STATUS_INTERNET_UP");
            break;
        case NETDEV_CB_STATUS_INTERNET_DOWN:    /* changed to 'internet down' */
            LOG_D("NETDEV_CB_STATUS_INTERNET_DOWN");
            break;
        case NETDEV_CB_STATUS_DHCP_ENABLE:      /* enable DHCP capability */
            LOG_D("NETDEV_CB_STATUS_DHCP_ENABLE");
            break;
        case NETDEV_CB_STATUS_DHCP_DISABLE:     /* disable DHCP capability */
            LOG_D("NETDEV_CB_STATUS_DHCP_DISABLE");
            break;
    }
}

static void report_timer_timeout(void *parameter)
{
    LOG_D("%s()", __FUNCTION__);
    
    app_send_msg(APP_MSG_DATA_REPORT_REQ, RT_NULL); // 发送数据主动上报请求
}

static void acquisition_timer_timeout(void *parameter)
{
    LOG_D("%s()", __FUNCTION__);
    
    app_send_msg(APP_MSG_DATA_ACQUISITION_REQ, RT_NULL); // 发送数据采集请求
}

static void mqtt_sub_default_callback(mqtt_client *client, message_data *msg)
{
    *((char *)msg->message->payload + msg->message->payloadlen) = '\0';
    LOG_D("%s mqtt sub default callback: %.*s %.*s",
               __FUNCTION__,
               msg->topic_name->lenstring.len,
               msg->topic_name->lenstring.data,
               msg->message->payloadlen,
               (char *)msg->message->payload);
}

static void mqtt_connect_callback(mqtt_client *client)
{
    LOG_D("%s()", __FUNCTION__);
}

static void mqtt_online_callback(mqtt_client *client)
{
    LOG_D("%s()", __FUNCTION__);
    
    app_send_msg(APP_MSG_MQTT_CLIENT_ONLINE, RT_NULL); // MQTT客户端已上线
}

static void mqtt_offline_callback(mqtt_client *client)
{
    LOG_D("%s()", __FUNCTION__);
    
    app_send_msg(APP_MSG_MQTT_CLIENT_OFFLINE, RT_NULL); // MQTT客户端已下线
}

static void topic_telemetry_get_handler(mqtt_client *client, message_data *msg)
{
    (void) client;
    const char *json_str = (const char*)(msg->message->payload);
    size_t json_str_len = msg->message->payloadlen;
    c_str_ref productkey = {0, NULL}; // productKey
    c_str_ref devicecode = {0, NULL}; // deviceCode
    c_str_ref operationdate = {0, NULL}; // operationDate
    c_str_ref id = {0, NULL}; // id
    char *topic_buf = NULL;
    char *json_data_buf = NULL;
    rt_err_t ret = RT_EOK;
    int i = 0;
    
    LOG_I("%s() %s %s", __FUNCTION__, msg->topic_name->cstring, json_str);
    
    /* 解析JSON字符串 */
    jsmn_parser json_paser;
    jsmn_init(&json_paser);
    jsmntok_t tok_list[32];
    int list_len = jsmn_parse(&json_paser, json_str, json_str_len, tok_list, ARRAY_SIZE(tok_list));
    if (list_len == JSMN_ERROR_PART)
    {
        LOG_E("%s jsmn_parse failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    for (i = 0; i < list_len; ++i)
    {
        if (JSMN_STRING == tok_list[i].type)
		{
			const char *token_str = json_str + tok_list[i].start;
			int token_str_len = tok_list[i].end - tok_list[i].start;
			if ((token_str_len == STR_LEN("productKey")) 
				&& (0 == memcmp(token_str, "productKey", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                productkey.c_str = json_str + tok_list[i].start;
                productkey.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("deviceCode")) 
				&& (0 == memcmp(token_str, "deviceCode", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                devicecode.c_str = json_str + tok_list[i].start;
                devicecode.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("operationDate")) 
				&& (0 == memcmp(token_str, "operationDate", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                operationdate.c_str = json_str + tok_list[i].start;
                operationdate.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("id")) 
				&& (0 == memcmp(token_str, "id", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                id.c_str = json_str + tok_list[i].start;
                id.len = tok_list[i].end - tok_list[i].start;
			}
		}
    }
    
    /* 检查productKey和deviceCode */
    if (strref_str_cmp(get_productkey(), &productkey) != 0)
    {
        LOG_E("%s productkey check failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    if (strref_str_cmp(get_devicecode(), &devicecode) != 0)
    {
        LOG_E("%s devicecode check failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    if (strref_is_empty(&id))
    {
        LOG_E("%s id is empty!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    /* 分配响应消息用的内存 */
    topic_buf = (char*)rt_malloc(MQTT_TOPIC_BUF_LEN);
    if (topic_buf == NULL)
    {
        LOG_E("%s rt_malloc(%d) failed!", __FUNCTION__, MQTT_TOPIC_BUF_LEN);
        ret = -RT_ENOMEM;
        goto __exit;
    }
    
    json_data_buf = (char*)rt_malloc(JSON_DATA_BUF_LEN);
    if (json_data_buf == NULL)
    {
        LOG_E("%s rt_malloc(%d) failed!", __FUNCTION__, JSON_DATA_BUF_LEN);
        ret = -RT_ENOMEM;
        goto __exit;
    }
	
    /* 编码JSON采集数据并发送 */
    {
		time_t time_stamp = time(RT_NULL); // 上报时间戳
        rt_int32_t json_data_len = rt_snprintf(json_data_buf, JSON_DATA_BUF_LEN, 
            "{\"productKey\":\"%s\",\"deviceCode\":\"%s\",\"clientId\":\"%010u\",\"itemId\":\"%s\","
			"\"timeStamp\":\"%u\",\"requestId\":\"%.*s\",\"data\":", get_productkey(), get_devicecode(), 
			get_clientid(), get_itemid(), time_stamp, id.len, id.c_str);
        
        /* 读取最新采集的数据(JSON格式) */
        uint32_t read_len = read_history_data_json(0, json_data_buf + json_data_len, JSON_DATA_BUF_LEN - json_data_len, false);
        if (read_len <= 0)
        {
            LOG_E("%s read_history_data_json(read_len=%d) failed!", __FUNCTION__, read_len);
            ret = -RT_ERROR;
            goto __exit;
        }
        json_data_len += read_len;
        RT_ASSERT(json_data_len < JSON_DATA_BUF_LEN);
        json_data_buf[json_data_len++] = '}';
        RT_ASSERT(json_data_len < JSON_DATA_BUF_LEN);
        json_data_buf[json_data_len] = '\0';
        
        /* 请求发布/sys/${productKey}/${deviceCode}/telemetry/get_reply响应 */
        rt_snprintf(topic_buf, MQTT_TOPIC_BUF_LEN, "/sys/%s/%s/telemetry/get_reply", get_productkey(), get_devicecode());
        LOG_D("publish(%s) %s", topic_buf, json_data_buf);
        
        /* 请求主线程来发布MQTT数据(回调函数不能阻塞) */
        ret = send_mqtt_publish_req(QOS1, topic_buf, json_data_buf, json_data_len);
        if (ret != RT_EOK)
        {
            LOG_E("%s send_mqtt_publish_req() error(%d)!", __FUNCTION__, ret);
            goto __exit;
        }
    }
    
__exit:
    
    if (ret != RT_EOK)
    {
        if (topic_buf != NULL)
        {
            rt_free(topic_buf);
            topic_buf = NULL;
        }
        
        if (json_data_buf != NULL)
        {
            rt_free(json_data_buf);
            json_data_buf = NULL;
        }
    }
}

static void topic_config_get_handler(mqtt_client *client, message_data *msg)
{
    (void) client;
    const char *json_str = (const char*)(msg->message->payload);
    size_t json_str_len = msg->message->payloadlen;
    c_str_ref productkey = {0, NULL}; // productKey
    c_str_ref devicecode = {0, NULL}; // deviceCode
    c_str_ref operationdate = {0, NULL}; // operationDate
    c_str_ref id = {0, NULL}; // id
    char *topic_buf = NULL;
    char *json_data_buf = NULL;
    rt_err_t ret = RT_EOK;
    int i = 0;
    
    LOG_I("%s() %s %s", __FUNCTION__, msg->topic_name->cstring, json_str);
    
    /* 解析JSON字符串 */
    jsmn_parser json_paser;
    jsmn_init(&json_paser);
    jsmntok_t tok_list[32];
    int list_len = jsmn_parse(&json_paser, json_str, json_str_len, tok_list, ARRAY_SIZE(tok_list));
    if (list_len == JSMN_ERROR_PART)
    {
        LOG_E("%s jsmn_parse failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    for (i = 0; i < list_len; ++i)
    {
        if (JSMN_STRING == tok_list[i].type)
		{
			const char *token_str = json_str + tok_list[i].start;
			int token_str_len = tok_list[i].end - tok_list[i].start;
			if ((token_str_len == STR_LEN("productKey")) 
				&& (0 == memcmp(token_str, "productKey", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                productkey.c_str = json_str + tok_list[i].start;
                productkey.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("deviceCode")) 
				&& (0 == memcmp(token_str, "deviceCode", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                devicecode.c_str = json_str + tok_list[i].start;
                devicecode.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("operationDate")) 
				&& (0 == memcmp(token_str, "operationDate", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                operationdate.c_str = json_str + tok_list[i].start;
                operationdate.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("id")) 
				&& (0 == memcmp(token_str, "id", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                id.c_str = json_str + tok_list[i].start;
                id.len = tok_list[i].end - tok_list[i].start;
			}
		}
    }
    
    /* 检查productKey和deviceCode */
    if (strref_str_cmp(get_productkey(), &productkey) != 0)
    {
        LOG_E("%s productkey check failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    if (strref_str_cmp(get_devicecode(), &devicecode) != 0)
    {
        LOG_E("%s devicecode check failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    if (strref_is_empty(&id))
    {
        LOG_E("%s id is empty!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    /* 分配响应消息用的内存 */
    topic_buf = (char*)rt_malloc(MQTT_TOPIC_BUF_LEN);
    if (topic_buf == NULL)
    {
        LOG_E("%s rt_malloc(%d) failed!", __FUNCTION__, MQTT_TOPIC_BUF_LEN);
        ret = -RT_ENOMEM;
        goto __exit;
    }
    
    json_data_buf = (char*)rt_malloc(JSON_DATA_BUF_LEN);
    if (json_data_buf == NULL)
    {
        LOG_E("%s rt_malloc(%d) failed!", __FUNCTION__, JSON_DATA_BUF_LEN);
        ret = -RT_ENOMEM;
        goto __exit;
    }
    
    /* 编码JSON配置信息并发送 */
    {
        rt_int32_t json_data_len = rt_snprintf(json_data_buf, JSON_DATA_BUF_LEN, 
            "{\"status\":\"200\",\"productKey\":\"%s\",\"deviceCode\":\"%s\",\"requestId\":\"%.*s\",\"data\":", 
             get_productkey(), get_devicecode(), id.len, id.c_str);
        
        /* 读取配置信息(JSON格式)  */
        uint32_t read_len = read_config_info_json(json_data_buf + json_data_len, JSON_DATA_BUF_LEN - json_data_len);
        if (read_len <= 0)
        {
            LOG_E("%s read_config_info_json(read_len=%d) failed!", __FUNCTION__, read_len);
            
            json_data_len = rt_snprintf(json_data_buf, JSON_DATA_BUF_LEN, 
                "{\"status\":\"500\",\"productKey\":\"%s\",\"deviceCode\":\"%s\",\"requestId\":\"%.*s\"}", 
                 get_productkey(), get_devicecode(), id.len, id.c_str);
            RT_ASSERT(json_data_len < JSON_DATA_BUF_LEN);
            json_data_buf[json_data_len] = '\0';
        }
        else
        {
            json_data_len += read_len;
            RT_ASSERT(json_data_len < JSON_DATA_BUF_LEN);
            json_data_buf[json_data_len++] = '}';
            RT_ASSERT(json_data_len < JSON_DATA_BUF_LEN);
            json_data_buf[json_data_len] = '\0';
        }
        
        /* 发布/sys/${productKey}/${deviceCode}/config/get_reply响应 */
        rt_snprintf(topic_buf, MQTT_TOPIC_BUF_LEN, "/sys/%s/%s/config/get_reply", get_productkey(), get_devicecode());
        LOG_D("%s publish(%s) %s", __FUNCTION__, topic_buf, json_data_buf);
        
        /* 请求主线程来发布MQTT数据(回调函数不能阻塞) */
        ret = send_mqtt_publish_req(QOS1, topic_buf, json_data_buf, json_data_len);
        if (ret != RT_EOK)
        {
            LOG_E("%s send_mqtt_publish_req() error(%d)!", __FUNCTION__, ret);
            goto __exit;
        }
    }
    
__exit:
    
    if (ret != RT_EOK)
    {
        if (topic_buf != NULL)
        {
            rt_free(topic_buf);
            topic_buf = NULL;
        }
        
        if (json_data_buf != NULL)
        {
            rt_free(json_data_buf);
            json_data_buf = NULL;
        }
    }
}

/* 取得set_reply错误码对应的文本描述 */
static const char* cfg_ret_code_get_message(int cfg_ret_code)
{
    switch (cfg_ret_code)
    {
        case 200:
            return "success";
        case 500:
            return "error";
        case 410:
            return "too many requests";
        case 420:
            return "request parameter error";
        default:
            break;
    }
    return "";
}

static void topic_config_set_handler(mqtt_client *client, message_data *msg)
{
    (void) client;
    const char *json_str = (const char*)(msg->message->payload);
    size_t json_str_len = msg->message->payloadlen;
    c_str_ref productkey = {0, NULL}; // productKey
    c_str_ref devicecode = {0, NULL}; // deviceCode
    c_str_ref operationdate = {0, NULL}; // operationDate
    c_str_ref id = {0, NULL}; // id
    c_str_ref cycle = {0, NULL}; // cycleSet
    c_str_ref acquisition = {0, NULL}; // acquisitionSet
    c_str_ref autocontrol = {0, NULL}; // autoControlSet
    c_str_ref a_ip = {0, NULL}; // aIPSet
    c_str_ref a_port = {0, NULL}; // aPortSet
    c_str_ref b_ip = {0, NULL}; // bIPSet
    c_str_ref b_port = {0, NULL}; // bPortSet
    c_str_ref restart = {0, NULL}; // restart
    char *topic_buf = NULL;
    char *json_data_buf = NULL;
    rt_err_t ret = RT_EOK;
    int cfg_ret_code = 0;
    int i = 0;
    
    LOG_I("%s() %s %s", __FUNCTION__, msg->topic_name->cstring, json_str);
    
    /* 解析JSON字符串 */
    jsmn_parser json_paser;
    jsmn_init(&json_paser);
    jsmntok_t tok_list[32];
    int list_len = jsmn_parse(&json_paser, json_str, json_str_len, tok_list, ARRAY_SIZE(tok_list));
    if (list_len == JSMN_ERROR_PART)
    {
        LOG_E("%s jsmn_parse failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    for (i = 0; i < list_len; ++i)
    {
        if (JSMN_STRING == tok_list[i].type)
		{
			const char *token_str = json_str + tok_list[i].start;
			int token_str_len = tok_list[i].end - tok_list[i].start;
			if ((token_str_len == STR_LEN("productKey")) 
				&& (0 == memcmp(token_str, "productKey", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                productkey.c_str = json_str + tok_list[i].start;
                productkey.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("deviceCode")) 
				&& (0 == memcmp(token_str, "deviceCode", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                devicecode.c_str = json_str + tok_list[i].start;
                devicecode.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("operationDate")) 
				&& (0 == memcmp(token_str, "operationDate", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                operationdate.c_str = json_str + tok_list[i].start;
                operationdate.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("id")) 
				&& (0 == memcmp(token_str, "id", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                id.c_str = json_str + tok_list[i].start;
                id.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("cycleSet")) 
				&& (0 == memcmp(token_str, "cycleSet", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                cycle.c_str = json_str + tok_list[i].start;
                cycle.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("acquisitionSet")) 
				&& (0 == memcmp(token_str, "acquisitionSet", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                acquisition.c_str = json_str + tok_list[i].start;
                acquisition.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("autoControlSet")) 
				&& (0 == memcmp(token_str, "autoControlSet", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                autocontrol.c_str = json_str + tok_list[i].start;
                autocontrol.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("aIPSet")) 
				&& (0 == memcmp(token_str, "aIPSet", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                a_ip.c_str = json_str + tok_list[i].start;
                a_ip.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("aPortSet")) 
				&& (0 == memcmp(token_str, "aPortSet", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                a_port.c_str = json_str + tok_list[i].start;
                a_port.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("bIPSet")) 
				&& (0 == memcmp(token_str, "bIPSet", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                b_ip.c_str = json_str + tok_list[i].start;
                b_ip.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("bPortSet")) 
				&& (0 == memcmp(token_str, "bPortSet", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                b_port.c_str = json_str + tok_list[i].start;
                b_port.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("restart")) 
				&& (0 == memcmp(token_str, "restart", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                restart.c_str = json_str + tok_list[i].start;
                restart.len = tok_list[i].end - tok_list[i].start;
			}
		}
    }
    
    /* 检查productKey和deviceCode */
    if (strref_str_cmp(get_productkey(), &productkey) != 0)
    {
        LOG_E("%s productkey check failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    if (strref_str_cmp(get_devicecode(), &devicecode) != 0)
    {
        LOG_E("%s devicecode check failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    if (strref_is_empty(&id))
    {
        LOG_E("%s id is empty!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    if (strref_is_empty(&operationdate))
    {
        LOG_E("%s operationdate is empty!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    /* 修改配置 */
    cfg_ret_code = set_config_info(&cycle, &acquisition, &autocontrol, 
        &a_ip, &a_port, &b_ip, &b_port, &restart);
    
    /* 分配响应消息用的内存 */
    topic_buf = (char*)rt_malloc(MQTT_TOPIC_BUF_LEN);
    if (topic_buf == NULL)
    {
        LOG_E("%s rt_malloc(%d) failed!", __FUNCTION__, MQTT_TOPIC_BUF_LEN);
        ret = -RT_ENOMEM;
        goto __exit;
    }
    
    json_data_buf = (char*)rt_malloc(JSON_DATA_BUF_LEN);
    if (json_data_buf == NULL)
    {
        LOG_E("%s rt_malloc(%d) failed!", __FUNCTION__, JSON_DATA_BUF_LEN);
        ret = -RT_ENOMEM;
        goto __exit;
    }
    
    /* 编码响应信息并发送 */
    {
        /* 消息内容 */
        time_t now = time(RT_NULL);
        rt_snprintf(topic_buf, MQTT_TOPIC_BUF_LEN, "/sys/%s/%s/config/set", get_productkey(), get_devicecode());
        rt_int32_t json_data_len = rt_snprintf(json_data_buf, JSON_DATA_BUF_LEN, 
            "{\"productKey\":\"%s\",\"deviceCode\":\"%s\",\"operationDate\":\"%u\",\"requestId\":\"%.*s\","
            "\"code\":\"%d\",\"message\":\"%s\",\"topic\":\"%s\",\"data\":{}}", get_productkey(), get_devicecode(), 
            now, id.len, id.c_str, cfg_ret_code, cfg_ret_code_get_message(cfg_ret_code), topic_buf);
        RT_ASSERT(json_data_len < JSON_DATA_BUF_LEN);
        json_data_buf[json_data_len] = '\0';
        
        /* 发布/sys/${productKey}/${deviceCode}/config/set_reply响应 */
        rt_snprintf(topic_buf, MQTT_TOPIC_BUF_LEN, "/sys/%s/%s/config/set_reply", get_productkey(), get_devicecode());
        LOG_D("%s publish(%s) %s", __FUNCTION__, topic_buf, json_data_buf);
        
        /* 请求主线程来发布MQTT数据(回调函数不能阻塞) */
        ret = send_mqtt_publish_req(QOS1, topic_buf, json_data_buf, json_data_len);
        if (ret != RT_EOK)
        {
            LOG_E("%s send_mqtt_publish_req() error(%d)!", __FUNCTION__, ret);
            goto __exit;
        }
    }
    
__exit:
    if (ret != RT_EOK)
    {
        if (topic_buf != NULL)
        {
            rt_free(topic_buf);
            topic_buf = NULL;
        }
        
        if (json_data_buf != NULL)
        {
            rt_free(json_data_buf);
            json_data_buf = NULL;
        }
    }
    else
    {
        if (strref_str_cmp("1", &restart) == 0)
        {
            /* 等待响应发送完毕 */
            rt_thread_mdelay(10 * 1000);
            /* 重启 */
            rt_hw_cpu_reset();
        }
    }
}

static void topic_telemetry_result_handler(mqtt_client *client, message_data *msg)
{
    (void) client;
    const char *json_str = (const char*)(msg->message->payload);
    size_t json_str_len = msg->message->payloadlen;
    c_str_ref productkey = {0, NULL}; // productKey
    c_str_ref devicecode = {0, NULL}; // deviceCode
    c_str_ref operationdate = {0, NULL}; // operationDate
    c_str_ref id = {0, NULL}; // id
    c_str_ref code = {0, NULL}; // code
    c_str_ref message = {0, NULL}; // message
    c_str_ref topic = {0, NULL}; // topic
    c_str_ref data = {0, NULL}; // data
    int i = 0;
    
    LOG_I("%s() %s %s", __FUNCTION__, msg->topic_name->cstring, json_str);
    
    /* 解析JSON字符串 */
    jsmn_parser json_paser;
    jsmn_init(&json_paser);
    jsmntok_t tok_list[32];
    int list_len = jsmn_parse(&json_paser, json_str, json_str_len, tok_list, ARRAY_SIZE(tok_list));
    if (list_len == JSMN_ERROR_PART)
    {
        LOG_E("%s jsmn_parse failed!", __FUNCTION__);
        goto __exit;
    }
    
    for (i = 0; i < list_len; ++i)
    {
        const char *token_str = json_str + tok_list[i].start;
        int token_str_len = tok_list[i].end - tok_list[i].start;
        
        if (JSMN_STRING == tok_list[i].type)
		{
			if ((token_str_len == STR_LEN("productKey")) 
				&& (0 == memcmp(token_str, "productKey", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                productkey.c_str = json_str + tok_list[i].start;
                productkey.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("deviceCode")) 
				&& (0 == memcmp(token_str, "deviceCode", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                devicecode.c_str = json_str + tok_list[i].start;
                devicecode.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("operationDate")) 
				&& (0 == memcmp(token_str, "operationDate", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                operationdate.c_str = json_str + tok_list[i].start;
                operationdate.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("id")) 
				&& (0 == memcmp(token_str, "id", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                id.c_str = json_str + tok_list[i].start;
                id.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("code")) 
				&& (0 == memcmp(token_str, "code", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                code.c_str = json_str + tok_list[i].start;
                code.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("message")) 
				&& (0 == memcmp(token_str, "message", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                message.c_str = json_str + tok_list[i].start;
                message.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("topic")) 
				&& (0 == memcmp(token_str, "topic", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                topic.c_str = json_str + tok_list[i].start;
                topic.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("data")) 
				&& (0 == memcmp(token_str, "data", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                data.c_str = json_str + tok_list[i].start;
                data.len = tok_list[i].end - tok_list[i].start;
			}
		}
    }
    
    /* 检查productKey和deviceCode */
    if (strref_str_cmp(get_productkey(), &productkey) != 0)
    {
        LOG_E("%s productkey check failed!", __FUNCTION__);
        goto __exit;
    }
    if (strref_str_cmp(get_devicecode(), &devicecode) != 0)
    {
        LOG_E("%s devicecode check failed!", __FUNCTION__);
        goto __exit;
    }
    
    // TODO
    
__exit:
    return;
}

/* 升级进度文本 */
static const char* get_upgrade_progress_desc(int step)
{
    switch (step)
    {
        case -1:
            return "download fail";
        case -2:
            return "md5 check fail";
        case -3:
            return "upgrade fail";
        case 1:
            return "download success";
        case 2:
            return "md5 check success";
        case 200:
            return "upgrade success";
        default:
            return "";
    }
    return "";
}

/* 发送升级进度回复消息 */
static rt_err_t send_upgrade_progress(c_str_ref* req_id, int step)
{
    char *topic_buf = NULL;
    char *json_data_buf = NULL;
    rt_err_t ret = RT_EOK;
    
    /* 分配响应消息用的内存 */
    topic_buf = (char*)rt_malloc(MQTT_TOPIC_BUF_LEN);
    if (topic_buf == NULL)
    {
        LOG_E("%s rt_malloc(%d) failed!", __FUNCTION__, MQTT_TOPIC_BUF_LEN);
        ret = -RT_ENOMEM;
        goto __exit;
    }
    
    json_data_buf = (char*)rt_malloc(JSON_DATA_BUF_LEN);
    if (json_data_buf == NULL)
    {
        LOG_E("%s rt_malloc(%d) failed!", __FUNCTION__, JSON_DATA_BUF_LEN);
        ret = -RT_ENOMEM;
        goto __exit;
    }
    
    /* 编码响应信息并发送 */
    {
        /* 消息内容 */
        time_t now = time(RT_NULL);
        rt_int32_t json_data_len = rt_snprintf(json_data_buf, JSON_DATA_BUF_LEN, 
            "{\"productKey\":\"%s\",\"deviceCode\":\"%s\",\"operationDate\":\"%u\",\"requestId\":\"%.*s\","
            "\"params\":{\"step\":\"%d\",\"desc\":\"%s\"}}", get_productkey(), get_devicecode(), now, 
            req_id->len, req_id->c_str, step, get_upgrade_progress_desc(step));
        RT_ASSERT(json_data_len < JSON_DATA_BUF_LEN);
        json_data_buf[json_data_len] = '\0';
        
        /* 发布/sys/${productKey}/${deviceCode}/upgrade/progress响应 */
        rt_snprintf(topic_buf, MQTT_TOPIC_BUF_LEN, "/sys/%s/%s/upgrade/progress", get_productkey(), get_devicecode());
        LOG_D("%s publish(%s) %s", __FUNCTION__, topic_buf, json_data_buf);
        
        /* 请求主线程来发布MQTT数据(回调函数不能阻塞) */
        ret = send_mqtt_publish_req(QOS1, topic_buf, json_data_buf, json_data_len);
        if (ret != RT_EOK)
        {
            LOG_E("%s send_mqtt_publish_req() error(%d)!", __FUNCTION__, ret);
            goto __exit;
        }
    }
    
__exit:
    if (ret != RT_EOK)
    {
        if (topic_buf != NULL)
        {
            rt_free(topic_buf);
            topic_buf = NULL;
        }
        
        if (json_data_buf != NULL)
        {
            rt_free(json_data_buf);
            json_data_buf = NULL;
        }
    }
    
    return ret;
}

/* 保存已下载待升级的OTA固件版本号,用于重启升级后验证是否升级成功 */
static rt_err_t save_ota_version(c_str_ref *ota_version, c_str_ref *ota_req_id)
{
    LOG_D("%s() ota_version=%.*s, ota_req_id=%.*s", __FUNCTION__, 
        ota_version->len, ota_version->c_str, ota_req_id->len, ota_req_id->c_str);
    
    /* 保存已下载待升级的OTA固件版本号 */
    EfErrCode ef_ret = ef_set_env_blob("ota_version", ota_version->c_str, ota_version->len);
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(ota_version) error(%d)!", __FUNCTION__, ef_ret);
        return -RT_ERROR;
    }
    
    /* 保存OTA请求ID */
    ef_ret = ef_set_env_blob("ota_req_id", ota_req_id->c_str, ota_req_id->len);
    if (ef_ret != EF_NO_ERR)
    {
        ef_del_env("ota_version");
        
        LOG_E("%s ef_set_env_blob(ota_req_id) error(%d)!", __FUNCTION__, ef_ret);
        return -RT_ERROR;
    }
    
    return RT_EOK;
}

/* 用于重启后检查是否成功进行了OTA,并上报进度信息(不可重入,非线程安全) */
static void check_and_report_ota_process(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    /* 避免不必要的多次执行 */
    static bool is_done = false; // 本函数是否已执行
    if (is_done)
    { // 已执行
        return;
    }
    is_done = true;
    
    char ota_version[32] = "";
    size_t ver_len = ef_get_env_blob("ota_version", ota_version, sizeof(ota_version), RT_NULL);
    if ((ver_len <= 0) || (ver_len >= sizeof(ota_version)))
    { // 没有读取到有效的ota_version
        LOG_I("%s ef_get_env_blob(ota_version) not found!", __FUNCTION__);
        /* 认为没有请求进行OTA升级 */
        return;
    }
    ota_version[ver_len] = '\0';
    
    /* 读取OTA请求ID */
    char ota_req_id[32] = "";
    size_t id_len = ef_get_env_blob("ota_req_id", ota_req_id, sizeof(ota_req_id), RT_NULL);
    if ((id_len <= 0) || (id_len >= sizeof(ota_req_id)))
    { // 没有读取到有效的ota_req_id
        LOG_I("%s ef_get_env_blob(ota_req_id) not found!", __FUNCTION__);
        /* 认为没有请求进行OTA升级 */
        return;
    }
    ota_req_id[id_len] = '\0';
    
    c_str_ref id = {id_len, ota_req_id};
    int step = 0;
    
    /* 检查软件版本号是否和期望的版本号一致 */
    if (strcmp(ota_version, SW_VERSION) == 0)
    { // 一致
        step = 200; // OTA重启升级成功
    }
    else
    { // 不一致
        step = -3; // OTA重启升级失败
    }
    
    /* 上报OTA升级结果(成功/失败) */
    rt_err_t ret = send_upgrade_progress(&id, step);
    if (ret != RT_EOK)
    { // 上报失败
        LOG_W("%s send_upgrade_progress(%s,%d) error(%d)!", __FUNCTION__, ota_req_id, step, ret);
    }
    
    /* 清除保存的ota_version */
    EfErrCode ef_ret = ef_del_env("ota_version");
    if (ef_ret != EF_NO_ERR)
    {
        LOG_W("%s ef_del_env(ota_version) error(%d)!", __FUNCTION__, ef_ret);
        
        /* 清除失败,下次重启将再次上报! */
    }
}

/* HTTP OTA固件下载和升级线程 */
static void http_ota_thread_proc(void *param)
{
    LOG_D("%s()", __FUNCTION__);
    
    http_ota_info *ota_info = (http_ota_info*)param;
    RT_ASSERT(ota_info != RT_NULL);
    
    c_str_ref id = {strlen(ota_info->req_id), ota_info->req_id};
    c_str_ref version = {strlen(ota_info->version), ota_info->version};
    
    /* 下载并校验OTA固件(耗时操作) */
    int ret = http_ota_fw_download(ota_info->url, ota_info->firmware_size, ota_info->md5);
    switch (ret)
    {
        case 0: // 下载并校验成功
        {
            /* 上报下载成功 */
            send_upgrade_progress(&id, 1);
            
            /* 上报校验成功 */
            send_upgrade_progress(&id, 2);
            
            /* 保存已下载待升级的OTA固件版本号,用于重启升级后验证是否升级成功 */
            rt_err_t ret = save_ota_version(&version, &id);
            if (ret != RT_EOK)
            {
                send_upgrade_progress(&id, -3); // 直接上报升级失败
                break;
            }

            /* 重启系统进行升级 */
            http_ota_reboot();
            break;
        }
        case -1: // 下载失败
        {
            /* 上报下载失败 */
            send_upgrade_progress(&id, -1);
            break;
        }
        case -2: // 校验失败
        {
            /* 上报校验失败 */
            send_upgrade_progress(&id, -2);
            break;
        }
        default:
        {
            RT_ASSERT(0);
            break;
        }
    }
    
    rt_free(ota_info);
    
    app_send_msg(APP_MSG_MQTT_CLIENT_ONLINE, RT_NULL); // HTTP OTA线程已退出
}

/* 执行HTTP OTA请求 */
static rt_err_t do_http_ota_request(c_str_ref *url, uint8_t *md5, c_str_ref *req_id, c_str_ref *version, int firmware_size)
{
    LOG_D("%s()", __FUNCTION__);
    
    rt_err_t ret = RT_EOK;
    http_ota_info *ota_info = RT_NULL;
    
    rt_mutex_take(http_ota_mutex, RT_WAITING_FOREVER);
    
    if (http_ota_thread != RT_NULL)
    { // HTTP OTA请求正在处理中(不允许重复请求)
        LOG_E("%s http ota is in processing!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    ota_info = (http_ota_info*)rt_malloc(sizeof(http_ota_info));
    if (ota_info == RT_NULL)
    {
        LOG_E("%s rt_malloc(%u) failed!", __FUNCTION__, sizeof(http_ota_info));
        ret = -RT_ENOMEM;
        goto __exit;
    }
    
    strref_str_cpy(ota_info->url, sizeof(ota_info->url), url);
    memcpy(ota_info->md5, md5, sizeof(ota_info->md5));
    strref_str_cpy(ota_info->req_id, sizeof(ota_info->req_id), req_id);
    strref_str_cpy(ota_info->version, sizeof(ota_info->version), version);
    ota_info->firmware_size = firmware_size;
    
    /* 创建HTTP OTA线程 */
    http_ota_thread = rt_thread_create("http_ota", http_ota_thread_proc, 
        (void*)ota_info, HTTP_OTA_THREAD_STACK_SIZE, HTTP_OTA_THREAD_PRIORITY, 10);
    if (http_ota_thread == RT_NULL)
    {
        LOG_E("%s rt_thread_create(http_ota) failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    /* 启动HTTP OTA线程 */
    ret = rt_thread_startup(http_ota_thread);
    if (ret != RT_EOK)
    {
        LOG_E("%s rt_thread_startup() failed(%d)!", __FUNCTION__, ret);
    }
    
__exit:
    if (ret != RT_EOK)
    {
        if (ota_info != RT_NULL)
        {
            rt_free(ota_info);
        }
        if (http_ota_thread != RT_NULL)
        {
            rt_thread_delete(http_ota_thread);
            http_ota_thread = RT_NULL;
        }
    }
    
    rt_mutex_release(http_ota_mutex);
    
    return ret;
}

static void topic_upgrade_update_handler(mqtt_client *client, message_data *msg)
{
    (void) client;
    c_str_ref productkey = {0, NULL}; // productKey
    c_str_ref devicecode = {0, NULL}; // deviceCode
    c_str_ref operationdate = {0, NULL}; // operationDate
    c_str_ref id = {0, NULL}; // id
    c_str_ref code = {0, NULL}; // code
    c_str_ref message = {0, NULL}; // message
    c_str_ref data = {0, NULL}; // data
    c_str_ref size = {0, NULL}; // size
    c_str_ref version = {0, NULL}; // version
    c_str_ref url = {0, NULL}; // url
    c_str_ref md5 = {0, NULL}; // md5
    c_str_ref sign = {0, NULL}; // sign
    c_str_ref signmethod = {0, NULL}; // signMethod
    int i = 0;
    
    LOG_I("%s() %s %s", __FUNCTION__, msg->topic_name->cstring, msg->message->payload);
    
    /* 解析JSON字符串 */
    {
        const char *json_str = (const char*)(msg->message->payload);
        size_t json_str_len = msg->message->payloadlen;
        jsmn_parser json_paser;
        jsmn_init(&json_paser);
        jsmntok_t tok_list[32];
        int list_len = jsmn_parse(&json_paser, json_str, json_str_len, tok_list, ARRAY_SIZE(tok_list));
        if (list_len == JSMN_ERROR_PART)
        {
            LOG_E("%s jsmn_parse failed!", __FUNCTION__);
            goto __exit;
        }
        
        for (i = 0; i < list_len; ++i)
        {
            const char *token_str = json_str + tok_list[i].start;
            int token_str_len = tok_list[i].end - tok_list[i].start;
            
            if (JSMN_STRING == tok_list[i].type)
            {
                if ((token_str_len == STR_LEN("productKey")) 
                    && (0 == memcmp(token_str, "productKey", token_str_len)))
                {
                    ++i; // 指向Value
                    RT_ASSERT(i < list_len);
                    productkey.c_str = json_str + tok_list[i].start;
                    productkey.len = tok_list[i].end - tok_list[i].start;
                }
                else if ((token_str_len == STR_LEN("deviceCode")) 
                    && (0 == memcmp(token_str, "deviceCode", token_str_len)))
                {
                    ++i; // 指向Value
                    RT_ASSERT(i < list_len);
                    devicecode.c_str = json_str + tok_list[i].start;
                    devicecode.len = tok_list[i].end - tok_list[i].start;
                }
                else if ((token_str_len == STR_LEN("operationDate")) 
                    && (0 == memcmp(token_str, "operationDate", token_str_len)))
                {
                    ++i; // 指向Value
                    RT_ASSERT(i < list_len);
                    operationdate.c_str = json_str + tok_list[i].start;
                    operationdate.len = tok_list[i].end - tok_list[i].start;
                }
                else if ((token_str_len == STR_LEN("id")) 
                    && (0 == memcmp(token_str, "id", token_str_len)))
                {
                    ++i; // 指向Value
                    RT_ASSERT(i < list_len);
                    id.c_str = json_str + tok_list[i].start;
                    id.len = tok_list[i].end - tok_list[i].start;
                }
                else if ((token_str_len == STR_LEN("code")) 
                    && (0 == memcmp(token_str, "code", token_str_len)))
                {
                    ++i; // 指向Value
                    RT_ASSERT(i < list_len);
                    code.c_str = json_str + tok_list[i].start;
                    code.len = tok_list[i].end - tok_list[i].start;
                }
                else if ((token_str_len == STR_LEN("message")) 
                    && (0 == memcmp(token_str, "message", token_str_len)))
                {
                    ++i; // 指向Value
                    RT_ASSERT(i < list_len);
                    message.c_str = json_str + tok_list[i].start;
                    message.len = tok_list[i].end - tok_list[i].start;
                }
                else if ((token_str_len == STR_LEN("data")) 
                    && (0 == memcmp(token_str, "data", token_str_len)))
                {
                    ++i; // 指向Value
                    RT_ASSERT(i < list_len);
                    data.c_str = json_str + tok_list[i].start;
                    data.len = tok_list[i].end - tok_list[i].start;
                }
            }
        }
    }
    
    /* 检查参数有效性 */
    if (strref_str_cmp(get_productkey(), &productkey) != 0)
    {
        LOG_E("%s productkey check failed!", __FUNCTION__);
        goto __exit;
    }
    if (strref_str_cmp(get_devicecode(), &devicecode) != 0)
    {
        LOG_E("%s devicecode check failed!", __FUNCTION__);
        goto __exit;
    }
    if (strref_is_empty(&id))
    {
        LOG_E("%s id is empty!", __FUNCTION__);
        goto __exit;
    }
    if (strref_str_cmp("1000", &code) != 0)
    {
        LOG_E("%s code is not 1000!", __FUNCTION__);
        goto __exit;
    }
    if (strref_is_empty(&data))
    {
        LOG_E("%s data is empty!", __FUNCTION__);
        goto __exit;
    }
    
    /* 解析data */
    {
        const char *json_str = data.c_str;
        size_t json_str_len = data.len;
        jsmn_parser json_paser;
        jsmn_init(&json_paser);
        jsmntok_t tok_list[32];
        int list_len = jsmn_parse(&json_paser, json_str, json_str_len, tok_list, ARRAY_SIZE(tok_list));
        if (list_len == JSMN_ERROR_PART)
        {
            LOG_E("%s jsmn_parse failed!", __FUNCTION__);
            goto __exit;
        }
        
        for (i = 0; i < list_len; ++i)
        {
            const char *token_str = json_str + tok_list[i].start;
            int token_str_len = tok_list[i].end - tok_list[i].start;
            
            if (JSMN_STRING == tok_list[i].type)
            {
                if ((token_str_len == STR_LEN("size")) 
                    && (0 == memcmp(token_str, "size", token_str_len)))
                {
                    ++i; // 指向Value
                    RT_ASSERT(i < list_len);
                    size.c_str = json_str + tok_list[i].start;
                    size.len = tok_list[i].end - tok_list[i].start;
                }
                else if ((token_str_len == STR_LEN("version")) 
                    && (0 == memcmp(token_str, "version", token_str_len)))
                {
                    ++i; // 指向Value
                    RT_ASSERT(i < list_len);
                    version.c_str = json_str + tok_list[i].start;
                    version.len = tok_list[i].end - tok_list[i].start;
                }
                else if ((token_str_len == STR_LEN("url")) 
                    && (0 == memcmp(token_str, "url", token_str_len)))
                {
                    ++i; // 指向Value
                    RT_ASSERT(i < list_len);
                    url.c_str = json_str + tok_list[i].start;
                    url.len = tok_list[i].end - tok_list[i].start;
                }
                else if ((token_str_len == STR_LEN("md5")) 
                    && (0 == memcmp(token_str, "md5", token_str_len)))
                {
                    ++i; // 指向Value
                    RT_ASSERT(i < list_len);
                    md5.c_str = json_str + tok_list[i].start;
                    md5.len = tok_list[i].end - tok_list[i].start;
                }
                else if ((token_str_len == STR_LEN("sign")) 
                    && (0 == memcmp(token_str, "sign", token_str_len)))
                {
                    ++i; // 指向Value
                    RT_ASSERT(i < list_len);
                    sign.c_str = json_str + tok_list[i].start;
                    sign.len = tok_list[i].end - tok_list[i].start;
                }
                else if ((token_str_len == STR_LEN("signMethod")) 
                    && (0 == memcmp(token_str, "signMethod", token_str_len)))
                {
                    ++i; // 指向Value
                    RT_ASSERT(i < list_len);
                    signmethod.c_str = json_str + tok_list[i].start;
                    signmethod.len = tok_list[i].end - tok_list[i].start;
                }
            }
        }
    }
    
    /* 检查参数有效性 */
    if (strref_is_empty(&size))
    {
        LOG_E("%s size is empty!", __FUNCTION__);
        goto __exit;
    }
    if (strref_is_empty(&version))
    {
        LOG_E("%s version is empty!", __FUNCTION__);
        goto __exit;
    }
    if (strref_str_cmp(SW_VERSION, &version) == 0)
    {
        LOG_E("%s version(%.*s) was not change!", __FUNCTION__, version.len, version.c_str);
        goto __exit;
    }
    if (strref_is_empty(&url))
    {
        LOG_E("%s url is empty!", __FUNCTION__);
        goto __exit;
    }
    if (strref_is_empty(&md5))
    {
        LOG_E("%s md5 is empty!", __FUNCTION__);
        goto __exit;
    }
    if (md5.len != 32)
    {
        LOG_E("%s md5(len=%u) != 32!", __FUNCTION__, md5.len);
        goto __exit;
    }
    if (!strref_is_int(&size))
    {
        LOG_E("%s <size> not interger!", __FUNCTION__);
        goto __exit;
    }
    
    /* 请求执行固件升级并上报升级进度 */
    {   
        int firmware_size = strref_to_int(&size);
        if ((firmware_size < 0) || (firmware_size > (384 * 1024)))
        {
            LOG_E("%s <size>(%d) invalid!", __FUNCTION__, firmware_size);
            goto __exit;
        }
        
        uint8_t md5_bytes[16] = {0x00};
        util_from_hex_str(md5.c_str, md5.len, md5_bytes, sizeof(md5_bytes));
        
        /* 请求执行固件下载和升级(独立线程执行,回调函数不能阻塞) */
        rt_err_t ret = do_http_ota_request(&url, md5_bytes, &id, &version, firmware_size);
        if (ret != RT_EOK)
        {
            LOG_E("%s do_http_ota_request() failed(%d)!", __FUNCTION__, ret);
            
            send_upgrade_progress(&id, -3); // 直接上报升级失败
        }
    }
    
__exit:
    return;
}

static void app_deinit()
{
    LOG_D("%s()", __FUNCTION__);
    
    if (RT_NULL != history_fifo_mutex)
    {
        rt_mutex_delete(history_fifo_mutex);
        history_fifo_mutex = RT_NULL;
    }
    if (RT_NULL != app_msg_queue)
    {
        rt_mq_delete(app_msg_queue);
        app_msg_queue = RT_NULL;
    }
    if (RT_NULL != report_timer)
    {
        rt_timer_delete(report_timer);
        report_timer = RT_NULL;
    }
    if (RT_NULL != acquisition_timer)
    {
        rt_timer_delete(acquisition_timer);
        acquisition_timer = RT_NULL;
    }
    if (RT_NULL != http_ota_mutex)
    {
        rt_mutex_delete(http_ota_mutex);
        http_ota_mutex = RT_NULL;
    }
}

static rt_err_t app_init()
{
    LOG_D("%s()", __FUNCTION__);
    config_info *cfg = NULL;
    rt_err_t ret = RT_EOK;
    
    /* 输出软/硬件版本号 */
    LOG_D("sw_version: %s", __FUNCTION__, SW_VERSION);
    LOG_D("hw_version: %s", __FUNCTION__, HW_VERSION);
    
    /* fal init */
    {
        int iret = fal_init();
        if (iret < 0)
        {
            LOG_E("%s fal init error(%d)!", __FUNCTION__, iret);
            ret = -RT_ERROR;
            goto __exit;
        }
    }
    
    /* easyflash init */
    {
        EfErrCode ef_err = easyflash_init();
        if (ef_err != EF_NO_ERR)
        {
            LOG_E("%s easyflash init error(%d)!", __FUNCTION__, ef_err);
            ret = -RT_ERROR;
            goto __exit;
        }
    }
    
    /* 配置项加载到内存 */
    {
        ret = cfg_load();
        if (!ret)
        {
            LOG_E("%s cfg_load error!", __FUNCTION__);
            ret = -RT_ERROR;
            goto __exit;
        }
        /* LOG输出所有加载的配置信息 */
        cfg_print();
    }
    
    /* 取得缓存的配置信息 */
    cfg = cfg_get();
    
    /* 根据保存的配置来设置ULOG全局日志level */
    {
        ulog_global_filter_lvl_set(cfg->ulog_glb_lvl);
    }
    
    /* create history fifo mutex */
    history_fifo_mutex = rt_mutex_create("history_fifo", RT_IPC_FLAG_FIFO);
    if (RT_NULL == history_fifo_mutex)
    {
        LOG_E("%s create history fifo mutex failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    /* 创建主循环消息队列 */
    app_msg_queue = rt_mq_create("app_mq", sizeof(app_message), APP_MSG_QUEUE_LEN, RT_IPC_FLAG_FIFO);
    if (RT_NULL == app_msg_queue)
    {
        LOG_E("%s create app message queue failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    /* create report timer */
    report_timer = rt_timer_create("report_timer", report_timer_timeout, 
                            RT_NULL, rt_tick_from_millisecond(cfg->cycle * 60 * 1000), 
                            RT_TIMER_FLAG_PERIODIC);
    if (RT_NULL == report_timer)
    {
        LOG_E("%s create report timer failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    /* create acquisition timer */
    acquisition_timer = rt_timer_create("acquisition_timer", acquisition_timer_timeout, 
                            RT_NULL, rt_tick_from_millisecond(cfg->acquisition * 60 * 1000), 
                            RT_TIMER_FLAG_PERIODIC);
    if (RT_NULL == acquisition_timer)
    {
        LOG_E("%s create acquisition timer failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    /* 创建HTTP OTA互斥锁 */
    http_ota_mutex = rt_mutex_create("http_ota", RT_IPC_FLAG_FIFO);
    if (RT_NULL == http_ota_mutex)
    {
        LOG_E("%s create http_ota_mutex failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    /* 初始化MQTT连接参数 */
    {
        MQTTPacket_connectData condata = MQTTPacket_connectData_initializer;
        
        mq_client.isconnected = 0;
        
        /* malloc buffer. */
        mq_client.buf_size = 1024;
        mq_client.buf = rt_calloc(1, mq_client.buf_size);
        if (mq_client.buf == NULL)
        {
            LOG_E("%s no memory for MQTT client buffer!", __FUNCTION__);
            ret = -RT_ENOMEM;
            goto __exit;
        }
        mq_client.readbuf_size = 1024;
        mq_client.readbuf = rt_calloc(1, mq_client.readbuf_size);
        if (mq_client.readbuf == NULL)
        {
            LOG_E("%s no memory for MQTT client read buffer!", __FUNCTION__);
            ret = -RT_ENOMEM;
            goto __exit;
        }
        
        /* 设置服务器地址和端口 */
        {
#ifndef TEST_NEW_FETURE
            char addr[32] = "";
            inet_ntoa_r(cfg->a_ip, addr, sizeof(addr));
            rt_snprintf(mqtt_server_url, sizeof(mqtt_server_url), "tcp://%s:%u", addr, cfg->a_port);
#else
            rt_snprintf(mqtt_server_url, sizeof(mqtt_server_url), "tcp://mq.tongxinmao.com:18830");
			//rt_snprintf(mqtt_server_url, sizeof(mqtt_server_url), "tcp://47.103.22.229:1883");
#endif
            mq_client.uri = mqtt_server_url;
            LOG_D("%s mqtt_server_url %s", __FUNCTION__, mqtt_server_url);
        }
        
        /* generate the random client ID */
        rt_snprintf(mqtt_client_id, sizeof(mqtt_client_id), "rtu_%d", rt_tick_get());
        //rt_snprintf(mqtt_client_id, sizeof(mqtt_client_id), "%010u", get_clientid());
        
        /* config connect param */
        memcpy(&mq_client.condata, &condata, sizeof(condata));
        mq_client.condata.clientID.cstring = mqtt_client_id;
        //mq_client.condata.username.cstring = "";
        //mq_client.condata.password.cstring = "";
        mq_client.condata.keepAliveInterval = 120;
        mq_client.condata.cleansession = 1;

        /* config MQTT will param. */
        //mq_client.condata.willFlag = 1;
        //mq_client.condata.will.qos = 1;
        //mq_client.condata.will.retained = 0;
        //mq_client.condata.will.topicName.cstring = MQTT_PUBTOPIC;
        //mq_client.condata.will.message.cstring = MQTT_WILLMSG;

        /* set event callback function */
        mq_client.connect_callback = mqtt_connect_callback;
        mq_client.online_callback = mqtt_online_callback;
        mq_client.offline_callback = mqtt_offline_callback;

        /* set subscribe table and event callback */
        
        /* 订阅/sys/${productKey}/${deviceCode}/telemetry/get主题,接收服务器数据查询指令 */
        rt_snprintf(topic_telemetry_get, sizeof(topic_telemetry_get), "/sys/%s/%s/telemetry/get", get_productkey(), get_devicecode());
        LOG_D("subscribe %s", topic_telemetry_get);
        mq_client.message_handlers[0].topicFilter = topic_telemetry_get;
        mq_client.message_handlers[0].callback = topic_telemetry_get_handler;
        mq_client.message_handlers[0].qos = QOS0;
        
        /* 订阅/sys/${productKey}/${deviceCode}/config/get主题,接收服务器配置查询指令 */
        rt_snprintf(topic_config_get, sizeof(topic_config_get), "/sys/%s/%s/config/get", get_productkey(), get_devicecode());
        LOG_D("subscribe %s", topic_config_get);
        mq_client.message_handlers[1].topicFilter = topic_config_get;
        mq_client.message_handlers[1].callback = topic_config_get_handler;
        mq_client.message_handlers[1].qos = QOS0;
        
        /* 订阅/sys/${productKey}/${deviceCode}/config/set主题,接收服务器配置设置指令 */
        rt_snprintf(topic_config_set, sizeof(topic_config_set), "/sys/%s/%s/config/set", get_productkey(), get_devicecode());
        LOG_D("subscribe %s", topic_config_set);
        mq_client.message_handlers[2].topicFilter = topic_config_set;
        mq_client.message_handlers[2].callback = topic_config_set_handler;
        mq_client.message_handlers[2].qos = QOS0;
        
        /* 订阅/sys/${productKey}/${deviceCode}/telemetry/result主题,接收主动上报服务器响应 */
        rt_snprintf(topic_telemetry_result, sizeof(topic_telemetry_result), "/sys/%s/%s/telemetry/result", get_productkey(), get_devicecode());
        LOG_D("subscribe %s", topic_telemetry_result);
        mq_client.message_handlers[3].topicFilter = topic_telemetry_result;
        mq_client.message_handlers[3].callback = topic_telemetry_result_handler;
        mq_client.message_handlers[3].qos = QOS0;
        
        /* 订阅/sys/${productKey}/${deviceCode}/upgrade/update主题,接收主动上报服务器响应 */
        rt_snprintf(topic_upgrade_update, sizeof(topic_upgrade_update), "/sys/%s/%s/upgrade/update", get_productkey(), get_devicecode());
        LOG_D("subscribe %s", topic_upgrade_update);
        mq_client.message_handlers[4].topicFilter = topic_upgrade_update;
        mq_client.message_handlers[4].callback = topic_upgrade_update_handler;
        mq_client.message_handlers[4].qos = QOS0;

        /* set default subscribe event callback */
        mq_client.default_message_handlers = mqtt_sub_default_callback;
        
        /* 设置其他MQTT参数初值 */
        mq_client.keepalive_interval = 120; // keepalive间隔，以秒为单位
        mq_client.keepalive_count = 3; // keepalive次数，超过该次数无应答，则关闭连接
        mq_client.connect_timeout = 60; // 连接超时，以秒为单位
        mq_client.reconnect_interval = 5; // 重新连接间隔，以秒为单位
        mq_client.msg_timeout = 60; // 消息通信超时，以秒为单位，根据网络情况，不能为0
    }
    
    /* 监听网络就绪事件 */
    {
        struct netdev *net_dev = netdev_get_by_name(TB22_DEVICE_NAME);
        if (RT_NULL == net_dev)
        {
            LOG_E("%s get net device(%s) failed!", __FUNCTION__, TB22_DEVICE_NAME);
            ret = -RT_ERROR;
            goto __exit;
        }
        netdev_set_status_callback(net_dev, tbss_netdev_status_callback);
    }
	
	/* 初始化RS485 RTS GPIO */
	rt_pin_mode(RS485_1_REN_PIN, PIN_MODE_OUTPUT);
	rt_pin_mode(RS485_2_REN_PIN, PIN_MODE_OUTPUT);
	rt_pin_mode(RS485_3_REN_PIN, PIN_MODE_OUTPUT);
	rt_pin_mode(RS485_4_REN_PIN, PIN_MODE_OUTPUT);
    
    /* 成功完成初始化 */
    ret = RT_EOK;
    
__exit:
    
    if (ret != RT_EOK)
    {
        if (RT_NULL != mq_client.buf)
        {
            rt_free(mq_client.buf);
            mq_client.buf = RT_NULL;
        }
        if (RT_NULL != mq_client.readbuf)
        {
            rt_free(mq_client.readbuf);
            mq_client.readbuf = RT_NULL;
        }
        memset(&mq_client, 0, sizeof(mq_client));
        
        if (RT_NULL != history_fifo_mutex)
        {
            rt_mutex_delete(history_fifo_mutex);
            history_fifo_mutex = RT_NULL;
        }
        if (RT_NULL != app_msg_queue)
        {
            rt_mq_delete(app_msg_queue);
            app_msg_queue = RT_NULL;
        }
        if (RT_NULL != report_timer)
        {
            rt_timer_delete(report_timer);
            report_timer = RT_NULL;
        }
        if (RT_NULL != acquisition_timer)
        {
            rt_timer_delete(acquisition_timer);
            acquisition_timer = RT_NULL;
        }
        if (RT_NULL != http_ota_mutex)
        {
            rt_mutex_delete(http_ota_mutex);
            http_ota_mutex = RT_NULL;
        }
    }
    
    return ret;
}

/* 启动MQTT客户端 */
static rt_err_t mqtt_client_start(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    int ret = paho_mqtt_start(&mq_client, MQTT_CLIENT_THREAD_STACK_SIZE, MQTT_CLIENT_THREAD_PRIORITY);
    if (ret != PAHO_SUCCESS)
    {
       LOG_E("%s paho_mqtt_start() error(%d)", __FUNCTION__, ret);
       return -RT_ERROR;
    }
    
    return RT_EOK;
}

/* 停止MQTT客户端 */
static rt_err_t mqtt_client_stop(rt_int32_t timeout)
{
    LOG_D("%s() timeout=%d", __FUNCTION__, timeout);
    
    int ret = paho_mqtt_stop(&mq_client, timeout);
    if (ret != PAHO_SUCCESS)
    {
       LOG_E("%s paho_mqtt_stop() error(%d)", __FUNCTION__, ret);
       return -RT_ERROR;
    }
    
    return RT_EOK;
}

/* 上报数据 */
static rt_err_t app_data_report(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    rt_err_t ret = RT_EOK;

    char* json_data_buf = (char*)rt_malloc(JSON_DATA_BUF_LEN);
    if (json_data_buf == NULL)
    {
        LOG_E("%s rt_malloc(%d) failed!", __FUNCTION__, JSON_DATA_BUF_LEN);
        ret = -RT_ENOMEM;
        goto __exit;
    }
    
    /* 编码JSON采集数据并发送 */
    {
		time_t time_stamp = time(RT_NULL); // 上报时间戳
        rt_int32_t json_data_len = rt_snprintf(json_data_buf, JSON_DATA_BUF_LEN, 
            "{\"productKey\":\"%s\",\"deviceCode\":\"%s\",\"clientId\":\"%010u\","
			"\"timeStamp\":\"%u\",\"itemId\":\"%s\",\"data\":", get_productkey(), get_devicecode(), 
			get_clientid(), time_stamp, get_itemid());
        
        /* 读取最新采集的数据(JSON格式)  */
        uint32_t read_len = read_history_data_json(0, json_data_buf + json_data_len, JSON_DATA_BUF_LEN - json_data_len, false);
        if (read_len <= 0)
        {
            LOG_E("%s read_history_data_json(read_len=%d) failed!", __FUNCTION__, read_len);
            ret = -RT_ERROR;
            goto __exit;
        }
        json_data_len += read_len;
        RT_ASSERT(json_data_len < JSON_DATA_BUF_LEN);
        json_data_buf[json_data_len++] = '}';
        RT_ASSERT(json_data_len < JSON_DATA_BUF_LEN);
        json_data_buf[json_data_len] = '\0';
        
        /* 发布/sys/${productKey}/${deviceCode}/telemetry/real_time_data */
        {
            char topic_buf[MQTT_TOPIC_BUF_LEN] = "";
            rt_snprintf(topic_buf, MQTT_TOPIC_BUF_LEN, "/sys/%s/%s/telemetry/real_time_data", get_productkey(), get_devicecode());
            LOG_D("%s publish(%s) %s", __FUNCTION__, topic_buf, json_data_buf);
            int mqtt_ret = paho_mqtt_publish(&mq_client, QOS1, topic_buf, json_data_buf, json_data_len);
            if (mqtt_ret != PAHO_SUCCESS)
            {
                LOG_E("%s mqtt publish(%s) error(%d)", __FUNCTION__, topic_buf, mqtt_ret);
                ret = -RT_ERROR;
                goto __exit;
            }
        }
    }
    
__exit:
    if (json_data_buf != NULL)
    {
        rt_free(json_data_buf);
        json_data_buf = NULL;
    }
    
    return ret;
}

/* 请求采集数据 */

rt_err_t req_data_acquisition(void)
{
	LOG_D("%s()", __FUNCTION__);
	
	return app_send_msg(APP_MSG_DATA_ACQUISITION_REQ, RT_NULL); // 发送数据采集请求
}

/* 请求上报数据 */

rt_err_t req_data_report(void)
{
	LOG_D("%s()", __FUNCTION__);
    
    /* 检查是否已连接到MQTT服务器 */
    if (!paho_mqtt_is_connected(&mq_client))
    {
        LOG_E("%s mqtt is not connect!", __FUNCTION__);
        return -RT_ERROR;
    }
	
	return app_send_msg(APP_MSG_DATA_REPORT_REQ, RT_NULL); // 发送数据上报请求
}

int main(void)
{
    /* 初始化APP */
    rt_err_t ret = app_init();
    if (ret != RT_EOK)
    {
        LOG_E("%s app_init error(%d)!", __FUNCTION__, ret);
        goto __exit;
    }
    
    /* 主消息循环 */
    while (1)
    {
        app_message app_msg = {APP_MSG_NONE};
        ret = rt_mq_recv(app_msg_queue, &app_msg, sizeof(app_msg), RT_WAITING_FOREVER);
        if (RT_EOK != ret)
        { // 出现严重错误
            LOG_E("%s recv app msg failed(%d)!", __FUNCTION__, ret);
            goto __exit;
        }
        
        switch (app_msg.msg)
        {
            case APP_MSG_MQTT_CLIENT_START_REQ: // MQTT客户端启动请求
            {
                /* 等待一段时间 */
                rt_thread_delay(rt_tick_from_millisecond(5 * 1000));
                
                /* 启动MQTT客户端 */
                ret = mqtt_client_start();
                if (ret != RT_EOK)
                { // 出现严重错误
                    LOG_E("%s mqtt_client_start failed(%d)!", __FUNCTION__, ret);
                    goto __exit;
                }
                break;
            }
            case APP_MSG_DATA_REPORT_REQ: // 数据主动上报请求
            {
                /* 检查是否已连接到MQTT服务器 */
                if (!paho_mqtt_is_connected(&mq_client))
                {
                    LOG_W("%s mqtt is not connect!", __FUNCTION__);
                    break;
                }
                
                /* 主动上报数据 */
                app_data_report();
                break;
            }
            case APP_MSG_DATA_ACQUISITION_REQ: // 数据采集请求
            {
                /* 采集并保存数据 */
                data_acquisition_and_save();
                break;
            }
            case APP_MSG_MQTT_PUBLISH_REQ: // MQTT发布数据请求
            {
                mqtt_publish_data_info *pub_data_info  = 
                    (mqtt_publish_data_info*)app_msg.msg_data;
                if (pub_data_info == RT_NULL)
                {
                    LOG_E("%s recv mqtt_publish_data_info is empty!", __FUNCTION__);
                    break;
                }
                
                if ((pub_data_info->topic != RT_NULL) 
                    && (pub_data_info->payload != RT_NULL)
                    && (pub_data_info->length > 0))
                {
                    /* 发布MQTT数据 */
                    int mqtt_ret = paho_mqtt_publish(&mq_client, QOS1, pub_data_info->topic, 
                        pub_data_info->payload, pub_data_info->length);
                    if (mqtt_ret != PAHO_SUCCESS)
                    {
                        LOG_E("%s mqtt publish(%s) error(%d)!", __FUNCTION__, pub_data_info->topic, ret);
                    }
                }
                else
                {
                    LOG_E("%s mqtt publish topic or data is empty!", __FUNCTION__);
                }
                
                /* 释放内存 */
                if (pub_data_info->topic != RT_NULL)
                {
                    rt_free(pub_data_info->topic);
                }
                if (pub_data_info->payload != RT_NULL)
                {
                    rt_free(pub_data_info->payload);
                }
                rt_free(app_msg.msg_data);
                break;
            }
            case APP_MSG_MQTT_CLIENT_ONLINE: // MQTT客户端已上线
            {
                /* 检查是否成功进行了OTA,并上报进度信息 */
                check_and_report_ota_process();
                
                /* 启动定时上报 */
                rt_err_t ret = rt_timer_start(report_timer);
                if (RT_EOK != ret)
                {
                    LOG_E("%s start report timer failed(%d)!", __FUNCTION__, ret);
                }
                break;
            }
            case APP_MSG_MQTT_CLIENT_OFFLINE: // MQTT客户端已下线
            {
                /* 停止定时上报 */
                rt_err_t ret = rt_timer_stop(report_timer);
                if (RT_EOK != ret)
                {
                    LOG_E("%s stop report timer failed(%d)!", __FUNCTION__, ret);
                }
                break;
            }
            case APP_MSG_HTTP_OTA_THREAD_QUIT: // HTTP OTA线程已退出
            {
                rt_mutex_take(http_ota_mutex, RT_WAITING_FOREVER);
                
                /* 释放资源 */
                rt_thread_delete(http_ota_thread);
                http_ota_thread = RT_NULL;
                
                rt_mutex_release(http_ota_mutex);
                break;
            }
            default:
            {
                LOG_W("%s recv unknown msg(%u)!", __FUNCTION__, app_msg.msg);
                break;
            }
        }
    }
    
__exit:
    
    LOG_D("main exit");
    
    /* 停止MQTT客户端 */
    mqtt_client_stop(2000);
    
    /* 逆初始化 */
    app_deinit();
    
    /* 重启系统 */
    rt_hw_cpu_reset();
    
    return ret;
}

/**
 * Function    ota_app_vtor_reconfig
 * Description Set Vector Table base location to the start addr of app(RT_APP_PART_ADDR).
*/
static int ota_app_vtor_reconfig(void)
{
    #define NVIC_VTOR_MASK   0x3FFFFF80
    /* Set the Vector Table base location by user application firmware definition */
    SCB->VTOR = RT_APP_PART_ADDR & NVIC_VTOR_MASK;

    return 0;
}
INIT_BOARD_EXPORT(ota_app_vtor_reconfig);
