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
#include <at.h>
#include <arpa/inet.h>
#include <netdev.h>
#include "mqttclient.h"

#define LOG_TAG              "main"
#define LOG_LVL              LOG_LVL_DBG
#include <rtdbg.h>

/* 定时上报周期(s) */
#define APP_MQTT_REPORT_INTERVAL (10)

/* event flag */
#define APP_EVENT_MQTT_CONNECT_REQ 0x00000001 // MQTT连接请求
#define APP_EVENT_MQTT_REPORT_REQ 0x00000002 // MQTT连接上报数据

/* event for application */
static rt_event_t app_event = RT_NULL;

/* 定时上报定时器 */
static rt_timer_t report_timer = RT_NULL;

/* mqtt client */
static mqtt_client_t mq_client;
static client_init_params_t mq_init_params;
static char mqtt_client_id[32] = "";

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
    
    app_send_event(APP_EVENT_MQTT_REPORT_REQ);
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
}

static rt_err_t app_init()
{
    LOG_D("app_init()");
    
    rt_err_t ret = RT_EOK;
    
    /* create app event */
    app_event = rt_event_create("app_event", RT_IPC_FLAG_FIFO);
    if (RT_NULL == app_event)
    {
        LOG_E("create app event failed!");
        ret = -RT_ERROR;
        goto __exit;
    }
    
    report_timer = rt_timer_create("report_timer", report_timer_timeout, 
                            RT_NULL, rt_tick_from_millisecond(APP_MQTT_REPORT_INTERVAL * 1000), 
                            RT_TIMER_FLAG_PERIODIC);
    if (RT_NULL == report_timer)
    {
        LOG_E("create report timer failed!");
        ret = -RT_ERROR;
        goto __exit;
    }
    
    mq_init_params.cmd_timeout = KAWAII_MQTT_MAX_CMD_TIMEOUT;
    mq_init_params.read_buf_size = 1024;
    mq_init_params.write_buf_size = 1024;
    mq_init_params.connect_params.network_params.port = "18830";
    mq_init_params.connect_params.network_params.addr = "mq.tongxinmao.com";

    mq_init_params.connect_params.user_name = RT_NULL;
    mq_init_params.connect_params.password = RT_NULL;
    /* generate the random client ID */
    rt_snprintf(mqtt_client_id, sizeof(mqtt_client_id), "rtu_%d", rt_tick_get());
    mq_init_params.connect_params.client_id = mqtt_client_id;
    mq_init_params.connect_params.clean_session = 1;

    {
        int iret = mqtt_init(&mq_client, &mq_init_params);
        if (iret != MQTT_SUCCESS_ERROR)
        {
            LOG_E("mqtt init error(%d)", ret);
            ret = -RT_ERROR;
            goto __exit;
        }
    }
    
    /* 监听网络就绪 */
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

static rt_err_t app_mqtt_report()
{
    LOG_D("app_mqtt_report()");

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

int main(void)
{
    /* 关闭LOG输出(可使用AT指令打开) */
    ulog_global_filter_lvl_set(LOG_FILTER_LVL_SILENT);
    
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
        ret = rt_event_recv(app_event, (APP_EVENT_MQTT_CONNECT_REQ | APP_EVENT_MQTT_REPORT_REQ),
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
        
        if (event_recved & APP_EVENT_MQTT_REPORT_REQ)
        {
            app_mqtt_report();
        }
    }
    
    app_deinit();
    
    return ret;
}
