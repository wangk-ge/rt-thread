/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-11-06     SummerGift   first version
 * 2018-11-19     flybreak     add stm32f429-fire-challenger bsp
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
#include <mqttclient.h>
#include <errno.h>
#include "config.h"
#include "common.h"

#define LOG_TAG              "main"
#define LOG_LVL              LOG_LVL_DBG
#include <rtdbg.h>

/* event flag */
#define APP_EVENT_MQTT_CONNECT_REQ 0x00000001 // MQTT连接请求
#define APP_EVENT_DATA_REPORT_REQ 0x00000002 // 数据上报请求
#define APP_EVENT_DATA_ACQUISITION_REQ 0x00000004 // 数据采集请求

/* RS485设备名 */
#define RS485_1_DEVICE_NAME "/dev/uart2"
#define RS485_2_DEVICE_NAME "/dev/uart6"
#define RS485_3_DEVICE_NAME "/dev/uart7"
#define RS485_4_DEVICE_NAME "/dev/uart8"

/* RS485接收使能GPIO引脚 */
#define RS485_1_REN_PIN GET_PIN(E, 10) // PE10(高电平发送,低电平接收)
#define RS485_2_REN_PIN GET_PIN(E, 11) // PE11(高电平发送,低电平接收)
#define RS485_3_REN_PIN GET_PIN(E, 12) // PE12(高电平发送,低电平接收)
#define RS485_4_REN_PIN GET_PIN(E, 13) // PE13(高电平发送,低电平接收)

/* UARTX每条采集的数据大小(字节数) */
static uint32_t uart_x_data_len[4] = {0};

/* event for application */
static rt_event_t app_event = RT_NULL;

/* 定时上报定时器 */
static rt_timer_t report_timer = RT_NULL;
/* 定时采集定时器 */
static rt_timer_t acquisition_timer = RT_NULL;

/* mqtt client */
static mqtt_client_t mq_client;
static client_init_params_t mq_init_params;
static char mqtt_client_id[16] = "";

static rt_err_t app_send_event(rt_uint32_t event_set)
{
    rt_err_t ret = rt_event_send(app_event, event_set);
    if (ret != RT_EOK)
    {
        LOG_E("app_send_event() call rt_event_send error(%d)", ret);
    }
    return ret;
}

static void topic_rtu_test_handler(void* client, message_data_t* msg)
{
    (void) client;
    LOG_I("-----------------------------------------------------------------------------------");
    LOG_I("%s:%d %s()...\ntopic: %s\nmessage:%s", __FILE__, __LINE__, __FUNCTION__, msg->topic_name, (char*)msg->message->payload);
    LOG_I("-----------------------------------------------------------------------------------");
}

static void tbss_netdev_status_callback(struct netdev *netdev, enum netdev_cb_type type)
{
    LOG_D("tbss_netdev_status_callback(%d)", type);
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
            app_send_event(APP_EVENT_MQTT_CONNECT_REQ); // 发送MQTT连接请求
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
    LOG_D("report_timer_timeout()");
    
    app_send_event(APP_EVENT_DATA_REPORT_REQ); // 发送数据上报请求
}

static void acquisition_timer_timeout(void *parameter)
{
    LOG_D("acquisition_timer_timeout()");
    
    app_send_event(APP_EVENT_DATA_ACQUISITION_REQ); // 发送数据采集请求
}

static void app_deinit()
{
    LOG_D("app_deinit()");
    
    mqtt_release(&mq_client);
    
    if (RT_NULL != app_event)
    {
        rt_event_delete(app_event);
        app_event = RT_NULL;
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
}

/* 统计UARTX每条采集的数据大小(字节数) 
 *
 *   数据格式: "采集时间(YYMMDDhhmmss),变量名1:读出数据的HEX格式字符串1,变量名2:读出数据的HEX格式字符串2,...\0"
 *
 */
static uint32_t calc_uart_x_data_len(int x)
{
    int index = x - 1;
    config_info *cfg = cfg_get();

    uint32_t data_len = STR_LEN("YYMMDDhhmmss") + 1; // 采集时间(YYMMDDhhmmss)+分隔符','
    uint8_t variablecnt = cfg->uart_x_cfg[index].variablecnt; // 变量个数
    int i = 0;
    for (i = 0; i < variablecnt; ++i)
    {
        char* variable = cfg->uart_x_cfg[index].variable[i]; // 变量名
        uint16_t length = cfg->uart_x_cfg[index].length[i]; // 寄存器个数(每个2字节,转换成HEX字符串4字节)
        
        data_len += rt_strlen(variable) + 1 + length * 2 * 2 + 1; // 变量名长度+分隔符':'+数据HEX字符串长度+分隔符','
    }
    
    return data_len;
}

static rt_err_t app_init()
{
    LOG_D("app_init()");
    config_info *cfg = NULL;
    rt_err_t ret = RT_EOK;
    
    /* fal init */
    {
        int iret = fal_init();
        if (iret < 0)
        {
            LOG_E("fal init error(%d)!", iret);
            ret = -RT_ERROR;
            goto __exit;
        }
    }
    
    /* easyflash init */
    {
        EfErrCode ef_err = easyflash_init();
        if (ef_err != EF_NO_ERR)
        {
            LOG_E("easyflash init error(%d)!", ef_err);
            ret = -RT_ERROR;
            goto __exit;
        }
    }
    
    /* 配置项加载到内存 */
    {
        bool ret = cfg_load();
        if (!ret)
        {
            //LOG_E("cfg_load error!");
            ret = -RT_ERROR;
            goto __exit;
        }
        /* LOG输出所有加载的配置信息 */
        cfg_print();
    }
    
    /* 取得缓存的配置信息 */
    cfg = cfg_get();
    
    /* 统计UARTX每条采集的数据大小(字节数) */
    {
        int i = 0;
        for (i = 0; i < ARRAY_SIZE(uart_x_data_len); ++i)
        {
            int x = i + 1;
            uart_x_data_len[i] = calc_uart_x_data_len(x);
            LOG_I("uart_%d_data_len=%u", x, uart_x_data_len[i]);
        }
    }
    
    /* 根据保存的配置来设置ULOG全局日志level */
    {
        ulog_global_filter_lvl_set(cfg->ulog_glb_lvl);
    }
    
    /* create app event */
    app_event = rt_event_create("app_event", RT_IPC_FLAG_FIFO);
    if (RT_NULL == app_event)
    {
        LOG_E("create app event failed!");
        ret = -RT_ERROR;
        goto __exit;
    }
    
    /* create report timer */
    report_timer = rt_timer_create("report_timer", report_timer_timeout, 
                            RT_NULL, rt_tick_from_millisecond(cfg->cycle * 60 * 1000), 
                            RT_TIMER_FLAG_PERIODIC);
    if (RT_NULL == report_timer)
    {
        LOG_E("create report timer failed!");
        ret = -RT_ERROR;
        goto __exit;
    }
    
    /* create acquisition timer */
    acquisition_timer = rt_timer_create("acquisition_timer", acquisition_timer_timeout, 
                            RT_NULL, rt_tick_from_millisecond(cfg->acquisition * 60 * 1000), 
                            RT_TIMER_FLAG_PERIODIC);
    if (RT_NULL == acquisition_timer)
    {
        LOG_E("create acquisition timer failed!");
        ret = -RT_ERROR;
        goto __exit;
    }
    
    /* 初始化MQTT连接参数 */
    {
        mq_init_params.cmd_timeout = KAWAII_MQTT_MAX_CMD_TIMEOUT;
        mq_init_params.read_buf_size = 1024;
        mq_init_params.write_buf_size = 1024;
        mq_init_params.connect_params.network_params.port = "18830";
        mq_init_params.connect_params.network_params.addr = "mq.tongxinmao.com";
        mq_init_params.connect_params.user_name = RT_NULL;
        mq_init_params.connect_params.password = RT_NULL;
        /* generate the random client ID */
        //rt_snprintf(mqtt_client_id, sizeof(mqtt_client_id), "rtu_%d", rt_tick_get());
        rt_snprintf(mqtt_client_id, sizeof(mqtt_client_id), "%010u", cfg->client_id);
        mq_init_params.connect_params.client_id = mqtt_client_id;
        mq_init_params.connect_params.clean_session = 1;
            
        int iret = mqtt_init(&mq_client, &mq_init_params);
        if (iret != MQTT_SUCCESS_ERROR)
        {
            LOG_E("mqtt init error(%d)", ret);
            ret = -RT_ERROR;
            goto __exit;
        }
    }
    
    /* 监听网络就绪事件 */
    {
        struct netdev *net_dev = netdev_get_by_name(TB22_DEVICE_NAME);
        if (RT_NULL == net_dev)
        {
            LOG_E("get net device(%s) failed!", TB22_DEVICE_NAME);
            ret = -RT_ERROR;
            goto __exit_mq_init;
        }
        netdev_set_status_callback(net_dev, tbss_netdev_status_callback);
    }
    
    return ret;
    
__exit_mq_init:
    mqtt_release(&mq_client);
    
__exit:
    if (RT_NULL != app_event)
    {
        rt_event_delete(app_event);
        app_event = RT_NULL;
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
    
    return ret;
}

static rt_err_t app_mqtt_connect()
{
    LOG_D("app_mqtt_connect()");

    int ret = mqtt_connect(&mq_client);
    if (ret != MQTT_SUCCESS_ERROR)
    {
        LOG_E("mqtt connect error(%d)", ret);
        return -RT_ERROR;
    }
    
    ret = mqtt_subscribe(&mq_client, "/sys/rtu_test", QOS1, topic_rtu_test_handler);
    if (ret != MQTT_SUCCESS_ERROR)
    {
        mqtt_disconnect(&mq_client);
        
        LOG_E("mqtt subscribe error(%d)", ret);
        return -RT_ERROR;
    }
    
    return RT_EOK;
}

/* 上报数据 */
static rt_err_t app_data_report()
{
    LOG_D("app_data_report()");

    char buf[128] = "";
    sprintf(buf, "welcome to mqttclient, this is a publish test, a rand number: %d ...", random_number());
    
    mqtt_message_t msg;
    memset(&msg, 0, sizeof(msg));

    msg.qos = QOS1;
    msg.payload = (void*)buf;
    
    int ret = mqtt_publish(&mq_client, "/sys/rtu_test_pub", &msg);
    if (ret != MQTT_SUCCESS_ERROR)
    {
        LOG_E("mqtt publish error(%d)", ret);
        return -RT_ERROR;
    }
    
    return RT_EOK;
}

/* 采集UARTX数据 */
static char* app_data_acquisition(int x)
{
    LOG_D("app_data_acquisition(UART%d)", x);
    
    /* 内部函数不做参数检查,由调用者保证参数有效性 */
    int index = x - 1;
    
    modbus_t *mb_ctx = NULL;
    
    rt_err_t ret = RT_EOK;
    
    /* 校验位转换表(0=无'NONE',1=奇'EVEN',2=偶'ODD') */
    const char parity_name[] = {'N', 'E', 'O'};

    /* 配置信息 */
    config_info *cfg = cfg_get();
    
    /* 分配采集数据缓存 */
    char *acquisition_data = (char*)rt_malloc(uart_x_data_len[index]);
    if (acquisition_data == NULL)
    {
        LOG_E("rt_malloc(%u) error", uart_x_data_len[index]);
        goto __exit;
    }

    {
        /* 创建MODBUS RTU对象实例 */
        mb_ctx = modbus_new_rtu(RS485_1_DEVICE_NAME, 
            cfg->uart_x_cfg[index].baudrate, 
            parity_name[cfg->uart_x_cfg[index].parity], 
            cfg->uart_x_cfg[index].wordlength, 
            cfg->uart_x_cfg[index].stopbits);
        if (mb_ctx == NULL)
        {
            LOG_E("modbus_new_rtu error");
            ret = -RT_ERROR;
            goto __exit;
        }
        
        /* 配置MODBUS RTU属性 */
        modbus_rtu_set_serial_mode(mb_ctx, MODBUS_RTU_RS485);
        /* function shall set the Request To Send mode to communicate on a RS485 serial bus. */
        modbus_rtu_set_rts(mb_ctx, RS485_1_REN_PIN, MODBUS_RTU_RTS_UP);
        modbus_set_slave(mb_ctx, cfg->uart_x_cfg[index].slaveraddr); // 从机地址
        modbus_set_response_timeout(mb_ctx, 1, 0); // 超时时间:1S
        
        /* 连接MODBUS端口 */
        int iret = modbus_connect(mb_ctx);
        if (iret != 0)
        {
            LOG_E("modbus_connect error(%d)", errno);
            ret = -RT_ERROR;
            goto __exit;
        }
        
        /* acquisition_data中的数据长度 */
        int data_len = 0;
        
        /* 获取当前时间 */
        time_t now = time(RT_NULL);
        struct tm* local_time = localtime(&now);
        data_len = snprintf(acquisition_data, uart_x_data_len[index], "%02d%02d%02d%02d%02d%02d", 
            (local_time->tm_year + 1900) - 2000, local_time->tm_mon + 1, local_time->tm_mday,
            local_time->tm_hour, local_time->tm_min, local_time->tm_sec);
        
        int i = 0;
        uint8_t variablecnt = cfg->uart_x_cfg[index].variablecnt; // 变量个数
        for (i = 0; i < variablecnt; ++i)
        {
            char* variable = cfg->uart_x_cfg[index].variable[i]; // 变量名
            uint16_t startaddr = cfg->uart_x_cfg[index].startaddr[i]; // 寄存器地址
            uint16_t length = cfg->uart_x_cfg[index].length[i]; // 寄存器个数
            /* Reads the holding registers of remote device and put the data into an array */
            uint16_t read_buf[MODBUS_MAX_READ_REGISTERS] = {0}; // 读取数据缓冲区
            int read_bytes = modbus_read_registers(mb_ctx, startaddr, length, read_buf);
            
            /* 保存到采集数据缓存 */
            data_len += snprintf(acquisition_data + data_len, uart_x_data_len[index] - data_len, "%s:", variable);
            int j = 0;
            for (j = 0; j < length; ++j)
            {
                data_len += snprintf(acquisition_data + data_len, uart_x_data_len[index] - data_len, "%04x", read_buf[j]);
            }
            acquisition_data[data_len++] = ',';
        }
        if (data_len > 0)
        {
            acquisition_data[data_len - 1] = '\0'; // 最后一个','改为'\0'
        }
    }
    
__exit:
    /* 关闭MODBUS端口 */
    modbus_close(mb_ctx);
    
    /* 释放MODBUS对象实例 */
    modbus_free(mb_ctx);
    
    if (ret != RT_EOK)
    {
        free(acquisition_data);
        acquisition_data = NULL;
    }
    
    return acquisition_data;
}

/* 保存数据 */
static rt_err_t app_data_save(int x, const char* data)
{
    LOG_D("app_data_save(UART%d)", x);

    // TODO
    
    return RT_EOK;
}
    
int main(void)
{
    /* 初始化APP */
    rt_err_t ret = app_init();
    if (ret != RT_EOK)
    {
        return ret;
    }
    
    /* event loop */
    while (1)
    {
        rt_uint32_t event_recved = 0;
        ret = rt_event_recv(app_event, (APP_EVENT_MQTT_CONNECT_REQ | APP_EVENT_DATA_REPORT_REQ),
                          (RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR),
                          RT_WAITING_FOREVER, &event_recved);
        if (RT_EOK != ret)
        {
            LOG_E("recv event failed(%d)!", ret);
            break;
        }
        
        if (event_recved & APP_EVENT_MQTT_CONNECT_REQ)
        {
            ret = app_mqtt_connect();
            if (ret == RT_EOK)
            {
                /* 启动定时上报 */
                ret = rt_timer_start(report_timer);
                if (RT_EOK != ret)
                {
                    LOG_E("start report timer failed(%d)!", ret);
                    break;
                }
            }
            else
            {
                rt_thread_delay(rt_tick_from_millisecond(2 * 1000));
                /* 重连 */
                app_send_event(APP_EVENT_MQTT_CONNECT_REQ);
            }
        }
        
        if (event_recved & APP_EVENT_DATA_REPORT_REQ)
        {
            /* 上报数据 */
            app_data_report();
        }
        
        if (event_recved & APP_EVENT_DATA_ACQUISITION_REQ)
        {
            /* 采集4路UART数据 */
            char* data1 = app_data_acquisition(1); // UART1
            char* data2 = app_data_acquisition(2); // UART2
            char* data3 = app_data_acquisition(3); // UART3
            char* data4 = app_data_acquisition(4); // UART4
            
            LOG_I("data1: %s", data1);
            LOG_I("data2: %s", data2);
            LOG_I("data3: %s", data3);
            LOG_I("data4: %s", data4);
            
            rt_free(data1);
            rt_free(data2);
            rt_free(data3);
            rt_free(data4);
        }
    }
    
    app_deinit();
    
    return ret;
}
