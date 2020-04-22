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
#include <mqttclient.h>
#include <errno.h>
#include <jsmn.h>
#include <at_device.h>
#include "config.h"
#include "common.h"
#include "util.h"
#include "strref.h"

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

/* 历史数据保存的最大条数 */
#define HISTORY_DATA_MAX_NUM (4096)

/* 上报数据用的JSON数据缓冲区 */
#define JSON_DATA_BUF_LEN (1024)

/* MQTT订阅主题名缓冲区长度 */
#define MQTT_TOPIC_BUF_LEN (64)

/* 历史数据FIFO队列信息(头部插入、尾部删除) */
typedef struct
{
    uint32_t length; // 队列长度
    uint32_t head_pos; // 头部位置
    uint32_t tail_pos; // 尾部位置
} history_fifo_info;

/* 用于保护历史数据FIFO队列信息的并发访问 */
static rt_mutex_t history_fifo_mutex = RT_NULL;

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

/* MQTT订阅主题名(必须分配具有长生命周期的缓冲区) */
char topic_telemetry_get[MQTT_TOPIC_BUF_LEN] = "";
char topic_config_get[MQTT_TOPIC_BUF_LEN] = "";
char topic_config_set[MQTT_TOPIC_BUF_LEN] = "";
char topic_telemetry_result[MQTT_TOPIC_BUF_LEN] = "";

static rt_err_t app_send_event(rt_uint32_t event_set)
{
    rt_err_t ret = rt_event_send(app_event, event_set);
    if (ret != RT_EOK)
    {
        LOG_E("app_send_event() call rt_event_send error(%d)", ret);
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
static uint32_t get_clientid()
{
    config_info *cfg = cfg_get();
    return cfg->client_id;
}

/* 取得产品编号 */
static const char* get_productkey()
{
    config_info *cfg = cfg_get();
    if (cfg->productkey == NULL)
    {
        return "productKey";
    }
    return cfg->productkey;
}

/* 取得设备ID */
static const char* get_deviceid()
{
    config_info *cfg = cfg_get();
    if (cfg->deviceid == NULL)
    {
        return "deviceId";
    }
    return cfg->deviceid;
}

/* 取得标签ID */
static const char* get_itemid()
{
    //config_info *cfg = cfg_get();
    //return cfg->itemid;
    return ""; // TODO
}

/* 采集UARTX数据 */
static uint32_t uart_x_data_acquisition(int x, uint8_t *data_buf, uint32_t buf_len)
{
    LOG_D("uart_x_data_acquisition(UART%d)", x);
    
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
        mb_ctx = modbus_new_rtu(RS485_1_DEVICE_NAME, 
            cfg->uart_x_cfg[index].baudrate, 
            parity_name[cfg->uart_x_cfg[index].parity], 
            cfg->uart_x_cfg[index].wordlength, 
            cfg->uart_x_cfg[index].stopbits);
        if (mb_ctx == NULL)
        {
            LOG_E("modbus_new_rtu error");
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
            
            /* 保存到采集数据缓存 */
            int j = 0;
            for (j = 0; j < length; ++j)
            {
                uint16_t data = read_buf[i];
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
static rt_err_t data_acquisition_and_save()
{
    LOG_D("data_acquisition_and_save()");
    
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
        LOG_W("ef_get_env_blob(history_fifo_info) load fail, create new!");
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
        snprintf(data_key, sizeof(data_key) - 1, "u%dd%u", x, pos); // Key="uXdN"
        EfErrCode ef_ret = ef_set_env_blob(data_key, data_buf, data_len);
        if (ret != EF_NO_ERR)
        { // 保存失败
            /* 输出警告 */
            LOG_W("ef_set_env_blob(%s) error!", data_key);
            /* 继续采集其他总线的数据 */
        }
    }
    
    /* 保存时间戳 */
    snprintf(data_key, sizeof(data_key) - 1, "d%uts", pos); // Key="dNts"
    EfErrCode ef_ret = ef_set_env_blob(data_key, &now, sizeof(now));
    if (ret != EF_NO_ERR)
    {
        LOG_E("ef_set_env_blob(%s) error!", data_key);
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
        LOG_E("ef_set_env_blob(history_fifo_info) error!");
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
static uint32_t read_history_pos_data_json(uint32_t read_pos, char* json_data_buf, uint32_t json_buf_len)
{
    LOG_D("read_history_pos_data_json(%u)", read_pos);
    
    /* 缓冲区中已读取数据字节数 */
    uint32_t json_data_len = 0;
    
    /* 配置信息 */
    config_info *cfg = cfg_get();
    
    char data_key[16] = "";
    
    /* 读取时间戳 */
    snprintf(data_key, sizeof(data_key) - 1, "d%uts", read_pos); // Key="dNts"
    time_t time_stamp = 0;
    size_t len = ef_get_env_blob(data_key, &time_stamp, sizeof(time_stamp), NULL);
    if (len != sizeof(time_stamp))
    {
        LOG_E("ef_get_env_blob(%s) error!", data_key);
        return 0;
    }
    /* 时间戳编码成JSON格式并写入缓冲区 */
    struct tm* local_time = localtime(&time_stamp);
    json_data_len = rt_snprintf(json_data_buf, json_buf_len, "{\"ts\":\"%04d%02d%02d%02d%02d%02d\",", 
        (local_time->tm_year + 1900), (local_time->tm_mon + 1), local_time->tm_mday,
        local_time->tm_hour, local_time->tm_min, local_time->tm_sec);
    
    /* 读取UARTX数据 */
    int x = 1;
    for (x = 1; x < CFG_UART_X_NUM; ++x)
    {
        /* 读取保存的历史数据 */
        uint8_t data_buf[128] = {0};
        snprintf(data_key, sizeof(data_key) - 1, "u%dd%u", x, read_pos); // Key="uXdN"
        len = ef_get_env_blob(data_key, data_buf, sizeof(data_buf), NULL);
        if (len != sizeof(data_buf))
        {
            LOG_E("ef_get_env_blob(%s) error!", data_key);
            return 0;
        }
        
        /* 转换成JSON格式并写入数据缓冲区 */
        json_data_len += rt_snprintf(json_data_buf + json_data_len, 
            json_buf_len - json_data_len, "\"u%d\":{", x); // 分组名
        uint32_t data_read_pos = 0; // 变量采集值的读取位置
        int i = 0;
        for (i = 0; i < cfg->uart_x_cfg[x - 1].variablecnt; ++i)
        {
            /* 每个寄存器16bit */
            uint16_t reg_num = cfg->uart_x_cfg[x - 1].length[i]; // 寄存器个数
            uint16_t data_bytes = reg_num * sizeof(uint16_t); // 字节数
            
            char hex_str_buf[128] = "";
            util_to_hex_str(data_buf + data_read_pos, // 取得变量对应的采集值
                data_bytes, hex_str_buf, sizeof(hex_str_buf)); // 转换成HEX字符串格式
            json_data_len += rt_snprintf(json_data_buf + json_data_len, json_buf_len - json_data_len, 
                "\"%s\":\"%s\",", cfg->uart_x_cfg[x - 1].variable[i], hex_str_buf);
            
            data_read_pos += data_bytes; // 指向下一个采集值起始
        }
        if (json_data_len < json_buf_len)
        {
            json_data_buf[json_data_len - 1] = '}'; // 末尾','改成'}'
            json_data_buf[json_data_len++] = ','; // 添加','
        }
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
uint32_t read_history_data_json(uint32_t n, char* json_data_buf, uint32_t json_buf_len)
{
    LOG_D("read_history_data_json(%u)", n);
    
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
        LOG_E("ef_get_env_blob(history_fifo_info) load fail!");
        return 0;
    }
    
    /* 状态检查 */
    if (fifo_info.length <= 0)
    { // 队列为空
        LOG_E("history data is empty!");
        return 0;
    }
    
    /* 范围检查 */
    if (n >= fifo_info.length)
    {
        /* 超范围 */
        LOG_E("n(%u) not in range[0,%u]!", n, fifo_info.length - 1);
        return 0;
    }
    
    /* 读取前第n条历史数据 */
    uint32_t read_pos = fifo_info.head_pos;
    if (fifo_info.head_pos >= n)
    {
        read_pos = fifo_info.head_pos - n;
    }
    else
    {
        read_pos = HISTORY_DATA_MAX_NUM - n;
    }
    
    /* 读取read_pos处的一条历史数据(JSON格式) */
    json_data_len = read_history_pos_data_json(read_pos, json_data_buf, json_buf_len);
    
    return json_data_len;
}

/* 
 * 清空历史数据
 */
rt_err_t clear_history_data(void)
{
    LOG_D("clear_history_data()");

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
        LOG_E("ef_set_env_blob(history_fifo_info) error(%d)!", ef_ret);
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
        LOG_E("ef_get_env_blob(history_fifo_info) load fail!");
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
    config_info *cfg = cfg_get();
    char a_ip[32] = "";
    
    inet_ntoa_r(cfg->a_ip, a_ip, sizeof(a_ip));
    char b_ip[32] = "";
    
    inet_ntoa_r(cfg->b_ip, b_ip, sizeof(b_ip));
    int rssi = 0;
    
    get_modem_rssi(&rssi);
    
    rt_int32_t json_data_len = rt_snprintf(json_data_buf, json_buf_len, 
        "{\"CycleSet\":\"%u\",\"AcquisitionSet\":\"%u\",\"AutoControlSet\":\"0\","
        "\"AIPSet\":\"%s\",\"APortSet\":\"%u\",\"BIPSet\":\"%s\",\"BPortSet\":\"%u\","
        "\"Rssi\":\"%d\",\"version\":\"%s\"}", cfg->cycle, cfg->acquisition, 
        a_ip, cfg->a_port, b_ip, cfg->b_port,  rssi, SW_VERSION);
    
    return (uint32_t)json_data_len;
}

/* 
 * 修改配置参数(JSON格式) 
 *
 */
static int set_config_info(c_str_ref *cycle, c_str_ref *acquisition, c_str_ref *autocontrol, 
    c_str_ref *a_ip, c_str_ref *a_port, c_str_ref *b_ip, c_str_ref *b_port, c_str_ref *restart)
{
    /* 参数检查和转换 */
    
    /* cycle[可缺省] */
    int cycle_val = 0;
    if (!strref_is_empty(cycle))
    {
        if (!strref_is_int(cycle))
        {
            LOG_E("<cycle> not interger!");
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
        cycle_val = strref_to_int(cycle);
        if ((cycle_val < 1) || (cycle_val > 180))
        {
            LOG_E("<cycle>(%d) not in range[1,180]!", cycle_val);
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
            LOG_E("<acquisition> not interger!");
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
        acquisition_val = strref_to_int(acquisition);
        if ((acquisition_val < 1) || (acquisition_val > 30))
        {
            LOG_E("<acquisition>(%d) not in range[1,30]!", acquisition_val);
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
            LOG_E("<autocontrol> not interger!");
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
        autocontrol_val = strref_to_int(autocontrol);
        if ((autocontrol_val < 0) || (autocontrol_val > 1))
        {
            LOG_E("<autocontrol>(%d) not in range[0,1]!", autocontrol_val);
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
            LOG_E("inet_addr(%s) error!", a_ip_str);
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
            LOG_E("<a_port> not interger!");
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
        a_port_val = strref_to_int(a_port);
        if ((a_port_val < 0) || (a_port_val > 65535))
        {
            LOG_E("<a_port>(%d) not in range[0,65535]!", a_port_val);
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
            LOG_E("inet_addr(%s) error!", b_ip_str);
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
            LOG_E("<b_port> not interger!");
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
        b_port_val = strref_to_int(b_port);
        if ((b_port_val < 0) || (b_port_val > 65535))
        {
            LOG_E("<a_port>(%d) not in range[0,65535]!", b_port_val);
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
            LOG_E("<restart> not interger!");
            /* 420 request parameter error 请求参数错误， 设备入参校验失败 */
            return 420;
        }
        restart_val = strref_to_int(restart);
        if ((restart_val < 0) || (restart_val > 1))
        {
            LOG_E("<restart>(%d) not in range[0,1]!", restart_val);
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
            LOG_E("ef_set_env_blob(cycle,0x%x) error(%d)!", cycle_val, ef_ret);
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
            LOG_E("ef_set_env_blob(acquisition,0x%x) error(%d)!", acquisition_val, ef_ret);
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
            LOG_E("ef_set_env_blob(a_ip,0x%x) error(%d)!", a_ip_addr, ef_ret);
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
            LOG_E("ef_set_env_blob(a_port,0x%x) error(%d)!", a_port_val, ef_ret);
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
            LOG_E("ef_set_env_blob(b_ip,0x%x) error(%d)!", b_ip_addr, ef_ret);
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
            LOG_E("ef_set_env_blob(b_port,0x%x) error(%d)!", b_port_val, ef_ret);
            /* 500 error 系统内部异常 */
            return 500; // TODO恢复已成功的部分设置
        }
    }
    
    /* 200 success 请求成功 */
    return 200;
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
    
    if (RT_NULL != history_fifo_mutex)
    {
        rt_mutex_delete(history_fifo_mutex);
        history_fifo_mutex = RT_NULL;
    }
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
    
    /* 根据保存的配置来设置ULOG全局日志level */
    {
        ulog_global_filter_lvl_set(cfg->ulog_glb_lvl);
    }
    
    /* create history fifo mutex */
    history_fifo_mutex = rt_mutex_create("history_fifo", RT_IPC_FLAG_FIFO);
    if (RT_NULL == history_fifo_mutex)
    {
        LOG_E("create history fifo mutex failed!");
        ret = -RT_ERROR;
        goto __exit;
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
        rt_snprintf(mqtt_client_id, sizeof(mqtt_client_id), "rtu_%d", rt_tick_get());
        //rt_snprintf(mqtt_client_id, sizeof(mqtt_client_id), "%010u", get_clientid());
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
    if (RT_NULL != history_fifo_mutex)
    {
        rt_mutex_delete(history_fifo_mutex);
        history_fifo_mutex = RT_NULL;
    }
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

static void topic_telemetry_get_handler(void* client, message_data_t* msg)
{
    (void) client;
    const char *json_str = (const char*)(msg->message->payload);
    size_t json_str_len = msg->message->payloadlen;
    c_str_ref productkey = {0, NULL}; // productKey
    c_str_ref deviceid = {0, NULL}; // deviceId
    c_str_ref operationdate = {0, NULL}; // OperationDate
    c_str_ref id = {0, NULL}; // id
    char* json_data_buf = NULL;
    
    LOG_I("%s %s", msg->topic_name, json_str);
    
    /* 解析JSON字符串 */
    jsmn_parser json_paser;
    jsmn_init(&json_paser);
    jsmntok_t tok_list[20];
    int list_len = jsmn_parse(&json_paser, json_str, json_str_len, tok_list, ARRAY_SIZE(tok_list));
    int i = 0;
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
            else if ((token_str_len == STR_LEN("deviceId")) 
				&& (0 == memcmp(token_str, "deviceId", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                deviceid.c_str = json_str + tok_list[i].start;
                deviceid.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("OperationDate")) 
				&& (0 == memcmp(token_str, "OperationDate", token_str_len)))
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
    
    /* 检查productKey和deviceId */
    if (strref_str_cmp(get_productkey(), &productkey) != 0)
    {
        LOG_E("productkey check failed!");
        goto __exit;
    }
    if (strref_str_cmp(get_deviceid(), &deviceid) != 0)
    {
        LOG_E("deviceid check failed!");
        goto __exit;
    }
    
    json_data_buf = (char*)rt_malloc(JSON_DATA_BUF_LEN);
    if (json_data_buf == NULL)
    {
        LOG_E("rt_malloc(%d) failed!", JSON_DATA_BUF_LEN);
        goto __exit;
    }
    
    /* 编码JSON采集数据并发送 */
    {
        rt_int32_t json_data_len = rt_snprintf(json_data_buf, JSON_DATA_BUF_LEN, 
            "{\"productKey\":\"%s\",\"deviceId\":\"%s\",\"clientId\":\"%010u\",\"itemId\":\"%s\",\"requestId\":\"%.*s\",\"data\":", 
             get_productkey(), get_deviceid(), get_clientid(), get_itemid(), id.len, id.c_str);
        
        /* 读取最新采集的数据(JSON格式)  */
        uint32_t read_len = read_history_data_json(0, json_data_buf + json_data_len, JSON_DATA_BUF_LEN - json_data_len);
        if (read_len <= 0)
        {
            LOG_E("read_history_data_json(read_len=%d) failed!", read_len);
            goto __exit;
        }
        json_data_len += read_len;
        RT_ASSERT(json_data_len < JSON_DATA_BUF_LEN);
        json_data_buf[json_data_len++] = '}';
        RT_ASSERT(json_data_len < JSON_DATA_BUF_LEN);
        json_data_buf[json_data_len] = '\0';
        
        /* 发布/sys/${productKey}/${deviceId}/telemetry/get_reply响应 */
        {
            mqtt_message_t msg;
            memset(&msg, 0, sizeof(msg));
            msg.qos = QOS1;
            msg.payload = (void*)json_data_buf;

            char topic_buf[128] = "";
            rt_snprintf(topic_buf, sizeof(topic_buf) - 1, "/sys/%s/%s/telemetry/get_reply", get_productkey(), get_deviceid());
            LOG_D("publish(%s) %s", topic_buf, json_data_buf);
            int ret = mqtt_publish(&mq_client, topic_buf, &msg);
            if (ret != MQTT_SUCCESS_ERROR)
            {
                LOG_E("mqtt publish(%s) error(%d)", topic_buf, ret);
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
}

static void topic_config_get_handler(void* client, message_data_t* msg)
{
    (void) client;
    const char *json_str = (const char*)(msg->message->payload);
    size_t json_str_len = msg->message->payloadlen;
    c_str_ref productkey = {0, NULL}; // productKey
    c_str_ref deviceid = {0, NULL}; // deviceId
    c_str_ref operationdate = {0, NULL}; // OperationDate
    c_str_ref id = {0, NULL}; // id
    char* json_data_buf = NULL;
    
    LOG_I("%s %s", msg->topic_name, json_str);
    
    /* 解析JSON字符串 */
    jsmn_parser json_paser;
    jsmn_init(&json_paser);
    jsmntok_t tok_list[20];
    int list_len = jsmn_parse(&json_paser, json_str, json_str_len, tok_list, ARRAY_SIZE(tok_list));
    int i = 0;
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
            else if ((token_str_len == STR_LEN("deviceId")) 
				&& (0 == memcmp(token_str, "deviceId", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                deviceid.c_str = json_str + tok_list[i].start;
                deviceid.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("OperationDate")) 
				&& (0 == memcmp(token_str, "OperationDate", token_str_len)))
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
    
    /* 检查productKey和deviceId */
    if (strref_str_cmp(get_productkey(), &productkey) != 0)
    {
        LOG_E("productkey check failed!");
        goto __exit;
    }
    if (strref_str_cmp(get_deviceid(), &deviceid) != 0)
    {
        LOG_E("deviceid check failed!");
        goto __exit;
    }
    
    json_data_buf = (char*)rt_malloc(JSON_DATA_BUF_LEN);
    if (json_data_buf == NULL)
    {
        LOG_E("rt_malloc(%d) failed!", JSON_DATA_BUF_LEN);
        goto __exit;
    }
    
    /* 编码JSON配置信息并发送 */
    {
        rt_int32_t json_data_len = rt_snprintf(json_data_buf, JSON_DATA_BUF_LEN, 
            "{\"Status\":\"200\",\"productKey\":\"%s\",\"deviceId\":\"%s\",\"requestId\":\"%.*s\",\"data\":", 
             get_productkey(), get_deviceid(), id.len, id.c_str);
        
        /* 读取配置信息(JSON格式)  */
        uint32_t read_len = read_config_info_json(json_data_buf + json_data_len, JSON_DATA_BUF_LEN - json_data_len);
        if (read_len <= 0)
        {
            LOG_E("read_config_info_json(read_len=%d) failed!", read_len);
            
            json_data_len = rt_snprintf(json_data_buf, JSON_DATA_BUF_LEN, 
                "{\"Status\":\"500\",\"productKey\":\"%s\",\"deviceId\":\"%s\",\"requestId\":\"%.*s\"}", 
                 get_productkey(), get_deviceid(), id.len, id.c_str);
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
        
        /* 发布/sys/${productKey}/${deviceId}/config/get_reply响应 */
        mqtt_message_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.qos = QOS1;
        msg.payload = (void*)json_data_buf;

        char topic_buf[128] = "";
        rt_snprintf(topic_buf, sizeof(topic_buf) - 1, "/sys/%s/%s/config/get_reply", get_productkey(), get_deviceid());
        LOG_D("publish(%s) %s", topic_buf, json_data_buf);
        int ret = mqtt_publish(&mq_client, topic_buf, &msg);
        if (ret != MQTT_SUCCESS_ERROR)
        {
            LOG_E("mqtt publish(%s) error(%d)", topic_buf, ret);
            goto __exit;
        }
    }
    
__exit:
    if (json_data_buf != NULL)
    {
        rt_free(json_data_buf);
        json_data_buf = NULL;
    }
}

static void topic_config_set_handler(void* client, message_data_t* msg)
{
    (void) client;
    const char *json_str = (const char*)(msg->message->payload);
    size_t json_str_len = msg->message->payloadlen;
    c_str_ref productkey = {0, NULL}; // productKey
    c_str_ref deviceid = {0, NULL}; // deviceId
    c_str_ref operationdate = {0, NULL}; // OperationDate
    c_str_ref id = {0, NULL}; // id
    c_str_ref cycle = {0, NULL}; // CycleSet
    c_str_ref acquisition = {0, NULL}; // AcquisitionSet
    c_str_ref autocontrol = {0, NULL}; // AutoControlSet
    c_str_ref a_ip = {0, NULL}; // AIPSet
    c_str_ref a_port = {0, NULL}; // APortSet
    c_str_ref b_ip = {0, NULL}; // AIPSet
    c_str_ref b_port = {0, NULL}; // APortSet
    c_str_ref restart = {0, NULL}; // Restart
    char* json_data_buf = NULL;
    int cfg_ret_code = 0;
    
    LOG_I("%s %s", msg->topic_name, json_str);
    
    /* 解析JSON字符串 */
    jsmn_parser json_paser;
    jsmn_init(&json_paser);
    jsmntok_t tok_list[20];
    int list_len = jsmn_parse(&json_paser, json_str, json_str_len, tok_list, ARRAY_SIZE(tok_list));
    int i = 0;
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
            else if ((token_str_len == STR_LEN("deviceId")) 
				&& (0 == memcmp(token_str, "deviceId", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                deviceid.c_str = json_str + tok_list[i].start;
                deviceid.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("OperationDate")) 
				&& (0 == memcmp(token_str, "OperationDate", token_str_len)))
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
            else if ((token_str_len == STR_LEN("CycleSet")) 
				&& (0 == memcmp(token_str, "CycleSet", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                cycle.c_str = json_str + tok_list[i].start;
                cycle.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("AcquisitionSet")) 
				&& (0 == memcmp(token_str, "AcquisitionSet", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                acquisition.c_str = json_str + tok_list[i].start;
                acquisition.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("AutoControlSet")) 
				&& (0 == memcmp(token_str, "AutoControlSet", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                autocontrol.c_str = json_str + tok_list[i].start;
                autocontrol.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("AIPSet")) 
				&& (0 == memcmp(token_str, "AIPSet", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                a_ip.c_str = json_str + tok_list[i].start;
                a_ip.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("APortSet")) 
				&& (0 == memcmp(token_str, "APortSet", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                a_port.c_str = json_str + tok_list[i].start;
                a_port.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("BIPSet")) 
				&& (0 == memcmp(token_str, "BIPSet", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                b_ip.c_str = json_str + tok_list[i].start;
                b_ip.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("BPortSet")) 
				&& (0 == memcmp(token_str, "BPortSet", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                b_port.c_str = json_str + tok_list[i].start;
                b_port.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("Restart")) 
				&& (0 == memcmp(token_str, "Restart", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                restart.c_str = json_str + tok_list[i].start;
                restart.len = tok_list[i].end - tok_list[i].start;
			}
		}
    }
    
    /* 检查productKey和deviceId */
    if (strref_str_cmp(get_productkey(), &productkey) != 0)
    {
        LOG_E("productkey check failed!");
        goto __exit;
    }
    if (strref_str_cmp(get_deviceid(), &deviceid) != 0)
    {
        LOG_E("deviceid check failed!");
        goto __exit;
    }
    
    /* 修改配置 */
    cfg_ret_code = set_config_info(&cycle, &acquisition, &autocontrol, 
        &a_ip, &a_port, &b_ip, &b_port, &restart);
    
    json_data_buf = (char*)rt_malloc(JSON_DATA_BUF_LEN);
    if (json_data_buf == NULL)
    {
        LOG_E("rt_malloc(%d) failed!", JSON_DATA_BUF_LEN);
        goto __exit;
    }
    
    /* 编码响应信息并发送 */
    {
        char topic_buf[128] = "";
        rt_snprintf(topic_buf, sizeof(topic_buf) - 1, "/sys/%s/%s/config/set", get_productkey(), get_deviceid());
        rt_int32_t json_data_len = rt_snprintf(json_data_buf, JSON_DATA_BUF_LEN, 
            "{\"productKey\":\"%s\",\"deviceId\":\"%s\",\"OperationDate\":\"%.*s\",\"requestId\":\"%.*s\","
            "\"code\":\"%d\",\"topic\":\"%s\",\"data\":{}}", get_productkey(), get_deviceid(), 
            operationdate.len, operationdate.c_str, id.len, id.c_str, cfg_ret_code, topic_buf);
        RT_ASSERT(json_data_len < JSON_DATA_BUF_LEN);
        json_data_buf[json_data_len] = '\0';
        
        /* 发布/sys/${productKey}/${deviceId}/config/set_reply响应 */
        mqtt_message_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.qos = QOS1;
        msg.payload = (void*)json_data_buf;

        rt_snprintf(topic_buf, sizeof(topic_buf) - 1, "/sys/%s/%s/config/set_reply", get_productkey(), get_deviceid());
        LOG_D("publish(%s) %s", topic_buf, json_data_buf);
        int ret = mqtt_publish(&mq_client, topic_buf, &msg);
        if (ret != MQTT_SUCCESS_ERROR)
        {
            LOG_E("mqtt publish(%s) error(%d)", topic_buf, ret);
            goto __exit;
        }
    }
    
__exit:
    if (json_data_buf != NULL)
    {
        rt_free(json_data_buf);
        json_data_buf = NULL;
    }
    
    if (strref_str_cmp("1", &restart))
    {
        /* 重启 */
        rt_hw_cpu_reset();
    }
}

static void topic_telemetry_result_handler(void* client, message_data_t* msg)
{
    (void) client;
    const char *json_str = (const char*)(msg->message->payload);
    size_t json_str_len = msg->message->payloadlen;
    c_str_ref productkey = {0, NULL}; // productKey
    c_str_ref deviceid = {0, NULL}; // deviceId
    c_str_ref operationdate = {0, NULL}; // OperationDate
    c_str_ref id = {0, NULL}; // id
    c_str_ref code = {0, NULL}; // code
    c_str_ref message = {0, NULL}; // message
    c_str_ref topic = {0, NULL}; // topic
    c_str_ref data = {0, NULL}; // data
    
    LOG_I("%s %s", msg->topic_name, json_str);
    
    /* 解析JSON字符串 */
    jsmn_parser json_paser;
    jsmn_init(&json_paser);
    jsmntok_t tok_list[20];
    int list_len = jsmn_parse(&json_paser, json_str, json_str_len, tok_list, ARRAY_SIZE(tok_list));
    int i = 0;
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
            else if ((token_str_len == STR_LEN("deviceId")) 
				&& (0 == memcmp(token_str, "deviceId", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                deviceid.c_str = json_str + tok_list[i].start;
                deviceid.len = tok_list[i].end - tok_list[i].start;
			}
            else if ((token_str_len == STR_LEN("OperationDate")) 
				&& (0 == memcmp(token_str, "OperationDate", token_str_len)))
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
		}
        else if (JSMN_OBJECT == tok_list[i].type)
        {
            if ((token_str_len == STR_LEN("data")) 
				&& (0 == memcmp(token_str, "data", token_str_len)))
			{
                ++i; // 指向Value
                RT_ASSERT(i < list_len);
                data.c_str = json_str + tok_list[i].start;
                data.len = tok_list[i].end - tok_list[i].start;
			}
        }
    }
    
    /* 检查productKey和deviceId */
    if (strref_str_cmp(get_productkey(), &productkey) != 0)
    {
        LOG_E("productkey check failed!");
        goto __exit;
    }
    if (strref_str_cmp(get_deviceid(), &deviceid) != 0)
    {
        LOG_E("deviceid check failed!");
        goto __exit;
    }
    
    // TODO
    
__exit:
    return;
}

static rt_err_t app_mqtt_connect()
{
    LOG_D("app_mqtt_connect()");
    
    int mqtt_ret = mqtt_connect(&mq_client);
    if (mqtt_ret != MQTT_SUCCESS_ERROR)
    {
        LOG_E("mqtt connect error(%d)", mqtt_ret);
        return -RT_ERROR;
    }
    
    /* 订阅/sys/${productKey}/${deviceId}/telemetry/get主题,接收服务器数据查询指令 */
    rt_snprintf(topic_telemetry_get, sizeof(topic_telemetry_get), "/sys/%s/%s/telemetry/get", get_productkey(), get_deviceid());
    LOG_D("subscribe %s", topic_telemetry_get);
    mqtt_ret = mqtt_subscribe(&mq_client, topic_telemetry_get, QOS1, topic_telemetry_get_handler);
    if (mqtt_ret != MQTT_SUCCESS_ERROR)
    {
        mqtt_disconnect(&mq_client);
        
        LOG_E("mqtt subscribe(%s) error(%d)", topic_telemetry_get, mqtt_ret);
        return -RT_ERROR;
    }
    
    /* 订阅/sys/${productKey}/${deviceId}/config/get主题,接收服务器配置查询指令 */
    rt_snprintf(topic_config_get, sizeof(topic_config_get), "/sys/%s/%s/config/get", get_productkey(), get_deviceid());
    LOG_D("subscribe %s", topic_config_get);
    mqtt_ret = mqtt_subscribe(&mq_client, topic_config_get, QOS1, topic_config_get_handler);
    if (mqtt_ret != MQTT_SUCCESS_ERROR)
    {
        mqtt_disconnect(&mq_client);
        
        LOG_E("mqtt subscribe(%s) error(%d)", topic_config_get, mqtt_ret);
        return -RT_ERROR;
    }
    
    /* 订阅/sys/${productKey}/${deviceId}/config/set主题,接收服务器配置设置指令 */
    rt_snprintf(topic_config_set, sizeof(topic_config_set), "/sys/%s/%s/config/set", get_productkey(), get_deviceid());
    LOG_D("subscribe %s", topic_config_set);
    mqtt_ret = mqtt_subscribe(&mq_client, topic_config_set, QOS1, topic_config_set_handler);
    if (mqtt_ret != MQTT_SUCCESS_ERROR)
    {
        mqtt_disconnect(&mq_client);
        
        LOG_E("mqtt subscribe(%s) error(%d)", topic_config_set, mqtt_ret);
        return -RT_ERROR;
    }
    
    /* 订阅/sys/${productKey}/${deviceId}/telemetry/result主题,接收主动上报服务器响应 */
    rt_snprintf(topic_telemetry_result, sizeof(topic_telemetry_result), "/sys/%s/%s/telemetry/result", get_productkey(), get_deviceid());
    LOG_D("subscribe %s", topic_telemetry_result);
    mqtt_ret = mqtt_subscribe(&mq_client, topic_telemetry_result, QOS1, topic_telemetry_result_handler);
    if (mqtt_ret != MQTT_SUCCESS_ERROR)
    {
        mqtt_disconnect(&mq_client);
        
        LOG_E("mqtt subscribe(%s) error(%d)", topic_telemetry_result, mqtt_ret);
        return -RT_ERROR;
    }
    
    return RT_EOK;
}

/* 上报数据 */
static rt_err_t app_data_report()
{
    LOG_D("app_data_report()");
    
    rt_err_t ret = RT_EOK;

    char* json_data_buf = (char*)rt_malloc(JSON_DATA_BUF_LEN);
    if (json_data_buf == NULL)
    {
        LOG_E("rt_malloc(%d) failed!", JSON_DATA_BUF_LEN);
        ret = -RT_ENOMEM;
        goto __exit;
    }
    
    /* 编码JSON采集数据并发送 */
    {
        rt_int32_t json_data_len = rt_snprintf(json_data_buf, JSON_DATA_BUF_LEN, 
            "{\"productKey\":\"%s\",\"deviceId\":\"%s\",\"clientId\":\"%010u\",\"itemId\":\"%s\",\"data\":", 
             get_productkey(), get_deviceid(), get_clientid(), get_itemid());
        
        /* 读取最新采集的数据(JSON格式)  */
        uint32_t read_len = read_history_data_json(0, json_data_buf + json_data_len, JSON_DATA_BUF_LEN - json_data_len);
        if (read_len <= 0)
        {
            LOG_E("read_history_data_json(read_len=%d) failed!", read_len);
            ret = -RT_ERROR;
            goto __exit;
        }
        json_data_len += read_len;
        RT_ASSERT(json_data_len < JSON_DATA_BUF_LEN);
        json_data_buf[json_data_len++] = '}';
        RT_ASSERT(json_data_len < JSON_DATA_BUF_LEN);
        json_data_buf[json_data_len] = '\0';
        
        /* 发布/sys/${productKey}/${deviceId}/telemetry/real_time_data */
        {
            mqtt_message_t msg;
            memset(&msg, 0, sizeof(msg));
            msg.qos = QOS1;
            msg.payload = (void*)json_data_buf;

            char topic_buf[128] = "";
            rt_snprintf(topic_buf, sizeof(topic_buf) - 1, "/sys/%s/%s/telemetry/real_time_data", get_productkey(), get_deviceid());
            LOG_D("publish(%s) %s", topic_buf, json_data_buf);
            int ret = mqtt_publish(&mq_client, topic_buf, &msg);
            if (ret != MQTT_SUCCESS_ERROR)
            {
                LOG_E("mqtt publish(%s) error(%d)", topic_buf, ret);
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
            /* 等待一段时间 */
            rt_thread_delay(rt_tick_from_millisecond(5 * 1000));
            
            /* 连接MQTT服务器 */
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
            /* 采集并保存数据 */
            data_acquisition_and_save();
        }
    }
    
    app_deinit();
    
    return ret;
}
