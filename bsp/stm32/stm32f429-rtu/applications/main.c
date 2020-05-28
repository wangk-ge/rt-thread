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
#include <math.h>
#include <jsmn.h>
#include <at_device.h>
#include <at.h>
#include "config.h"
#include "common.h"
#include "util.h"
#include "strref.h"
#include "app.h"
#include "http_ota.h"
#include "at_esp32.h"
#include "history_data.h"
#include "wdt.h"

#define LOG_TAG              "main"
#define LOG_LVL              LOG_LVL_DBG
#include <rtdbg.h>

/* APP主循环消息 */
typedef enum
{
    APP_MSG_NONE = 0,
    APP_MSG_MQTT_CLIENT_START_REQ, // MQTT客户端启动请求
    APP_MSG_DATA_ACQUISITION_REQ, // 数据采集请求
    APP_MSG_MQTT_PUBLISH_REQ, // MQTT发布数据请求
    APP_MSG_MQTT_CLIENT_ONLINE, // MQTT客户端器上线
    APP_MSG_MQTT_CLIENT_OFFLINE, // MQTT客户端器下线
    APP_MSG_RESTART_REQ, // 重启系统请求
} app_msg_type;

/* 数据上报事件 */
#define DATA_REPORT_EVENT_REQ 0x00000001 // 数据上报请求(请求上报线程上报数据)
#define DATA_REPORT_EVENT_ACK 0x00000002 // 数据上报应答(收到服务器应答)

/* RS485设备名 */
#define RS485_1_DEVICE_NAME "/dev/uart2"
#define RS485_2_DEVICE_NAME "/dev/uart8"
#define RS485_3_DEVICE_NAME "/dev/uart6"
#define RS485_4_DEVICE_NAME "/dev/uart7"
//#define RS485_4_DEVICE_NAME "/dev/uart5"

/* RS485接收使能GPIO引脚 */
#define RS485_1_REN_PIN GET_PIN(E, 10) // PE10(高电平发送,低电平接收)
#define RS485_2_REN_PIN GET_PIN(E, 11) // PE11(高电平发送,低电平接收)
#define RS485_3_REN_PIN GET_PIN(E, 12) // PE12(高电平发送,低电平接收)
#define RS485_4_REN_PIN GET_PIN(E, 13) // PE13(高电平发送,低电平接收)

/* 历史数据分区名 */
#define HISTORY_DATA_PARTITION "data"

/* MQTT订阅主题名缓冲区长度 */
#define MQTT_TOPIC_BUF_LEN (128)

/* 上报数据用的JSON数据缓冲区 */
#define JSON_DATA_BUF_LEN (APP_MP_BLOCK_SIZE - MQTT_TOPIC_BUF_LEN - sizeof(mqtt_publish_data_info))

/* HTTP OTA URL缓冲区长度 */
#define HTTP_OTA_URL_BUF_LEN (512)

/* HTTP OTA Request ID缓冲区长度 */
#define HTTP_OTA_REQ_ID_BUF_LEN (64)

/* HTTP OTA Version缓冲区长度 */
#define HTTP_OTA_VERSION_BUF_LEN (64)

/* 主循环消息队列最大长度 */
#define APP_MSG_QUEUE_LEN (8)

/* MQTT客户端线程栈大小 */
#define MQTT_CLIENT_THREAD_STACK_SIZE (8192)

/* MQTT客户端线程优先级(优先级必须高于主线程) */
#define MQTT_CLIENT_THREAD_PRIORITY (RT_MAIN_THREAD_PRIORITY - 1)

/* HTTP OTA线程优先级(优先级低于主线程) */
#define HTTP_OTA_THREAD_PRIORITY (RT_MAIN_THREAD_PRIORITY + 2)

/* HTTP OTA线程栈大小 */
#define HTTP_OTA_THREAD_STACK_SIZE (4096)

/* HTTP OTA下载失败最大重试次数 */
#define HTTP_OTA_DOWNLOAD_MAX_RETRY_CNT (100)

/* 数据上报线程优先级(优先级低于主线程) */
#define DATA_REPORT_THREAD_PRIORITY (RT_MAIN_THREAD_PRIORITY + 1)

/* 数据上报线程栈大小 */
#define DATA_REPORT_THREAD_STACK_SIZE (4096)

/* 数据上报ACK超时时间(ms) */
#define DATA_REPORT_ACK_TIMEOUT (60 * 1000)

/* 上报没收到ACK最大重试次数 */
#define DATA_REPORT_MAX_RERTY_CNT (3)

/* 上报时等待MQTT连接的时间(s) */
#define DATA_REPORT_WAIT_CONNECT_TIME (60)

/* MQTT离线超时时间(s) */
#define MQTT_OFFLINE_TIMEOUT (30 * 60)

/* 看门狗超时时间(s) */
#define APP_WDT_TIMEOUT (30)

/* MODBUS响应超时时间(s) */
#define MODBUS_RESP_TIMEOUT (1)

/* 连续上报失败最大允许次数(超过后将重启系统) */
#define REPORT_FAIL_MAX_CNT (10)

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
    char url[HTTP_OTA_URL_BUF_LEN];
    uint8_t md5[16];
    char req_id[HTTP_OTA_REQ_ID_BUF_LEN];
    char version[HTTP_OTA_VERSION_BUF_LEN];
    int firmware_size;
} http_ota_info;

/* RS485接口信息 */
typedef struct
{
    const char *dev_name; // 设备名
    rt_base_t rts_pin; //RTS引脚
    int serial_mode;
    modbus_t *mb_ctx;
} rs485_device_info;

static rs485_device_info rs485_dev_infos[CFG_UART_X_NUM] = 
{
    {RS485_1_DEVICE_NAME, RS485_1_REN_PIN, MODBUS_RTU_RS485, RT_NULL},
    {RS485_2_DEVICE_NAME, RS485_2_REN_PIN, MODBUS_RTU_RS485, RT_NULL},
    {RS485_3_DEVICE_NAME, RS485_3_REN_PIN, MODBUS_RTU_RS485, RT_NULL},
    {RS485_4_DEVICE_NAME, RS485_4_REN_PIN, MODBUS_RTU_RS485, RT_NULL},
    //{RS485_4_DEVICE_NAME, 0xFFFFFFFF, MODBUS_RTU_RS232, RT_NULL},
};

/* 主消息循环队列 */
static rt_mq_t app_msg_queue = RT_NULL;

/* APP内存池 */
static rt_mp_t app_mp = RT_NULL;

/* 定时上报定时器 */
static rt_timer_t report_timer = RT_NULL;
/* 定时采集定时器 */
static rt_timer_t acquisition_timer = RT_NULL;
/* MQTT离线超时检测定时器 */
static rt_timer_t mqtt_offline_check_timer = RT_NULL;

/* HTTP OTA线程 */
static rt_thread_t http_ota_thread = RT_NULL;
/* HTTP OTA互斥锁(用于保护http_ota_thread变量) */
static rt_mutex_t http_ota_mutex = RT_NULL;

/* 数据上报线程 */
static rt_thread_t data_report_thread = RT_NULL;
/* 数据上报Event对象 */
static rt_event_t data_report_event = RT_NULL;

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

/* 从APP内存池分配一块内存 */
void *app_mp_alloc(void)
{
    void *buf = rt_mp_alloc(app_mp, RT_WAITING_FOREVER); // 如果暂时没有足够的内存将会阻塞等待
    RT_ASSERT(buf != RT_NULL);
    
    return buf;
}

/* 释放APP内存池分配的内存块 */
void app_mp_free(void *buf)
{
    if (buf != RT_NULL)
    {
        rt_mp_free(buf);
    }
}

/* 从内存池分配at_response_t对象 */
at_response_t app_alloc_at_resp(rt_size_t line_num, rt_int32_t timeout)
{
    uint8_t *buf = (uint8_t*)rt_mp_alloc(app_mp, RT_WAITING_FOREVER); // 如果暂时没有足够的内存将会阻塞等待
    RT_ASSERT(buf != RT_NULL);
    
    at_response_t resp = (at_response_t)buf;
    resp->buf = (char*)(buf + sizeof(struct at_response));
    resp->buf_size = APP_MP_BLOCK_SIZE - sizeof(struct at_response);
    resp->line_num = line_num;
    resp->line_counts = 0;
    resp->timeout = timeout;
    
    //LOG_D("%s at_resp_mp=0x%08x, resp=0x%08x", __FUNCTION__, app_mp, resp);
    
    return resp;
}

/* 释放at_response_t对象 */
void app_free_at_resp(at_response_t resp)
{
    //LOG_D("%s resp=0x%08x", __FUNCTION__, resp);
    
    if (resp != RT_NULL)
    {
        rt_mp_free(resp);
    }
}

/* 分配mqtt_publish_data_info对象 */
static mqtt_publish_data_info *mqtt_alloc_publish_data_info(void)
{
    mqtt_publish_data_info *pub_info = 
        (mqtt_publish_data_info*)rt_mp_alloc(app_mp, RT_WAITING_FOREVER); // 如果暂时没有足够的内存将会阻塞等待
    RT_ASSERT(pub_info != RT_NULL);
    pub_info->qos = QOS0;
    pub_info->length = 0;
    pub_info->topic = (char*)pub_info + sizeof(mqtt_publish_data_info);
    pub_info->payload = pub_info->topic + MQTT_TOPIC_BUF_LEN;
    
    return pub_info;
}

/* 释放mqtt_publish_data_info对象 */
static void mqtt_free_publish_data_info(mqtt_publish_data_info *pub_info)
{
    if (pub_info != RT_NULL)
    {
        rt_mp_free(pub_info);
    }
}

/* 分配http_ota_info对象 */
static http_ota_info *http_alloc_ota_info(void)
{
    http_ota_info *ota_info = 
        (http_ota_info*)rt_mp_alloc(app_mp, RT_WAITING_FOREVER); // 如果暂时没有足够的内存将会阻塞等待
    RT_ASSERT(ota_info != RT_NULL);
    
    return ota_info;
}

/* 释放http_ota_info对象 */
static void http_free_ota_info(http_ota_info *ota_info)
{
    if (ota_info != RT_NULL)
    {
        rt_mp_free(ota_info);
    }
}

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
static rt_err_t send_mqtt_publish_req(mqtt_publish_data_info *pub_info)
{
    rt_err_t ret = app_send_msg(APP_MSG_MQTT_PUBLISH_REQ, pub_info);
    if (ret != RT_EOK)
    {
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

/* 取得时区 */
int get_timezone(int *zz)
{
    struct at_device *device = at_device_get_by_name(AT_DEVICE_NAMETYPE_DEVICE, TB22_DEVICE_NAME);
    return at_device_control(device, AT_DEVICE_CTRL_GET_TIMEZONE, zz);
}

/* 取得时间戳(UNIX时间戳格式) */
uint32_t get_timestamp(void)
{
    /* 当前时区 */
    int zz = 0;
    get_timezone(&zz);
    /* 本地时间 */
    time_t now = time(RT_NULL);
    /* 去除时区 */
    now -= ((zz / 4) * 3600);
    
    return (uint32_t)now;
}

/* 取得客户端唯一编号 */
uint32_t get_clientid(void)
{
    config_info *cfg = cfg_get();
    return cfg->client_id;
}

/* 取得产品编号 */
const char* get_productkey(void)
{
    config_info *cfg = cfg_get();
    if (cfg->productkey == NULL)
    {
        return "productKey";
    }
    return cfg->productkey;
}

/* 取得设备ID */
const char* get_devicecode(void)
{
    config_info *cfg = cfg_get();
    if (cfg->devicecode == NULL)
    {
        return "deviceCode";
    }
    return cfg->devicecode;
}

/* 取得标签ID */
const char* get_itemid(void)
{
    config_info *cfg = cfg_get();
    if (cfg->itemid == NULL)
    {
        return "";
    }
    return cfg->itemid;
}

/* 采集UARTX数据(返回读取的寄存器个数) */
static uint32_t uart_x_data_acquisition(int x, uint16_t *data_buf, uint32_t buf_len)
{
    LOG_D("%s(UART%d)", __FUNCTION__, x);
    
    /* 内部函数不做参数检查,由调用者保证参数有效性 */
    int index = x - 1;
    
    uint32_t data_len = 0; // 采集的数据长度(寄存器个数)
    
    RT_ASSERT(index < ARRAY_SIZE(rs485_dev_infos));
    modbus_t *mb_ctx = rs485_dev_infos[index].mb_ctx;

    /* 配置信息 */
    config_info *cfg = cfg_get();
    
    /* 采集数据 */
    memset(data_buf, 0, buf_len * sizeof(data_buf[0])); // 先清零
    int i = 0;
    uint8_t variablecnt = cfg->uart_x_cfg[index].variablecnt; // 变量个数
    for (i = 0; i < variablecnt; ++i)
    {
        uint16_t startaddr = cfg->uart_x_cfg[index].startaddr[i]; // 寄存器地址
        uint16_t length = cfg->uart_x_cfg[index].length[i]; // 寄存器个数
        uint8_t function = cfg->uart_x_cfg[index].function[i]; // 功能码
        uint8_t delay = cfg->uart_x_cfg[index].delay[i]; // 采集延时(单位100ms)
        
        modbus_set_slave(mb_ctx, cfg->uart_x_cfg[index].slaveraddr[i]); // 从机地址
        
        #define MODBUS_READ_MAX_TETRY_CNT 3 // 最大重试次数
        int iRetryCnt = MODBUS_READ_MAX_TETRY_CNT;
        int read_bytes = 0;
        
        __retry:
        
        if (function == 0x03)
        {
            /* Reads the holding registers of remote device and put the data into an array */
            read_bytes = modbus_read_registers(mb_ctx, startaddr, length, (data_buf + data_len)); // 读取寄存器数据
        }
        else //if (function == 0x04)
        {
            /* Reads the input registers of remote device and put the data into an array */
            read_bytes = modbus_read_input_registers(mb_ctx, startaddr, length, (data_buf + data_len)); // 读取寄存器数据
        }
        
        if (read_bytes != length)
        { // 失败
            
            --iRetryCnt;
            
            LOG_W("%s modbus_read_registers(0x%04x,%u) return(%u), try %d!", __FUNCTION__, startaddr, length, read_bytes, 
                (MODBUS_READ_MAX_TETRY_CNT - iRetryCnt));
            
            if (iRetryCnt > 0)
            {    
                goto __retry;
            }
        }
        else
        { // 成功
            /* 等待一段时间再读取下一个变量 */
            rt_thread_mdelay(delay * 100);
        }
        
        data_len += (uint32_t)length;
    }
    
    return data_len;
}

/* 采集数据并保存到Flash */
static rt_err_t data_acquisition_and_save(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    rt_err_t ret = RT_EOK;
    uint8_t* data_buf = (uint8_t*)app_mp_alloc();
    RT_ASSERT(data_buf);
    uint8_t* buf_write_ptr = data_buf;
    
    /* 采集时间 */
    uint32_t timestamp = get_timestamp();
    memcpy(buf_write_ptr, &timestamp, sizeof(timestamp));
    buf_write_ptr += sizeof(timestamp);
    
    int x = 1;
    for (x = 1; x <= CFG_UART_X_NUM; ++x)
    {
        /* 采集UARTX数据(返回读取的寄存器个数) */
        uint32_t reg_num = uart_x_data_acquisition(x, (uint16_t*)buf_write_ptr, MODBUS_RTU_MAX_ADU_LENGTH);
        buf_write_ptr += reg_num * sizeof(uint16_t); // 每个寄存器数据为uint16_t
    }
    
    /* 保存采集的数据 */
    size_t data_len = buf_write_ptr - data_buf;
    ret = history_data_save(data_buf, data_len);
    if (ret != RT_EOK)
    {
        LOG_E("%s() history_data_save failed(%d)!", __FUNCTION__, ret);
    }
    
    app_mp_free(data_buf);
    
    return ret;
}

/* 
 * 读取FIFO队列中指定位置的一条历史数据的采集时间戳
 *
 */
static uint32_t read_history_pos_data_timestamp(uint32_t read_pos)
{
    uint32_t timestamp = 0;
    history_data_load_pos(read_pos, (uint8_t*)&timestamp, sizeof(timestamp));
    return timestamp;
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
    
    /* 加载数据 */
    uint8_t* data_buf = (uint8_t*)app_mp_alloc();
    RT_ASSERT(data_buf);
    uint8_t* buf_read_ptr = data_buf;
    history_data_load_pos(read_pos, data_buf, cfg->data_size);
    
    /* 编码成JSON格式 */
    json_data_buf[json_data_len++] = '{';
    
    /* 读取时间戳 */
    uint32_t timestamp = 0;
    memcpy(&timestamp, buf_read_ptr, sizeof(timestamp));
    buf_read_ptr += sizeof(timestamp);
        
    if (need_timestamp)
    {
        /* 时间戳编码成JSON格式并写入缓冲区 */
        struct tm* local_time = localtime(&timestamp);
        json_data_len += rt_snprintf((json_data_buf + json_data_len), (json_buf_len - json_data_len), 
            "\"ts\":\"%04d%02d%02d%02d%02d%02d\",", (local_time->tm_year + 1900), (local_time->tm_mon + 1), 
            local_time->tm_mday, local_time->tm_hour, local_time->tm_min, local_time->tm_sec);
    }
    
    /* 读取UARTX数据 */
    int x = 1;
    for (x = 1; x <= CFG_UART_X_NUM; ++x)
    {
        int i = 0;
        for (i = 0; i < cfg->uart_x_cfg[x - 1].variablecnt; ++i)
        {
            /* 每个寄存器16bit */
            uint16_t reg_num = cfg->uart_x_cfg[x - 1].length[i]; // 变量寄存器个数
            uint8_t data_type = cfg->uart_x_cfg[x - 1].type[i]; // 变量类型
            uint16_t *var_data = (uint16_t*)buf_read_ptr; // 变量数据地址
            buf_read_ptr += reg_num * sizeof(uint16_t); // 指向下一个变量起始
            
            switch (data_type)
            {
                case 0x00: // 有符号16位int(AB)
                {
                    int16_t int16_data = (int16_t)var_data[0];
                    json_data_len += rt_snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                        "\"%s\":%d,", cfg->uart_x_cfg[x - 1].variable[i], (int)(int16_data));
                    break;
                }
                case 0x01: // 有符号16位int(BA)
                {
                    int16_t int16_data = (int16_t)((var_data[0] << 8) | (var_data[0] >> 8));
                    json_data_len += rt_snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                        "\"%s\":%d,", cfg->uart_x_cfg[x - 1].variable[i], (int)(int16_data));
                    break;
                }
                case 0x02: // 无符号16位int(AB)
                {
                    uint16_t uint16_data = (uint16_t)var_data[0];
                    json_data_len += rt_snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                        "\"%s\":%u,", cfg->uart_x_cfg[x - 1].variable[i], (uint32_t)uint16_data);
                    break;
                }
                case 0x03: // 无符号16位int(BA)
                {
                    uint16_t uint16_data= (uint16_t)((var_data[0] << 8) | (var_data[0] >> 8));
                    json_data_len += rt_snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                        "\"%s\":%u,", cfg->uart_x_cfg[x - 1].variable[i], (uint32_t)uint16_data);
                    break;
                }
                case 0x04: // 有符号32位int(ABCD)
                {
                    int32_t int32_data = (int32_t)modbus_get_long_abcd(var_data);
                    json_data_len += snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                        "\"%s\":%d,", cfg->uart_x_cfg[x - 1].variable[i], int32_data);
                    break;
                }
                case 0x05: // 有符号32位int(DCBA)
                {
                    int32_t int32_data = (int32_t)modbus_get_long_dcba(var_data);
                    json_data_len += snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                        "\"%s\":%d,", cfg->uart_x_cfg[x - 1].variable[i], int32_data);
                    break;
                }
                case 0x06: // 有符号32位int(BADC)
                {
                    int32_t int32_data = (int32_t)modbus_get_long_badc(var_data);
                    json_data_len += snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                        "\"%s\":%d,", cfg->uart_x_cfg[x - 1].variable[i], int32_data);
                    break;
                }
                case 0x07: // 有符号32位int(CDAB)
                {
                    int32_t int32_data = (int32_t)modbus_get_long_cdab(var_data);
                    json_data_len += snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                        "\"%s\":%d,", cfg->uart_x_cfg[x - 1].variable[i], int32_data);
                    break;
                }
                case 0x08: // 无符号32位int(ABCD)
                {
                    uint32_t uint32_data = (uint32_t)modbus_get_long_abcd(var_data);
                    json_data_len += snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                        "\"%s\":%u,", cfg->uart_x_cfg[x - 1].variable[i], uint32_data);
                    break;
                }
                case 0x09: // 无符号32位int(DCBA)
                {
                    uint32_t uint32_data = (uint32_t)modbus_get_long_dcba(var_data);
                    json_data_len += snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                        "\"%s\":%u,", cfg->uart_x_cfg[x - 1].variable[i], uint32_data);
                    break;
                }
                case 0x0A: // 无符号32位int(BADC)
                {
                    uint32_t uint32_data = (uint32_t)modbus_get_long_badc(var_data);
                    json_data_len += snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                        "\"%s\":%u,", cfg->uart_x_cfg[x - 1].variable[i], uint32_data);
                    break;
                }
                case 0x0B: // 无符号32位int(CDAB)
                {
                    uint32_t uint32_data = (uint32_t)modbus_get_long_cdab(var_data);
                    json_data_len += snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                        "\"%s\":%u,", cfg->uart_x_cfg[x - 1].variable[i], uint32_data);
                    break;
                }
                case 0x0C: // IEEE754浮点数(ABCD)
                {
                    float float_data = modbus_get_float_abcd(var_data);
                    if (isnan(float_data))
                    {
                        json_data_len += snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                            "\"%s\":%s,", cfg->uart_x_cfg[x - 1].variable[i], "null");
                    }
                    else
                    {
                        json_data_len += snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                            "\"%s\":%f,", cfg->uart_x_cfg[x - 1].variable[i], float_data);
                    }
                    break;
                }
                case 0x0D: // IEEE754浮点数(DCBA)
                {
                    float float_data = modbus_get_float_dcba(var_data);
                    if (isnan(float_data))
                    {
                        json_data_len += snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                            "\"%s\":%s,", cfg->uart_x_cfg[x - 1].variable[i], "null");
                    }
                    else
                    {
                        json_data_len += snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                            "\"%s\":%f,", cfg->uart_x_cfg[x - 1].variable[i], float_data);
                    }
                    break;
                }
                case 0x0E: // IEEE754浮点数(BADC)
                {
                    float float_data = modbus_get_float_badc(var_data);
                    if (isnan(float_data))
                    {
                        json_data_len += snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                            "\"%s\":%s,", cfg->uart_x_cfg[x - 1].variable[i], "null");
                    }
                    else
                    {
                        json_data_len += snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                            "\"%s\":%f,", cfg->uart_x_cfg[x - 1].variable[i], float_data);
                    }
                    break;
                }
                case 0x0F: // IEEE754浮点数(CDAB)
                {
                    float float_data = modbus_get_float_cdab(var_data);
                    if (isnan(float_data))
                    {
                        json_data_len += snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                            "\"%s\":%s,", cfg->uart_x_cfg[x - 1].variable[i], "null");
                    }
                    else
                    {
                        json_data_len += snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                            "\"%s\":%f,", cfg->uart_x_cfg[x - 1].variable[i], float_data);
                    }
                    break;
                }
                default:
                {
                    RT_ASSERT(0);
                    break;
                }
            }
        }
    }
    if (json_data_len < json_buf_len)
    {
        json_data_buf[json_data_len - 1] = '}'; // 末尾','改成'}'
        json_data_buf[json_data_len] = '\0'; // 添加'\0'
    }
    
    app_mp_free(data_buf);
    
    return json_data_len;
}

/* 
 * 读取前n个时刻的一条历史数据的采集时间戳
 *
 *   n=0 读取最近一条历史数据
 *   n>0 读取前n个时刻的一条历史数据
 *
 */
uint32_t read_history_data_timestamp(uint32_t n)
{
    /* 读取历史数据队列信息 */
    history_data_fifo_info fifo_info = {0};
    rt_err_t ret = history_data_get_fifo_info(&fifo_info);
    if (ret != RT_EOK)
    {
        LOG_E("%s history_data_get_fifo_info failed(%d)!", __FUNCTION__, ret);
        return 0;
    }
    
    /* 状态检查 */
    if (fifo_info.length <= 0)
    { // 队列为空
        LOG_W("%s history data is empty!", __FUNCTION__);
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
        read_pos = history_data_get_max_num() - n;
    }
    
    /* 读取read_pos处的一条历史数据的采集时间戳 */
    return read_history_pos_data_timestamp(read_pos);
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
    history_data_fifo_info fifo_info = {0};
    rt_err_t ret = history_data_get_fifo_info(&fifo_info);
    if (ret != RT_EOK)
    {
        LOG_E("%s history_data_get_fifo_info failed(%d)!", __FUNCTION__, ret);
        return 0;
    }
    
    /* 状态检查 */
    if (fifo_info.length <= 0)
    { // 队列为空
        LOG_W("%s history data is empty!", __FUNCTION__);
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
        read_pos = history_data_get_max_num() - n;
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
    EfErrCode ef_ret = EF_NO_ERR;
    
    /* 清除保存的历史数据 */
    ret = history_data_clear();
    if (ret != RT_EOK)
    {
        LOG_W("%s history_data_clear() error(%d)!", __FUNCTION__, ret);
        //ret = -RT_ERROR;
        //goto __exit;
        ret = RT_EOK;
    }
    
    /* 清除上报位置 */
    ef_ret = ef_del_env("report_pos");
    if (ef_ret != EF_NO_ERR)
    {
        LOG_W("%s ef_del_env(report_pos) error(%d)!", __FUNCTION__, ef_ret);
        //ret = -RT_ERROR;
        //goto __exit;
        ret = RT_EOK;
    }
    
//__exit:
    
    return ret;
}

/* 
 * 取得历史数据条目数
 */
uint32_t get_history_data_num(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    /* 读取历史数据队列信息 */
    history_data_fifo_info fifo_info = {0};
    rt_err_t ret = history_data_get_fifo_info(&fifo_info);
    if (ret != RT_EOK)
    {
        LOG_E("%s history_data_get_fifo_info failed(%d)!", __FUNCTION__, ret);
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
    int rssi = 0;
    int ret = get_modem_rssi(&rssi);
    if (ret != RT_EOK)
    {
        LOG_E("%s get_modem_rssi failed(%d)!", __FUNCTION__, ret);
    }
    
    rt_int32_t json_data_len = rt_snprintf(json_data_buf, json_buf_len, 
        "{\"cycleSet\":\"%u\",\"acquisitionSet\":\"%u\",\"autoControlSet\":\"0\","
        "\"aIPSet\":\"%s\",\"aPortSet\":\"%u\",\"bIPSet\":\"%s\",\"bPortSet\":\"%u\","
        "\"rssi\":\"%d\",\"version\":\"%s\"}", cfg->cycle, cfg->acquisition, 
        cfg->a_ip, cfg->a_port, cfg->b_ip, cfg->b_port, rssi, SW_VERSION);
    
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
    if (!strref_is_empty(a_ip))
    {
        char a_ip_str[64] = "";
        if (a_ip->len > (sizeof(a_ip_str) - 1))
        {
            LOG_E("%s length of addr(%.*s)>%d!", __FUNCTION__, a_ip->len, a_ip->c_str, (sizeof(a_ip_str) - 1));
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
        
        strref_str_cpy(a_ip_str, sizeof(a_ip_str), a_ip);
        
        /* 检查地址有效性 */
        bool addr_valid = util_is_ip_valid(a_ip_str);
        if (!addr_valid)
        {
            addr_valid = util_is_domainname_valid(a_ip_str);
        }
        
        if (!addr_valid)
        {
            LOG_E("%s addr(%s) is invalid!", __FUNCTION__, a_ip_str);
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
    if (!strref_is_empty(b_ip))
    {
        char b_ip_str[64] = "";
        if (b_ip->len > (sizeof(b_ip_str) - 1))
        {
            LOG_E("%s length of addr(%.*s)>%d!", __FUNCTION__, b_ip->len, b_ip->c_str, (sizeof(b_ip_str) - 1));
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
        
        strref_str_cpy(b_ip_str, sizeof(b_ip_str), b_ip);
        
        /* 检查地址有效性 */
        bool addr_valid = util_is_ip_valid(b_ip_str);
        if (!addr_valid)
        {
            addr_valid = util_is_domainname_valid(b_ip_str);
        }
        
        if (!addr_valid)
        {
            LOG_E("%s addr(%s) is invalid!", __FUNCTION__, b_ip_str);
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
        EfErrCode ef_ret = ef_set_env_blob("a_ip", a_ip->c_str, a_ip->len);
        if (ef_ret != EF_NO_ERR)
        {
            LOG_E("%s ef_set_env_blob(a_ip,%.*s) error(%d)!", __FUNCTION__, a_ip->len, a_ip->c_str, ef_ret);
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
        EfErrCode ef_ret = ef_set_env_blob("b_ip", b_ip->c_str, b_ip->len);
        if (ef_ret != EF_NO_ERR)
        {
            LOG_E("%s ef_set_env_blob(b_ip,%.*s) error(%d)!", __FUNCTION__, b_ip->len, b_ip->c_str, ef_ret);
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
            req_restart(); // 请求重启系统
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
    
    /* 请求数据上报 */
    rt_err_t ret = req_data_report();
    if (ret != RT_EOK)
    {
        LOG_D("%s() req_data_report failed(%d)!", __FUNCTION__, ret);
    }
}

static void acquisition_timer_timeout(void *parameter)
{
    LOG_D("%s()", __FUNCTION__);
    
    app_send_msg(APP_MSG_DATA_ACQUISITION_REQ, RT_NULL); // 发送数据采集请求
}

static void mqtt_offline_check_timer_timeout(void *parameter)
{
    LOG_D("%s()", __FUNCTION__);
    
    req_restart(); // 请求重启系统
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

/* 读取待上报数据位置 */
static rt_err_t get_report_pos(uint32_t *pos)
{
    /* 加载待上报数据位置信息 */
    size_t len = ef_get_env_blob("report_pos", pos, sizeof(uint32_t), NULL);
    if (len != sizeof(uint32_t))
    {
        /* 加载待上报数据位置信息失败(第一次上报?) */
        LOG_E("%s ef_get_env_blob(report_pos) failed!", __FUNCTION__);
        return -RT_ERROR;
    }
    
    return RT_EOK;
}

/* 设置待上报数据位置 */
static rt_err_t set_report_pos(uint32_t pos)
{
    LOG_D("%s() pos=%u", __FUNCTION__, pos);
    
    /* 保存待上报数据位置信息 */
    EfErrCode result = ef_set_env_blob("report_pos", &pos, sizeof(uint32_t));
    if (result != EF_NO_ERR)
    {
        /* 保存待上报数据位置信息失败 */
        LOG_E("%s ef_set_env_blob(report_pos) failed(%d)!", __FUNCTION__, result);
        return -RT_ERROR;
    }
    
    return RT_EOK;
}

/* 移动待上报数据位置到下一个位置(返回0表示移动位置成功,返回1表示队列上报已全部完成) */
static int move_report_pos_to_next(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    int ret = RT_EOK;
    /* 待上报历史数据位置 */
    uint32_t report_pos = 0;
    /* 历史数据队列信息 */
    history_data_fifo_info fifo_info = {0};
    
    /* 读取历史数据队列信息 */
    ret = history_data_get_fifo_info(&fifo_info);
    if (ret != RT_EOK)
    {
        LOG_W("%s history_data_get_fifo_info failed(%d), history data is empty!", __FUNCTION__, ret);
        ret = 1; // (队列上报已全部完成)
        goto __exit;
    }
    
    /* 状态检查 */
    if (fifo_info.length <= 0)
    { // 队列为空
        LOG_W("%s history data is empty!", __FUNCTION__);
        ret = 1; // (队列上报已全部完成)
        goto __exit;
    }
    
    /* 读取待上报数据位置 */
    ret = get_report_pos(&report_pos);
    if (ret != RT_EOK)
    {
        LOG_D("%s get_report_pos not found.", __FUNCTION__);
        /* 从队尾开始上报 */
        report_pos = fifo_info.tail_pos;
    }

    if (report_pos == fifo_info.head_pos)
    { // 已到达队列头部(队列上报已全部完成)
        LOG_W("%s report pos reach the fifo head!", __FUNCTION__);
        ret = 1; // (队列上报已全部完成)
        goto __exit;
    }
    
    /* 移动到下一个位置 */
    report_pos++;
    if (report_pos > history_data_get_max_num())
    {
        report_pos = 0;
    }

    /* 保存新的待上报位置 */
    ret = set_report_pos(report_pos);
    if (ret != RT_EOK)
    {
        LOG_E("%s set_report_pos(%u) failed!", __FUNCTION__, report_pos);
        goto __exit;
    }
    
    LOG_D("%s set_report_pos(%d) success.", __FUNCTION__, report_pos);
        
    ret = RT_EOK;
    
__exit:
    return ret;
}

/* 上报待上报历史数据位置的数据(返回0表示上报成功,返回1表示已完所有数据成上报) */
static int app_data_report(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    int ret = RT_EOK;
    mqtt_publish_data_info *pub_info = RT_NULL;
    /* 待上报历史数据位置 */
    uint32_t report_pos = 0;
    /* 历史数据队列信息 */
    history_data_fifo_info fifo_info = {0};
    
    /* 读取历史数据队列信息 */
    ret = history_data_get_fifo_info(&fifo_info);
    if (ret != RT_EOK)
    {
        LOG_W("%s history_data_get_fifo_info failed(%d), history data is empty!", __FUNCTION__, ret);
        ret = 1; // (队列上报已全部完成)
        goto __exit;
    }
    
    /* 状态检查 */
    if (fifo_info.length <= 0)
    { // 队列为空
        LOG_W("%s history data is empty!", __FUNCTION__);
        ret = 1; // (队列上报已全部完成)
        goto __exit;
    }
    
    /* 读取待上报数据位置 */
    ret = get_report_pos(&report_pos);
    if (ret != RT_EOK)
    {
        LOG_D("%s get_report_pos not found.", __FUNCTION__);
        /* 从队尾开始上报 */
        report_pos = fifo_info.tail_pos;
    }
    
    if (report_pos == fifo_info.head_pos)
    { // 已到达队列头部(队列上报已全部完成)
        LOG_W("%s report pos reach the fifo head!", __FUNCTION__);
        ret = 1; // (队列上报已全部完成)
        goto __exit;
    }
    
    /* 上报未上报的数据 */
    {
        pub_info = mqtt_alloc_publish_data_info();
        RT_ASSERT(pub_info != RT_NULL);
        char* topic_buf = pub_info->topic;
        char* json_data_buf = pub_info->payload;
        uint32_t read_len = 0;
            
        /* 编码JSON采集数据并发送 */
        uint32_t timestamp = read_history_pos_data_timestamp(report_pos); // 采集时间戳
        rt_int32_t json_data_len = rt_snprintf(json_data_buf, JSON_DATA_BUF_LEN, 
            "{\"productKey\":\"%s\",\"deviceCode\":\"%s\",\"clientId\":\"%010u\","
            "\"timeStamp\":\"%u\",\"itemId\":\"%s\",\"data\":", get_productkey(), get_devicecode(), 
            get_clientid(), timestamp, get_itemid());
        
        /* 读取report_pos处的一条待上报历史数据(JSON格式) */
        read_len = read_history_pos_data_json(report_pos, json_data_buf + json_data_len, JSON_DATA_BUF_LEN - json_data_len, false);
        if (read_len <= 0)
        {
            LOG_E("%s read_history_pos_data_json(%d) read_len(%d) failed!", __FUNCTION__, report_pos, read_len);
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
    
    ret = RT_EOK;
    
__exit:
    if (pub_info != NULL)
    {
        mqtt_free_publish_data_info(pub_info);
    }
    
    return ret;
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
    mqtt_publish_data_info *pub_info = RT_NULL;
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
    if (list_len <= 0)
    {
        LOG_E("%s jsmn_parse failed(%d)!", __FUNCTION__, list_len);
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
    pub_info = mqtt_alloc_publish_data_info();
    RT_ASSERT(pub_info != RT_NULL);
    topic_buf = pub_info->topic;
    json_data_buf = pub_info->payload;
    
    /* 编码JSON采集数据并发送 */
    {
        uint32_t timestamp = read_history_data_timestamp(0); // 采集时间戳
        rt_int32_t json_data_len = rt_snprintf(json_data_buf, JSON_DATA_BUF_LEN, 
            "{\"productKey\":\"%s\",\"deviceCode\":\"%s\",\"clientId\":\"%010u\",\"itemId\":\"%s\","
            "\"timeStamp\":\"%u\",\"requestId\":\"%.*s\",\"data\":", get_productkey(), get_devicecode(), 
            get_clientid(), get_itemid(), timestamp, id.len, id.c_str);
        
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
        pub_info->qos = QOS1;
        pub_info->length = json_data_len;
        ret = send_mqtt_publish_req(pub_info);
        if (ret != RT_EOK)
        {
            LOG_E("%s send_mqtt_publish_req() error(%d)!", __FUNCTION__, ret);
            goto __exit;
        }
    }
    
__exit:
    
    if (ret != RT_EOK)
    {
        if (pub_info != NULL)
        {
            mqtt_free_publish_data_info(pub_info);
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
    mqtt_publish_data_info *pub_info = RT_NULL;
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
    if (list_len <= 0)
    {
        LOG_E("%s jsmn_parse failed(%d)!", __FUNCTION__, list_len);
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
    pub_info = mqtt_alloc_publish_data_info();
    RT_ASSERT(pub_info != RT_NULL);
    topic_buf = pub_info->topic;
    json_data_buf = pub_info->payload;
    
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
        pub_info->qos = QOS1;
        pub_info->length = json_data_len;
        ret = send_mqtt_publish_req(pub_info);
        if (ret != RT_EOK)
        {
            LOG_E("%s send_mqtt_publish_req() error(%d)!", __FUNCTION__, ret);
            goto __exit;
        }
    }
    
__exit:
    
    if (ret != RT_EOK)
    {
        if (pub_info != NULL)
        {
            mqtt_free_publish_data_info(pub_info);
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
    mqtt_publish_data_info *pub_info = RT_NULL;
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
    if (list_len <= 0)
    {
        LOG_E("%s jsmn_parse failed(%d)!", __FUNCTION__, list_len);
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
    pub_info = mqtt_alloc_publish_data_info();
    RT_ASSERT(pub_info != RT_NULL);
    topic_buf = pub_info->topic;
    json_data_buf = pub_info->payload;
    
    /* 编码响应信息并发送 */
    {
        /* 消息内容 */
        uint32_t timestamp = get_timestamp();
        rt_snprintf(topic_buf, MQTT_TOPIC_BUF_LEN, "/sys/%s/%s/config/set", get_productkey(), get_devicecode());
        rt_int32_t json_data_len = rt_snprintf(json_data_buf, JSON_DATA_BUF_LEN, 
            "{\"productKey\":\"%s\",\"deviceCode\":\"%s\",\"operationDate\":\"%u\",\"requestId\":\"%.*s\","
            "\"code\":\"%d\",\"message\":\"%s\",\"topic\":\"%s\",\"data\":{}}", get_productkey(), get_devicecode(), 
            timestamp, id.len, id.c_str, cfg_ret_code, cfg_ret_code_get_message(cfg_ret_code), topic_buf);
        RT_ASSERT(json_data_len < JSON_DATA_BUF_LEN);
        json_data_buf[json_data_len] = '\0';
        
        /* 发布/sys/${productKey}/${deviceCode}/config/set_reply响应 */
        rt_snprintf(topic_buf, MQTT_TOPIC_BUF_LEN, "/sys/%s/%s/config/set_reply", get_productkey(), get_devicecode());
        LOG_D("%s publish(%s) %s", __FUNCTION__, topic_buf, json_data_buf);
        
        /* 请求主线程来发布MQTT数据(回调函数不能阻塞) */
        pub_info->qos = QOS1;
        pub_info->length = json_data_len;
        ret = send_mqtt_publish_req(pub_info);
        if (ret != RT_EOK)
        {
            LOG_E("%s send_mqtt_publish_req() error(%d)!", __FUNCTION__, ret);
            goto __exit;
        }
    }
    
__exit:
    if (ret != RT_EOK)
    {
        if (pub_info != NULL)
        {
            mqtt_free_publish_data_info(pub_info);
        }
    }
    else
    {
        if (strref_str_cmp("1", &restart) == 0)
        {
            if (pub_info != NULL)
            {
                mqtt_free_publish_data_info(pub_info);
            }
            
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
    if (list_len <= 0)
    {
        LOG_E("%s jsmn_parse failed(%d)!", __FUNCTION__, list_len);
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
    if (strref_str_cmp("200", &code) != 0)
    {
        LOG_E("%s code(%.*s)!=200 check failed!", __FUNCTION__, code.len, code.c_str);
        goto __exit;
    }
    
    /* 发送收到服务器ACK事件 */
    {
        rt_err_t ret = rt_event_send(data_report_event, DATA_REPORT_EVENT_ACK);
        if (ret != RT_EOK)
        {
            LOG_E("%s rt_event_send(DATA_REPORT_EVENT_ACK) failed(%d)!", __FUNCTION__, ret);
        }
    }
    
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
            break;
    }
    return "";
}

/* 发送升级进度回复消息 */
static rt_err_t send_upgrade_progress(c_str_ref* req_id, int step)
{
    mqtt_publish_data_info *pub_info = RT_NULL;
    char *topic_buf = NULL;
    char *json_data_buf = NULL;
    rt_err_t ret = RT_EOK;
    
    /* 分配响应消息用的内存 */
    pub_info = mqtt_alloc_publish_data_info();
    RT_ASSERT(pub_info != RT_NULL);
    topic_buf = pub_info->topic;
    json_data_buf = pub_info->payload;
    
    /* 编码响应信息并发送 */
    {
        /* 消息内容 */
        uint32_t timestamp = get_timestamp();
        rt_int32_t json_data_len = rt_snprintf(json_data_buf, JSON_DATA_BUF_LEN, 
            "{\"productKey\":\"%s\",\"deviceCode\":\"%s\",\"operationDate\":\"%u\",\"requestId\":\"%.*s\","
            "\"params\":{\"step\":\"%d\",\"desc\":\"%s\"}}", get_productkey(), get_devicecode(), timestamp, 
            req_id->len, req_id->c_str, step, get_upgrade_progress_desc(step));
        RT_ASSERT(json_data_len < JSON_DATA_BUF_LEN);
        json_data_buf[json_data_len] = '\0';
        
        /* 发布/sys/${productKey}/${deviceCode}/upgrade/progress响应 */
        rt_snprintf(topic_buf, MQTT_TOPIC_BUF_LEN, "/sys/%s/%s/upgrade/progress", get_productkey(), get_devicecode());
        LOG_D("%s publish(%s) %s", __FUNCTION__, topic_buf, json_data_buf);
        
        /* 请求主线程来发布MQTT数据(回调函数不能阻塞) */
        pub_info->qos = QOS1;
        pub_info->length = json_data_len;
        ret = send_mqtt_publish_req(pub_info);
        if (ret != RT_EOK)
        {
            LOG_E("%s send_mqtt_publish_req() error(%d)!", __FUNCTION__, ret);
            goto __exit;
        }
    }
    
__exit:
    if (ret != RT_EOK)
    {
        if (pub_info != NULL)
        {
            mqtt_free_publish_data_info(pub_info);
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

/* 清除已下载待升级的OTA固件版本号等信息 */
static rt_err_t clear_ota_version(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    /* 清除保存的ota_version */
    EfErrCode ef_ret = ef_del_env("ota_version");
    if (ef_ret != EF_NO_ERR)
    {
        LOG_W("%s ef_del_env(ota_version) error(%d)!", __FUNCTION__, ef_ret);
        
        /* 清除失败,下次重启将再次上报! */
        return -RT_ERROR;
    }
    
    /* 清除保存的ota_version */
    ef_ret = ef_del_env("ota_req_id");
    if (ef_ret != EF_NO_ERR)
    {
        LOG_W("%s ef_del_env(ota_req_id) error(%d)!", __FUNCTION__, ef_ret);
        
        /* ota_req_id清除失败不影响 */
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
    
    LOG_I("%s() ota_version=%s", __FUNCTION__, ota_version);
    
    /* 读取OTA请求ID */
    char ota_req_id[64] = "";
    size_t id_len = ef_get_env_blob("ota_req_id", ota_req_id, sizeof(ota_req_id), RT_NULL);
    if ((id_len <= 0) || (id_len >= sizeof(ota_req_id)))
    { // 没有读取到有效的ota_req_id
        LOG_I("%s ef_get_env_blob(ota_req_id) not found!", __FUNCTION__);
        /* 认为没有请求进行OTA升级 */
        return;
    }
    ota_req_id[id_len] = '\0';
    
    LOG_I("%s() ota_req_id=%s", __FUNCTION__, ota_req_id);
    
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
    
    /* 清除保存的ota_version等信息 */
    clear_ota_version();
}

/* HTTP OTA固件下载和升级线程 */
static void http_ota_thread_entry(void *param)
{
    LOG_D("%s()", __FUNCTION__);
    
    http_ota_info *ota_info = (http_ota_info*)param;
    RT_ASSERT(ota_info != RT_NULL);
    
    c_str_ref id = {strlen(ota_info->req_id), ota_info->req_id};
    c_str_ref version = {strlen(ota_info->version), ota_info->version};
    
    /* 下载并校验OTA固件(耗时操作) */
    http_ota_result ret = http_ota_fw_download(ota_info->url, ota_info->firmware_size, 
        ota_info->md5, HTTP_OTA_DOWNLOAD_MAX_RETRY_CNT);
    switch (ret)
    {
        case HTTP_OTA_DOWNLOAD_AND_VERIFY_SUCCESS: // 下载并校验成功
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
            
            /* 等待上报完毕 */
            rt_thread_delay(rt_tick_from_millisecond(10 * 1000));

            /* 重启系统进行升级 */
            http_ota_reboot();
            break;
        }
        case HTTP_OTA_DOWNLOAD_FAIL: // 下载失败
        {
            /* 上报下载失败 */
            send_upgrade_progress(&id, -1);
            break;
        }
        case HTTP_OTA_VERIFY_FAIL: // 校验失败
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
    
    http_free_ota_info(ota_info);

    rt_mutex_take(http_ota_mutex, RT_WAITING_FOREVER);
    http_ota_thread = RT_NULL;
    rt_mutex_release(http_ota_mutex);
}

/* 执行HTTP OTA请求 */
static rt_err_t do_http_ota_request(c_str_ref *url, uint8_t *md5, c_str_ref *req_id, c_str_ref *version, int firmware_size)
{
    LOG_D("%s()", __FUNCTION__);
    
    rt_err_t ret = RT_EOK;
    http_ota_info *ota_info = http_alloc_ota_info();
    RT_ASSERT(ota_info != RT_NULL);
    
    rt_mutex_take(http_ota_mutex, RT_WAITING_FOREVER);
    
    if (http_ota_thread != RT_NULL)
    { // HTTP OTA请求正在处理中(不允许重复请求)
        LOG_E("%s http ota is in processing!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    strref_str_cpy(ota_info->url, sizeof(ota_info->url), url);
    memcpy(ota_info->md5, md5, sizeof(ota_info->md5));
    strref_str_cpy(ota_info->req_id, sizeof(ota_info->req_id), req_id);
    strref_str_cpy(ota_info->version, sizeof(ota_info->version), version);
    ota_info->firmware_size = firmware_size;
    
    /* 创建HTTP OTA线程 */
    http_ota_thread = rt_thread_create("http_ota", http_ota_thread_entry, 
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
        http_ota_thread = RT_NULL;
    }
    
__exit:
    rt_mutex_release(http_ota_mutex);
    
    if (ret != RT_EOK)
    {
        if (ota_info != RT_NULL)
        {
            http_free_ota_info(ota_info);
        }
    }
    
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
        if (list_len <= 0)
        {
            LOG_E("%s jsmn_parse failed(%d)!", __FUNCTION__, list_len);
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
        if (list_len <= 0)
        {
            LOG_E("%s jsmn_parse failed(%d)!", __FUNCTION__, list_len);
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
    
    if (RT_NULL != app_mp)
    {
        rt_mp_delete(app_mp);
        app_mp = RT_NULL;
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
    if (RT_NULL != mqtt_offline_check_timer)
    {
        rt_timer_delete(mqtt_offline_check_timer);
        mqtt_offline_check_timer = RT_NULL;
    }
    if (RT_NULL != http_ota_mutex)
    {
        rt_mutex_delete(http_ota_mutex);
        http_ota_mutex = RT_NULL;
    }
}

/* 初始化MODBUS */
static rt_err_t app_modbus_init(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    rt_err_t ret = RT_EOK;
    config_info *cfg = cfg_get();
    /* 校验位转换表(0=无'NONE',1=奇'EVEN',2=偶'ODD') */
    const char parity_name[] = {'N', 'E', 'O'};
    
    int i = 0;
    for (i = 0; i < ARRAY_SIZE(rs485_dev_infos); ++i)
    {
        rs485_device_info *device_info = &(rs485_dev_infos[i]);
        
        modbus_t *mb_ctx = modbus_new_rtu(device_info->dev_name, 
            cfg->uart_x_cfg[i].baudrate, 
            parity_name[cfg->uart_x_cfg[i].parity], 
            cfg->uart_x_cfg[i].wordlength, 
            cfg->uart_x_cfg[i].stopbits);
        if (mb_ctx == NULL)
        {
            LOG_E("%s call modbus_new_rtu error!", __FUNCTION__);
            ret = -RT_ERROR;
            goto __exit;
        }
        device_info->mb_ctx = mb_ctx;
        
        /* 配置MODBUS RTU属性 */
        modbus_rtu_set_serial_mode(mb_ctx, device_info->serial_mode);
        if (device_info->serial_mode == MODBUS_RTU_RS485)
        {
            rt_pin_mode(device_info->rts_pin, PIN_MODE_OUTPUT);
            /* function shall set the Request To Send mode to communicate on a RS485 serial bus. */
            modbus_rtu_set_rts(mb_ctx, device_info->rts_pin, MODBUS_RTU_RTS_UP);
        }
        modbus_set_response_timeout(mb_ctx, MODBUS_RESP_TIMEOUT, 0); // 响应超时时间
        //modbus_set_debug(mb_ctx, 1); // 使能MODBUS库调试LOG
        
        /* 连接MODBUS端口 */
        int iret = modbus_connect(mb_ctx);
        if (iret != 0)
        {
            LOG_E("%s modbus_connect error(%d)!", __FUNCTION__, errno);
            ret = -RT_ERROR;
            goto __exit;
        }
    }
    
    ret = RT_EOK;
    
__exit:
    if (ret != RT_EOK)
    {
        for (i = 0; i < ARRAY_SIZE(rs485_dev_infos); ++i)
        {
            rs485_device_info *device_info = &(rs485_dev_infos[i]);
            if (device_info->mb_ctx != RT_NULL)
            {
                modbus_close(device_info->mb_ctx);
                modbus_free(device_info->mb_ctx);
                device_info->mb_ctx = RT_NULL;
            }
        }
    }
    
    return ret;
}

/* 数据上报处理线程 */
static void data_report_thread_entry(void *param)
{
    LOG_D("%s()", __FUNCTION__);
    
    uint32_t u32ReportFailCnt = 0; // 上报失败连续次数
    
    rt_int32_t ack_time_out = rt_tick_from_millisecond(DATA_REPORT_ACK_TIMEOUT);
    int retry_count = 0;
    
    while (1)
    {
        rt_uint32_t recved_event = 0;
        /* 等待数据上报请求 */
        rt_err_t ret =  rt_event_recv(data_report_event, DATA_REPORT_EVENT_REQ, 
            RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, RT_WAITING_FOREVER, &recved_event);
        if (ret != RT_EOK)
        {
            LOG_E("%s rt_event_recv failed(%d), quit!", __FUNCTION__, ret);
            break;
        }
        
        if ((recved_event & DATA_REPORT_EVENT_REQ) != DATA_REPORT_EVENT_REQ)
        { // 收到其他事件,不做处理
            LOG_W("%s rt_event_recv() 0x%08x is not DATA_REPORT_EVENT_REQ!", __FUNCTION__, recved_event);
            continue;
        }
        
        /* 清除可能存在的ACK事件 */
        rt_event_recv(data_report_event, DATA_REPORT_EVENT_ACK, 
            RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, 0, &recved_event);
        recved_event = 0;
        
        /* 初始化重试次数 */
        retry_count = DATA_REPORT_MAX_RERTY_CNT;
        
        /* 尝试上报数据 */
__retry:
        /* 检查是否已连接到MQTT服务器 */
        if (!paho_mqtt_is_connected(&mq_client))
        {
            LOG_W("%s mqtt is not connect!", __FUNCTION__);
            
            /* 等待一段时间后重试 */
            goto __wait_and_retry;
        }
        
        /* 上报数据 */
        ret = app_data_report();
        if (ret < 0)
        { // 上报失败
            LOG_E("%s app_data_report failed(%d)!", __FUNCTION__, ret);
            
            /* 等待一段时间后重试 */
            goto __wait_and_retry;
        }
        else if (ret == 1)
        { // 已到达队列头部(队列上报已全部完成)
            LOG_D("%s report history data completed.", __FUNCTION__);
            continue;
        }
        // else (ret == RT_EOK)
        
        LOG_I("%s send data success, wait the server ACK...", __FUNCTION__);
        
        /* 发送成功,等待服务器ACK */
        ret =  rt_event_recv(data_report_event, DATA_REPORT_EVENT_ACK, 
            RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, ack_time_out, &recved_event);
        if (ret == -RT_ETIMEOUT)
        { // 超时
            LOG_W("%s rt_event_recv(DATA_REPORT_EVENT_ACK) timeout!", __FUNCTION__);
            
            /* 等待一段时间后重试 */
            goto __wait_and_retry;
        }
        else if (ret != RT_EOK)
        {
            LOG_E("%s rt_event_recv failed(%d), quit!", __FUNCTION__, ret);
            break;
        }
        else // if (ret == RT_EOK)
        {
            /* 上报成功 */
            LOG_I("%s recv the server ACK success.", __FUNCTION__);
            
            /* 清零上报失败连续次数 */
            u32ReportFailCnt = 0;
        
            /* 移动待上报数据位置到下一个位置 */
            ret = move_report_pos_to_next();
            if (ret < 0)
            {
                LOG_W("%s move_report_pos_to_next failed!", __FUNCTION__);
                /* 移动位置失败,后续将继续上报本条数据! */
            }
            else if (ret == 1)
            { // 已到达队列头部(队列上报已全部完成)
                LOG_D("%s report history data completed.", __FUNCTION__);
                continue;
            }
            // else (ret == RT_EOK)
            
            /* 继续请求上报数据 */
            ret = req_data_report();
            if (ret != RT_EOK)
            {
                LOG_W("%s req_data_report failed!", __FUNCTION__);
                /* 继续请求上报数据失败,将在下次上报定时器到期时启动上报! */
            }
            continue;
        }
        
__wait_and_retry:
        /* 递减重试次数 */
        --retry_count;
        
        if (retry_count >= 0)
        {
            LOG_W("%s wait(%ds) and retry(%d)!", __FUNCTION__, 
                DATA_REPORT_WAIT_CONNECT_TIME, DATA_REPORT_MAX_RERTY_CNT - retry_count);
            
            /* 等待一段时间后重试 */
            rt_thread_delay(rt_tick_from_millisecond(DATA_REPORT_WAIT_CONNECT_TIME * 1000));
            goto __retry;
        }
        else    
        { // 已达到最大重试次数, 放弃
            LOG_W("%s retch max retry count(%d), give up!", __FUNCTION__, DATA_REPORT_MAX_RERTY_CNT);

            /* 累加上报失败连续次数 */
            ++u32ReportFailCnt;
            
            if (u32ReportFailCnt > REPORT_FAIL_MAX_CNT)
            { // 上报失败次数超过最大允许值
                /* 请求重启系统 */
                req_restart();
            }
        }
    }
    

    data_report_thread = RT_NULL;
}

/* 初始化并启动数据上报线程 */
static rt_err_t data_report_thread_init(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    rt_err_t ret = RT_EOK;
    
    /* 数据上报Event对象 */
    data_report_event = rt_event_create("data_report", RT_IPC_FLAG_FIFO);
    if (data_report_event == RT_NULL)
    {
        LOG_E("%s rt_event_create(data_report) failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    /* 创建数据上报线程 */
    data_report_thread = rt_thread_create("data_report", data_report_thread_entry, 
        (void*)RT_NULL, DATA_REPORT_THREAD_STACK_SIZE, DATA_REPORT_THREAD_PRIORITY, 10);
    if (data_report_thread == RT_NULL)
    {
        LOG_E("%s rt_thread_create(data_report) failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    ret = rt_thread_startup(data_report_thread);
    if (ret != RT_EOK)
    {
        LOG_E("%s rt_thread_startup(data_report) failed!", __FUNCTION__);
        data_report_thread = RT_NULL;
        ret = -RT_ERROR;
        goto __exit;
    }
    
    ret = RT_EOK;
    
__exit:
    if (ret != RT_EOK)
    {
        if (data_report_event)
        {
            rt_event_delete(data_report_event);
            data_report_event = RT_NULL;
        }
    }
    return ret;
}

static rt_err_t app_init(void)
{
    LOG_D("%s()", __FUNCTION__);
    config_info *cfg = NULL;
    rt_err_t ret = RT_EOK;
    
    /* 输出软/硬件版本号 */
    LOG_D("%s sw_version: %s", __FUNCTION__, SW_VERSION);
    LOG_D("%s hw_version: %s", __FUNCTION__, HW_VERSION);
    
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
            
            /* 加载最小配置(最小配置只保证系统能正常启动并连上服务器,以便可以执行远程升级或者诊断) */
            cfg_load_minimum();
            
            ret = RT_EOK;
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
    
    /* 创建主循环消息队列 */
    app_msg_queue = rt_mq_create("app_mq", sizeof(app_message), APP_MSG_QUEUE_LEN, RT_IPC_FLAG_FIFO);
    if (RT_NULL == app_msg_queue)
    {
        LOG_E("%s create app message queue failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    /* 创建APP内存池 */
    app_mp = rt_mp_create("app_mp", APP_MP_BLOCK_NUM, APP_MP_BLOCK_SIZE);
    if (RT_NULL == app_mp)
    {
        LOG_E("%s create app memory pool failed!", __FUNCTION__);
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
    
    /* create mqtt offline check timer */
    mqtt_offline_check_timer = rt_timer_create("offline check", mqtt_offline_check_timer_timeout, 
                            RT_NULL, rt_tick_from_millisecond(MQTT_OFFLINE_TIMEOUT * 1000), 
                            RT_TIMER_FLAG_ONE_SHOT);
    if (RT_NULL == mqtt_offline_check_timer)
    {
        LOG_E("%s create mqtt offline check timer failed!", __FUNCTION__);
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
    
    /* 初始化历史数据存取模块 */
    ret = history_data_init(HISTORY_DATA_PARTITION);
    if (ret != RT_EOK)
    {
        LOG_E("%s history_data_init(%s) failed(%d)!", __FUNCTION__, HISTORY_DATA_PARTITION, ret);
        //ret = -RT_ERROR;
        goto __exit;
    }
    
    /* 初始化MODBUS */
    ret = app_modbus_init();
    if (ret != RT_EOK)
    {
        LOG_E("%s app_modbus_init failed(%d)!", __FUNCTION__, ret);
        //ret = -RT_ERROR;
        goto __exit;
    }
    
    /* 初始化并启动数据上报线程 */
    ret = data_report_thread_init();
    if (ret != RT_EOK)
    {
        LOG_E("%s data_report_thread_init failed(%d)!", __FUNCTION__, ret);
        //ret = -RT_ERROR;
        goto __exit;
    }
    
    /* ESP32模块初始化 */
    ret = at_esp32_init();
    if (ret != RT_EOK)
    {
        LOG_E("%s at_esp32_init failed(%d)!", __FUNCTION__, ret);
        //ret = -RT_ERROR;
        //goto __exit; // 如果ESP32 AT失效,重启系统也无法恢复
        ret = RT_EOK;
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
            rt_snprintf(mqtt_server_url, sizeof(mqtt_server_url), "tcp://%s:%u", cfg->a_ip, cfg->a_port);
            //rt_snprintf(mqtt_server_url, sizeof(mqtt_server_url), "tcp://mq.tongxinmao.com:18830");
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
        mq_client.connect_timeout = 90; // 连接超时，以秒为单位
        mq_client.reconnect_interval = 5; // 重新连接间隔，以秒为单位
        mq_client.msg_timeout = 90; // 消息通信超时，以秒为单位，根据网络情况，不能为0
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
        
        if (RT_NULL != app_mp)
        {
            rt_mp_delete(app_mp);
            app_mp = RT_NULL;
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
        if (RT_NULL != mqtt_offline_check_timer)
        {
            rt_timer_delete(mqtt_offline_check_timer);
            mqtt_offline_check_timer = RT_NULL;
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

/* 请求采集数据 */
rt_err_t req_data_acquisition(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    return app_send_msg(APP_MSG_DATA_ACQUISITION_REQ, RT_NULL); // 发送数据采集请求
}

/* 请求重启系统 */
rt_err_t req_restart(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    return app_send_msg(APP_MSG_RESTART_REQ, RT_NULL); // 发送重启系统请求
}

/* 请求上报数据 */
rt_err_t req_data_report(void)
{
    LOG_D("%s()", __FUNCTION__);
    
#if 0 /* 即使暂时没有连接到MQTT服务器,也要发送请求,由数据上报线程决定如何处理 */
    /* 检查是否已连接到MQTT服务器 */
    if (!paho_mqtt_is_connected(&mq_client))
    {
        LOG_E("%s mqtt is not connect!", __FUNCTION__);
        return -RT_ERROR;
    }
#endif
    
    /* 发送数据上报请求 */
    rt_err_t ret = rt_event_send(data_report_event, DATA_REPORT_EVENT_REQ);
    if (ret != RT_EOK)
    {
        LOG_E("%s rt_event_send(DATA_REPORT_EVENT_REQ) failed(%d)!", __FUNCTION__, ret);
    }
    
    return ret;
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
    
    /* 启动定时采集 */
    ret = rt_timer_start(acquisition_timer);
    if (RT_EOK != ret)
    {
        LOG_E("%s start acquisition timer failed(%d)!", __FUNCTION__, ret);
        goto __exit;
    }
    
    /* 启动定时上报 */
    ret = rt_timer_start(report_timer);
    if (RT_EOK != ret)
    {
        LOG_E("%s start report timer failed(%d)!", __FUNCTION__, ret);
        goto __exit;
    }
    
    /* 启动AT Server */
    LOG_I("%s at_server_init() start", __FUNCTION__);
    ret = at_server_init();
    if (RT_EOK != ret)
    {
        LOG_E("%s at_server_init failed(%d)!", __FUNCTION__, ret);
        goto __exit;
    }
    LOG_I("%s at_server_init() success", __FUNCTION__);
    
    at_server_printfln("ready");
    
    /* 启动开门狗 */
    ret = wdt_start(APP_WDT_TIMEOUT);
    if (RT_EOK != ret)
    { // 出现严重错误
        LOG_E("%s wdt_start(%u) failed(%d)!", __FUNCTION__, APP_WDT_TIMEOUT, ret);
        goto __exit;
    }
    
    /* 启动MQTT离线检查定时器 */
    ret = rt_timer_start(mqtt_offline_check_timer);
    if (RT_EOK != ret)
    { // 出现严重错误
        LOG_E("%s rt_timer_start(mqtt_offline_check_timer) failed(%d)!", __FUNCTION__, ret);
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
            case APP_MSG_DATA_ACQUISITION_REQ: // 数据采集请求
            {
                /* 采集并保存数据 */
                data_acquisition_and_save();
                break;
            }
            case APP_MSG_MQTT_PUBLISH_REQ: // MQTT发布数据请求
            {
                mqtt_publish_data_info *pub_info  = 
                    (mqtt_publish_data_info*)app_msg.msg_data;
                if (pub_info == RT_NULL)
                {
                    LOG_E("%s recv mqtt_publish_data_info is empty!", __FUNCTION__);
                    break;
                }
                
                if ((pub_info->topic != RT_NULL) 
                    && (pub_info->payload != RT_NULL)
                    && (pub_info->length > 0))
                {
                    /* 发布MQTT数据 */
                    int mqtt_ret = paho_mqtt_publish(&mq_client, QOS1, pub_info->topic, 
                        pub_info->payload, pub_info->length);
                    if (mqtt_ret != PAHO_SUCCESS)
                    {
                        LOG_E("%s mqtt publish(%s) error(%d)!", __FUNCTION__, pub_info->topic, ret);
                    }
                }
                else
                {
                    LOG_E("%s mqtt publish topic or data is empty!", __FUNCTION__);
                }
                
                /* 释放内存 */
                mqtt_free_publish_data_info(pub_info);
                break;
            }
            case APP_MSG_MQTT_CLIENT_ONLINE: // MQTT客户端已上线
            {
                /* 停止MQTT离线检查定时器 */
                rt_timer_stop(mqtt_offline_check_timer);
                
                /* 检查是否成功进行了OTA,并上报进度信息 */
                check_and_report_ota_process();
                break;
            }
            case APP_MSG_MQTT_CLIENT_OFFLINE: // MQTT客户端已离线
            {
                /* 启动MQTT离线检查定时器 */
                rt_timer_start(mqtt_offline_check_timer);
                break;
            }
            case APP_MSG_RESTART_REQ: // 重启系统请求
            {
                /* 重启系统 */
                rt_hw_cpu_reset();
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
    /* 停止定时上报 */
    if (report_timer)
    {
        ret = rt_timer_stop(report_timer);
        if (RT_EOK != ret)
        {
            LOG_E("%s stop report timer failed(%d)!", __FUNCTION__, ret);
        }
    }
                
    /* 停止定时采集 */
    if (acquisition_timer)
    {
        ret = rt_timer_stop(acquisition_timer);
        if (RT_EOK != ret)
        {
            LOG_E("%s stop acquisition timer failed(%d)!", __FUNCTION__, ret);
        }
    }
    
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
