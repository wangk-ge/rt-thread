/****************************************************************************
 *
 * File Name
 *  config.c
 * Author
 *  
 * Date
 *  2020/04/20
 * Descriptions:
 * 参数配置接口实现
 *
 ******************************************************************************/
/*----------------------------------------------------------------------------*
**                             Dependencies                                   *
**----------------------------------------------------------------------------*/
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <easyflash.h>
#include <netdev_ipaddr.h>
#include "config.h"
#include "common.h"
#include "strref.h"

#define LOG_TAG              "config"
#define LOG_LVL              LOG_LVL_DBG
#include <rtdbg.h>

/**---------------------------------------------------------------------------*
 **                            Debugging Flag                                 *
 **---------------------------------------------------------------------------*/

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
    
/*----------------------------------------------------------------------------*
**                             Data Structures                                *
**----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*
**                             Extern Function                                *
**----------------------------------------------------------------------------*/
extern void ef_get_default_env(ef_env const **default_env, size_t *default_env_size);

/*----------------------------------------------------------------------------*
**                             Local Vars                                     *
**----------------------------------------------------------------------------*/

/* 配置信息缓存 */
static config_info cfg_info = {NULL};

/* 变量名字符串缓存
*
*  变量列表加载到内存后的存储结构如下所示:
*
*    variable:    [0]   [1]   [2]    ... [n]
*                  |     |     |          |
*                  |     |     |          |
*                  V     V     V          V
*    var_str_buf: "var0\0var1\0var2\0...\0varn\0"
*
*/
static char* var_str_buf[ARRAY_SIZE(cfg_info.uart_x_cfg)] = {NULL};

/*----------------------------------------------------------------------------*
**                             Local Function                                 *
**----------------------------------------------------------------------------*/
/*************************************************
* Function: cfg_info_clear
* Description: 清除缓存的配置数据
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
static void cfg_info_clear(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    /* 释放动态分配的内存 */
    int i = 0;
    for (i = 0; i < ARRAY_SIZE(cfg_info.uart_x_cfg); ++i)
    {
        if (var_str_buf[i] != NULL)
        {
            rt_free(var_str_buf[i]);
            var_str_buf[i] = NULL;
        }
        
        if (cfg_info.uart_x_cfg[i].startaddr != NULL)
        {
            rt_free(cfg_info.uart_x_cfg[i].startaddr);
        }
        
        if (cfg_info.uart_x_cfg[i].length != NULL)
        {
            rt_free(cfg_info.uart_x_cfg[i].length);
        }
        
        if (cfg_info.uart_x_cfg[i].variable != NULL)
        {
            rt_free(cfg_info.uart_x_cfg[i].variable);
        }
        
        if (cfg_info.uart_x_cfg[i].slaveraddr != NULL)
        {
            rt_free(cfg_info.uart_x_cfg[i].slaveraddr);
        }
        
        if (cfg_info.uart_x_cfg[i].function != NULL)
        {
            rt_free(cfg_info.uart_x_cfg[i].function);
        }
        
        if (cfg_info.uart_x_cfg[i].type != NULL)
        {
            rt_free(cfg_info.uart_x_cfg[i].type);
        }
    }
    
    if (cfg_info.a_ip != NULL)
    {
        rt_free(cfg_info.a_ip);
    }
    
    if (cfg_info.b_ip != NULL)
    {
        rt_free(cfg_info.b_ip);
    }
    
    if (cfg_info.productkey != NULL)
    {
        rt_free(cfg_info.productkey);
    }
    
    if (cfg_info.devicecode != NULL)
    {
        rt_free(cfg_info.devicecode);
    }
    
    if (cfg_info.itemid != NULL)
    {
        rt_free(cfg_info.itemid);
    }
    
    /* 所有成员清0 */
    memset(&cfg_info, 0, sizeof(cfg_info));
}

/* 计算每条数据的大小(字节数) */
static void calc_data_size(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    cfg_info.data_size = sizeof(time_t);
    
    int x = 1;
    for (x = 1; x <= CFG_UART_X_NUM; ++x)
    {
        int i = 0;
        for (i = 0; i < cfg_info.uart_x_cfg[x - 1].variablecnt; ++i)
        {
            /* 每个寄存器16bit */
            uint16_t reg_num = cfg_info.uart_x_cfg[x - 1].length[i]; // 变量寄存器个数
            cfg_info.data_size += reg_num * sizeof(uint16_t);
        }
    }
}

/*************************************************
* Function: cfg_load_uart_x
* Description: 加载UARTX配置
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
static bool cfg_load_uart_x(int x)
{
    LOG_D("%s() x=%d", __FUNCTION__, x);
    
    /* 内部函数不做参数检查,由调用者保证参数有效性 */
    int index = x - 1; // 索引值
    bool ret = true;
    char cfg_key[32] = "";
    
    /* variablecnt */
    uint8_t variablecnt = 0;
    snprintf(cfg_key, sizeof(cfg_key), "uart%dvariablecnt", x);
    size_t len = ef_get_env_blob(cfg_key, &variablecnt, sizeof(variablecnt), NULL);
    if (len != sizeof(variablecnt))
    {
        LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, cfg_key);
        ret = false;
        goto __exit;
    }
    cfg_info.uart_x_cfg[index].variablecnt = variablecnt;

    if (variablecnt > 0)
    {
        /* startaddr */
        cfg_info.uart_x_cfg[index].startaddr = (uint16_t*)rt_malloc(variablecnt * sizeof(uint16_t));
        if (cfg_info.uart_x_cfg[index].startaddr == NULL)
        {
            LOG_E("%s rt_malloc(%d) failed!", __FUNCTION__, variablecnt * sizeof(uint16_t));
            ret = false;
            goto __exit;
        }
        snprintf(cfg_key, sizeof(cfg_key), "uart%dstartaddr", x);
        len = ef_get_env_blob(cfg_key, cfg_info.uart_x_cfg[index].startaddr, variablecnt * sizeof(uint16_t), NULL);
        if (len != variablecnt * sizeof(uint16_t))
        {
            LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, cfg_key);
            ret = false;
            goto __exit;
        }
        
        /* length */
        cfg_info.uart_x_cfg[index].length = (uint16_t*)rt_malloc(variablecnt * sizeof(uint16_t));
        if (cfg_info.uart_x_cfg[index].length == NULL)
        {
            LOG_E("%s rt_malloc(%d) failed!", __FUNCTION__, variablecnt * sizeof(uint16_t));
            ret = false;
            goto __exit;
        }
        snprintf(cfg_key, sizeof(cfg_key), "uart%dlength", x);
        len = ef_get_env_blob(cfg_key, cfg_info.uart_x_cfg[index].length, variablecnt * sizeof(uint16_t), NULL);
        if (len != variablecnt * sizeof(uint16_t))
        {
            LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, cfg_key);
            ret = false;
            goto __exit;
        }
        
        /* slaveraddr */
        cfg_info.uart_x_cfg[index].slaveraddr = (uint8_t*)rt_malloc(variablecnt * sizeof(uint8_t));
        if (cfg_info.uart_x_cfg[index].slaveraddr == NULL)
        {
            LOG_E("%s rt_malloc(%d) failed!", __FUNCTION__, variablecnt * sizeof(uint8_t));
            ret = false;
            goto __exit;
        }
        snprintf(cfg_key, sizeof(cfg_key), "uart%dslaveraddr", x);
        len = ef_get_env_blob(cfg_key, cfg_info.uart_x_cfg[index].slaveraddr, variablecnt * sizeof(uint8_t), NULL);
        if (len != variablecnt * sizeof(uint8_t))
        {
            LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, cfg_key);
            ret = false;
            goto __exit;
        }
        
        /* function */
        cfg_info.uart_x_cfg[index].function = (uint8_t*)rt_malloc(variablecnt * sizeof(uint8_t));
        if (cfg_info.uart_x_cfg[index].function == NULL)
        {
            LOG_E("%s rt_malloc(%d) failed!", __FUNCTION__, variablecnt * sizeof(uint8_t));
            ret = false;
            goto __exit;
        }
        snprintf(cfg_key, sizeof(cfg_key), "uart%dfunction", x);
        len = ef_get_env_blob(cfg_key, cfg_info.uart_x_cfg[index].function, variablecnt * sizeof(uint8_t), NULL);
        if (len != variablecnt * sizeof(uint8_t))
        {
            LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, cfg_key);
            ret = false;
            goto __exit;
        }
        
        /* type */
        cfg_info.uart_x_cfg[index].type = (uint8_t*)rt_malloc(variablecnt * sizeof(uint8_t));
        if (cfg_info.uart_x_cfg[index].type == NULL)
        {
            LOG_E("%s rt_malloc(%d) failed!", __FUNCTION__, variablecnt * sizeof(uint8_t));
            ret = false;
            goto __exit;
        }
        snprintf(cfg_key, sizeof(cfg_key), "uart%dtype", x);
        len = ef_get_env_blob(cfg_key, cfg_info.uart_x_cfg[index].type, variablecnt * sizeof(uint8_t), NULL);
        if (len != variablecnt * sizeof(uint8_t))
        {
            LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, cfg_key);
            ret = false;
            goto __exit;
        }
        
        /* variable */
        cfg_info.uart_x_cfg[index].variable = (char**)rt_malloc(variablecnt * sizeof(char*)); // 分配变量列表内存
        if (cfg_info.uart_x_cfg[index].variable == NULL)
        {
            LOG_E("%s rt_malloc(%d) failed!", __FUNCTION__, variablecnt * sizeof(char*));
            ret = false;
            goto __exit;
        }
        /* 先读取数据长度并分配相应长度的内存 */
        size_t data_len = 0;
        snprintf(cfg_key, sizeof(cfg_key), "uart%dvariable", x);
        ef_get_env_blob(cfg_key, NULL, 0, &data_len);
        if (data_len <= 0)
        { // 数据长度无效(不应该发生)
            LOG_E("%s ef_get_env_blob(%s) data_len(%d)!", __FUNCTION__, cfg_key, data_len);
            ret = false;
            goto __exit;
        }
        var_str_buf[index] = (char*)rt_malloc(data_len + 1); // 分配变量列表字符串内存
        if (var_str_buf[index] == NULL)
        {
            LOG_E("%s rt_malloc(%d) failed!", __FUNCTION__, (data_len + 1));
            ret = false;
            goto __exit;
        }
        /* 读取配置数据 */
        len = ef_get_env_blob(cfg_key, var_str_buf[index], data_len, NULL);
        if (len != data_len)
        {
            LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, cfg_key);
            ret = false;
            goto __exit;
        }
        var_str_buf[index][len] = '\0';
        /* 拆分变量列表字符串
         *
         *  变量列表加载到内存后的存储结构如下所示:
         *
         *    variable:    [0]   [1]   [2]    ... [n]
         *                  |     |     |          |
         *                  |     |     |          |
         *                  V     V     V          V
         *    var_str_buf: "var0\0var1\0var2\0...\0varn\0"
         *
         */
        char* str = var_str_buf[index];
        char** variable = cfg_info.uart_x_cfg[index].variable;
        int j = 0;
        variable[j++] = str; // 指向第一个变量的起点
        int i = 0;
        for (i = 0; i < len; ++i)
        {
            if (str[i] == ',')
            {
                str[i] = '\0'; // 分隔符改成'\0'
                if (j >= variablecnt)
                { // 变量个数不匹配(不应该发生)
                    LOG_E("%s split_cnt(%d)>variablecnt(%d)!", __FUNCTION__, j, variablecnt);
                    ret = false;
                    goto __exit;
                }
                variable[j++] = str + i + 1; // 指向下一个变量的起点
            }
        }
    }
    
    /* 加载其他配置项 */
#define LOAD_UART_X_CONFIG_ITEM(item, uart_x) \
    snprintf(cfg_key, sizeof(cfg_key), "uart%d%s", uart_x, #item); \
    len = ef_get_env_blob(cfg_key, &(cfg_info.uart_x_cfg[index].item), sizeof(cfg_info.uart_x_cfg[index].item), NULL); \
    if (len != sizeof(cfg_info.uart_x_cfg[index].item)) \
    { \
        LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, cfg_key); \
        ret = false; \
        goto __exit; \
    }
    
    LOAD_UART_X_CONFIG_ITEM(baudrate, x); /* baudrate */
    LOAD_UART_X_CONFIG_ITEM(variablecnt, x); /* variablecnt */
    LOAD_UART_X_CONFIG_ITEM(wordlength, x); /* wordlength */
    LOAD_UART_X_CONFIG_ITEM(parity, x); /* parity */
    LOAD_UART_X_CONFIG_ITEM(stopbits, x); /* stopbits */
    
#undef LOAD_UART_X_CONFIG_ITEM
    
__exit:
    
    return ret;
}

/*----------------------------------------------------------------------------*
**                             Public Function                                *
**----------------------------------------------------------------------------*/
/*************************************************
* Function: cfg_set_default
* Description:  恢复默认Flash配置(不包括缓存中的配置)
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
bool cfg_set_default(void)
{
    LOG_D("%s()", __FUNCTION__);

    size_t default_env_set_size = 0;
    const ef_env *default_env_set;
    
    /* 取得默认配置集 */
    ef_get_default_env(&default_env_set, &default_env_set_size);
    
    /* 全部重新设置为默认配置 */
    int i = 0;
    for (i = 0; i < default_env_set_size; ++i)
    {
        EfErrCode ef_ret = ef_set_env_blob(default_env_set[i].key, default_env_set[i].value, default_env_set[i].value_len);
        if (ef_ret != EF_NO_ERR)
        {
            LOG_E("%s ef_set_env_blob(%s) error(%d)!", __FUNCTION__, default_env_set[i].key, ef_ret);
            return false;
        }
    }
    
    return true;
}

/*************************************************
* Function: cfg_load
* Description:  从Flash加载配置项到内存
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
bool cfg_load(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    bool ret = true;
    size_t len = 0;
    
    /* 加载通用配置项 */
#define LOAD_CONFIG_ITEM(item) \
    len = ef_get_env_blob(#item, &(cfg_info.item), sizeof(cfg_info.item), NULL); \
    if (len != sizeof(cfg_info.item)) \
    { \
        LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, #item); \
        ret = false; \
        goto __exit; \
    }
    
    LOAD_CONFIG_ITEM(ulog_glb_lvl);
    LOAD_CONFIG_ITEM(client_id);
    LOAD_CONFIG_ITEM(a_port);
    LOAD_CONFIG_ITEM(b_port);
    LOAD_CONFIG_ITEM(acquisition);
    LOAD_CONFIG_ITEM(cycle);

#undef LOAD_CONFIG_ITEM
    
    /* 加载a_ip */
    {
        size_t a_ip_len = 0;
        ef_get_env_blob("a_ip", RT_NULL, 0, &a_ip_len);
        if (a_ip_len > 0)
        {
            cfg_info.a_ip = rt_malloc(a_ip_len + 1);
            if (cfg_info.a_ip == RT_NULL)
            {
                LOG_E("%s rt_malloc(%u) error!", __FUNCTION__, a_ip_len + 1);
                ret = false;
                goto __exit;
            }
            
            size_t len = ef_get_env_blob("a_ip", cfg_info.a_ip, a_ip_len, RT_NULL);
            if (len != a_ip_len)
            {
                LOG_E("%s ef_get_env_blob(a_ip) error!", __FUNCTION__);
                ret = false;
                goto __exit;
            }
            cfg_info.a_ip[a_ip_len] = '\0';
        }
    }
    
    /* 加载b_ip */
    {
        size_t b_ip_len = 0;
        ef_get_env_blob("b_ip", RT_NULL, 0, &b_ip_len);
        if (b_ip_len > 0)
        {
            cfg_info.b_ip = rt_malloc(b_ip_len + 1);
            if (cfg_info.b_ip == RT_NULL)
            {
                LOG_E("%s rt_malloc(%u) error!", __FUNCTION__, b_ip_len + 1);
                ret = false;
                goto __exit;
            }
            
            size_t len = ef_get_env_blob("b_ip", cfg_info.b_ip, b_ip_len, RT_NULL);
            if (len != b_ip_len)
            {
                LOG_E("%s ef_get_env_blob(b_ip) error!", __FUNCTION__);
                ret = false;
                goto __exit;
            }
            cfg_info.b_ip[b_ip_len] = '\0';
        }
    }
    
    /* 加载productKey */
    {
        size_t productkey_len = 0;
        ef_get_env_blob("productkey", RT_NULL, 0, &productkey_len);
        if (productkey_len > 0)
        {
            cfg_info.productkey = rt_malloc(productkey_len + 1);
            if (cfg_info.productkey == RT_NULL)
            {
                LOG_E("%s rt_malloc(%u) error!", __FUNCTION__, productkey_len + 1);
                ret = false;
                goto __exit;
            }
            
            size_t len = ef_get_env_blob("productkey", cfg_info.productkey, productkey_len, RT_NULL);
            if (len != productkey_len)
            {
                LOG_E("%s ef_get_env_blob(productkey) error!", __FUNCTION__);
                ret = false;
                goto __exit;
            }
            cfg_info.productkey[productkey_len] = '\0';
        }
    }
    
    /* 加载deviceCode */
    {
        size_t devicecode_len = 0;
        ef_get_env_blob("devicecode", RT_NULL, 0, &devicecode_len);
        if (devicecode_len > 0)
        {
            cfg_info.devicecode = rt_malloc(devicecode_len + 1);
            if (cfg_info.devicecode == RT_NULL)
            {
                LOG_E("%s rt_malloc(%u) error!", __FUNCTION__, devicecode_len + 1);
                ret = false;
                goto __exit;
            }
            
            size_t len = ef_get_env_blob("devicecode", cfg_info.devicecode, devicecode_len, RT_NULL);
            if (len != devicecode_len)
            {
                LOG_E("%s ef_get_env_blob(devicecode) error!", __FUNCTION__);
                ret = false;
                goto __exit;
            }
            cfg_info.devicecode[devicecode_len] = '\0';
        }
    }
    
    /* 加载itemId */
    {
        size_t itemid_len = 0;
        ef_get_env_blob("itemid", RT_NULL, 0, &itemid_len);
        if (itemid_len > 0)
        {
            cfg_info.itemid = rt_malloc(itemid_len + 1);
            if (cfg_info.itemid == RT_NULL)
            {
                LOG_E("%s rt_malloc(%u) error!", __FUNCTION__, itemid_len + 1);
                ret = false;
                goto __exit;
            }
            
            size_t len = ef_get_env_blob("itemid", cfg_info.itemid, itemid_len, RT_NULL);
            if (len != itemid_len)
            {
                LOG_E("%s ef_get_env_blob(itemid) error!", __FUNCTION__);
                ret = false;
                goto __exit;
            }
            cfg_info.itemid[itemid_len] = '\0';
        }
    }
    
    /* 加载UARTX配置项 */
    {
        int x = 1;
        for (x = 1; x <= ARRAY_SIZE(cfg_info.uart_x_cfg); ++x)
        {
            ret = cfg_load_uart_x(x);
            if (!ret)
            {
                LOG_E("%s cfg_load_uart_x(%d) failed!", __FUNCTION__, x);
                goto __exit;
            }
        }
    }
    
    /* 计算每条数据的大小(字节数) */
    calc_data_size();
    
__exit:
    if (!ret)
    { // 加载失败
        /* 清除缓存的配置数据,释放内存 */
        cfg_info_clear();
    }
    
    return ret;
}

/*************************************************
* Function: cfg_load_minimum
* Description: 加载最小配置(最小配置只保证系统能正常启动并连上服务器,以便可以执行远程升级或者诊断)
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
void cfg_load_minimum(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    cfg_info.client_id = 0; // 客户端编号,默认值000000000
    cfg_info.a_ip = "47.103.22.229"; // 通道A IP
    cfg_info.b_ip = "47.103.22.229"; // 通道B IP
    cfg_info.a_port = 1883; // 通道A 端口
    cfg_info.b_port = 1883; // 通道B 端口
    cfg_info.ulog_glb_lvl = 7; // LOG输出等级
    cfg_info.acquisition = 5; // 数据采集间隔时间(分钟)
    cfg_info.cycle = 30; // 数据发布间隔时间(分钟)
    cfg_info.productkey = NULL; // productKey
    cfg_info.devicecode = NULL; // deviceCode
    cfg_info.itemid = NULL; // itemId
    
    /* UARTX最小配置项 */
    int i = 0;
    for (i = 0; i < ARRAY_SIZE(cfg_info.uart_x_cfg); ++i)
    {
        cfg_info.uart_x_cfg[i].variable = NULL; /* uartXvariable: 变量名 */
        cfg_info.uart_x_cfg[i].baudrate = 9600; /* uartXbaudrate: 波特率 */
        cfg_info.uart_x_cfg[i].startaddr = NULL; /* uartXstartaddr: 开始地址 */
        cfg_info.uart_x_cfg[i].length = NULL; /* uartXlength: 读取寄存器数量 */
        cfg_info.uart_x_cfg[i].variablecnt = 0; /* uartXvariablecnt: 变量个数 */
        cfg_info.uart_x_cfg[i].wordlength = 8; /* uartXwordlength: 数据位 */
        cfg_info.uart_x_cfg[i].parity = 0; /* uartXparity: 校验位 */
        cfg_info.uart_x_cfg[i].stopbits = 2; /* uartXstopbits: 停止位 */
        cfg_info.uart_x_cfg[i].slaveraddr = NULL; /* uartXslaveraddr: 从机地址 */
        cfg_info.uart_x_cfg[i].function = NULL; /* uartXfunction: 功能码 */
        cfg_info.uart_x_cfg[i].type = NULL; /* uartXtype: 变量类型 */
    }
    
    /* 计算每条数据的大小(字节数) */
    calc_data_size();
}

/*************************************************
* Function: cfg_get()
* Description: 取得缓存的配置信息
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
config_info* cfg_get(void)
{
    return &cfg_info;
}

/*************************************************
* Function: cfg_print()
* Description: 输出缓存的配置信息
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
void cfg_print(void)
{
    LOG_D("%s()", __FUNCTION__);
    
    LOG_I("client_id: %010d", cfg_info.client_id);
    LOG_I("a_ip: %s", cfg_info.a_ip);
    LOG_I("a_port: %u", cfg_info.a_port);
    LOG_I("b_ip: %s", cfg_info.b_ip);
    LOG_I("b_port: %u", cfg_info.b_port);
    LOG_I("ulog_glb_lvl: %u", cfg_info.ulog_glb_lvl);
    LOG_I("acquisition: %u", cfg_info.acquisition);
    LOG_I("cycle: %u", cfg_info.cycle);
    LOG_I("productkey: %s", cfg_info.productkey ? cfg_info.productkey : "");
    LOG_I("devicecode: %s", cfg_info.devicecode ? cfg_info.devicecode : "");
    LOG_I("data_size: %u", cfg_info.data_size);
    
    int i = 0;
    for (i = 0; i < ARRAY_SIZE(cfg_info.uart_x_cfg); ++i)
    {
        //LOG_I("UART%d config", i + 1);
        int x = i + 1;
        LOG_I("uart%dvariablecnt: %u", x, cfg_info.uart_x_cfg[i].variablecnt);
        LOG_I("uart%dvariable:", x);
        int j = 0;
        for (j = 0; j < cfg_info.uart_x_cfg[i].variablecnt; ++j)
        {
            LOG_I("  %s", cfg_info.uart_x_cfg[i].variable[j]);
        }
        LOG_I("uart%dstartaddr:", x);
        for (j = 0; j < cfg_info.uart_x_cfg[i].variablecnt; ++j)
        {
            LOG_I("  0x%04x", cfg_info.uart_x_cfg[i].startaddr[j]);
        }
        LOG_I("uart%dslaveraddr:", x);
        for (j = 0; j < cfg_info.uart_x_cfg[i].variablecnt; ++j)
        {
            LOG_I("  0x%02x", cfg_info.uart_x_cfg[i].slaveraddr[j]);
        }
        LOG_I("uart%dfunction:", x);
        for (j = 0; j < cfg_info.uart_x_cfg[i].variablecnt; ++j)
        {
            LOG_I("  0x%02x", cfg_info.uart_x_cfg[i].function[j]);
        }
        LOG_I("uart%dlength:", x);
        for (j = 0; j < cfg_info.uart_x_cfg[i].variablecnt; ++j)
        {
            LOG_I("  0x%04x", cfg_info.uart_x_cfg[i].length[j]);
        }
        LOG_I("uart%dtype:", x);
        for (j = 0; j < cfg_info.uart_x_cfg[i].variablecnt; ++j)
        {
            LOG_I("  0x%02x", cfg_info.uart_x_cfg[i].type[j]);
        }
        LOG_I("uart%dbaudrate: %u", x, cfg_info.uart_x_cfg[i].baudrate);
        LOG_I("uart%dwordlength: %u", x, cfg_info.uart_x_cfg[i].wordlength);
        LOG_I("uart%dparity: %u", x, cfg_info.uart_x_cfg[i].parity);
        LOG_I("uart%dstopbits: %u", x, cfg_info.uart_x_cfg[i].stopbits);
    }
}

/**---------------------------------------------------------------------------*
 **                         Compiler Flag                                     *
 **---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif
// End of config.c
