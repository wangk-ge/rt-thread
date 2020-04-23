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
    }
    
    if (cfg_info.productkey != NULL)
    {
        rt_free(cfg_info.productkey);
    }
    
    if (cfg_info.deviceid != NULL)
    {
        rt_free(cfg_info.deviceid);
    }
    
    if (cfg_info.itemid != NULL)
    {
        rt_free(cfg_info.itemid);
    }
    
    /* 所有成员清0 */
    memset(&cfg_info, 0, sizeof(cfg_info));
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
        LOG_E("ef_get_env_blob(%s) error!", cfg_key);
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
            LOG_E("rt_malloc(%d) failed!", variablecnt * sizeof(uint16_t));
            ret = false;
            goto __exit;
        }
        snprintf(cfg_key, sizeof(cfg_key), "uart%dstartaddr", x);
        len = ef_get_env_blob(cfg_key, cfg_info.uart_x_cfg[index].startaddr, variablecnt * sizeof(uint16_t), NULL);
        if (len != variablecnt * sizeof(uint16_t))
        {
            LOG_E("ef_get_env_blob(%s) error!", cfg_key);
            ret = false;
            goto __exit;
        }
        
        /* length */
        cfg_info.uart_x_cfg[index].length = (uint16_t*)rt_malloc(variablecnt * sizeof(uint16_t));
        if (cfg_info.uart_x_cfg[index].length == NULL)
        {
            LOG_E("rt_malloc(%d) failed!", variablecnt * sizeof(uint16_t));
            ret = false;
            goto __exit;
        }
        snprintf(cfg_key, sizeof(cfg_key), "uart%dlength", x);
        len = ef_get_env_blob(cfg_key, cfg_info.uart_x_cfg[index].length, variablecnt * sizeof(uint16_t), NULL);
        if (len != variablecnt * sizeof(uint16_t))
        {
            LOG_E("ef_get_env_blob(%s) error!", cfg_key);
            ret = false;
            goto __exit;
        }
        
        /* variable */
        cfg_info.uart_x_cfg[index].variable = (char**)rt_malloc(variablecnt * sizeof(char*)); // 分配变量列表内存
        if (cfg_info.uart_x_cfg[index].variable == NULL)
        {
            LOG_E("rt_malloc(%d) failed!", variablecnt * sizeof(char*));
            ret = false;
            goto __exit;
        }
        /* 先读取数据长度并分配相应长度的内存 */
        size_t data_len = 0;
        snprintf(cfg_key, sizeof(cfg_key), "uart%dvariable", x);
        ef_get_env_blob(cfg_key, NULL, 0, &data_len);
        if (data_len <= 0)
        { // 数据长度无效(不应该发生)
            LOG_E("ef_get_env_blob(%s) data_len(%d)!", cfg_key, data_len);
            ret = false;
            goto __exit;
        }
        var_str_buf[index] = (char*)rt_malloc(data_len + 1); // 分配变量列表字符串内存
        if (var_str_buf[index] == NULL)
        {
            LOG_E("rt_malloc(%d) failed!", (data_len + 1));
            ret = false;
            goto __exit;
        }
        /* 读取配置数据 */
        len = ef_get_env_blob(cfg_key, var_str_buf[index], data_len, NULL);
        if (len != data_len)
        {
            LOG_E("ef_get_env_blob(%s) error!", cfg_key);
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
                    LOG_E("split_cnt(%d)>variablecnt(%d)!", j, variablecnt);
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
        LOG_E("ef_get_env_blob(%s) error!", cfg_key); \
        ret = false; \
        goto __exit; \
    }
    
    LOAD_UART_X_CONFIG_ITEM(baudrate, x); /* baudrate */
    LOAD_UART_X_CONFIG_ITEM(variablecnt, x); /* variablecnt */
    LOAD_UART_X_CONFIG_ITEM(wordlength, x); /* wordlength */
    LOAD_UART_X_CONFIG_ITEM(parity, x); /* parity */
    LOAD_UART_X_CONFIG_ITEM(stopbits, x); /* stopbits */
    LOAD_UART_X_CONFIG_ITEM(slaveraddr, x); /* slaveraddr */
    LOAD_UART_X_CONFIG_ITEM(function, x); /* function */
    
#undef LOAD_UART_X_CONFIG_ITEM
    
__exit:
    
    return ret;
}

/*----------------------------------------------------------------------------*
**                             Public Function                                *
**----------------------------------------------------------------------------*/
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
    bool ret = true;
    size_t len = 0;
    
    /* 清除缓存的配置数据,释放内存 */
    cfg_info_clear();
    
    /* 加载通用配置项 */
#define LOAD_CONFIG_ITEM(item) \
    len = ef_get_env_blob(#item, &(cfg_info.item), sizeof(cfg_info.item), NULL); \
    if (len != sizeof(cfg_info.item)) \
    { \
        LOG_E("ef_get_env_blob(%s) error!", #item); \
        ret = false; \
        goto __exit; \
    }
    
    LOAD_CONFIG_ITEM(ulog_glb_lvl);
    LOAD_CONFIG_ITEM(client_id);
    LOAD_CONFIG_ITEM(a_ip);
    LOAD_CONFIG_ITEM(a_port);
    LOAD_CONFIG_ITEM(b_ip);
    LOAD_CONFIG_ITEM(b_port);
    LOAD_CONFIG_ITEM(acquisition);
    LOAD_CONFIG_ITEM(cycle);

#undef LOAD_CONFIG_ITEM
    
    /* 加载productKey */
    {
        size_t productkey_len = 0;
        ef_get_env_blob("productkey", RT_NULL, 0, &productkey_len);
        if (productkey_len > 0)
        {
            cfg_info.productkey = rt_malloc(productkey_len + 1);
            if (cfg_info.productkey == RT_NULL)
            {
                LOG_E("rt_malloc(%u) error!", productkey_len + 1);
                ret = false;
                goto __exit;
            }
            
            size_t len = ef_get_env_blob("productkey", cfg_info.productkey, productkey_len, RT_NULL);
            if (len != productkey_len)
            {
                LOG_E("ef_get_env_blob(productkey) error!");
                ret = false;
                goto __exit;
            }
            cfg_info.productkey[productkey_len] = '\0';
        }
    }
    
    /* 加载deviceId */
    {
        size_t deviceid_len = 0;
        ef_get_env_blob("deviceid", RT_NULL, 0, &deviceid_len);
        if (deviceid_len > 0)
        {
            cfg_info.deviceid = rt_malloc(deviceid_len + 1);
            if (cfg_info.deviceid == RT_NULL)
            {
                LOG_E("rt_malloc(%u) error!", deviceid_len + 1);
                ret = false;
                goto __exit;
            }
            
            size_t len = ef_get_env_blob("deviceid", cfg_info.deviceid, deviceid_len, RT_NULL);
            if (len != deviceid_len)
            {
                LOG_E("ef_get_env_blob(deviceid) error!");
                ret = false;
                goto __exit;
            }
            cfg_info.deviceid[deviceid_len] = '\0';
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
                LOG_E("rt_malloc(%u) error!", itemid_len + 1);
                ret = false;
                goto __exit;
            }
            
            size_t len = ef_get_env_blob("itemid", cfg_info.itemid, itemid_len, RT_NULL);
            if (len != itemid_len)
            {
                LOG_E("ef_get_env_blob(itemid) error!");
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
                goto __exit;
            }
        }
    }
    
__exit:
    if (!ret)
    { // 加载失败
        /* 清除缓存的配置数据,释放内存 */
        cfg_info_clear();
        //ef_env_set_default();
    }
    
    return ret;
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
    char ch_buf[32] = "";

    //LOG_I("all config info");
    LOG_I("client_id: %010d", cfg_info.client_id);
    LOG_I("a_ip: %s", inet_ntoa_r(cfg_info.a_ip, ch_buf, sizeof(ch_buf)));
    LOG_I("a_port: %u", cfg_info.a_port);
    LOG_I("b_ip: %s", inet_ntoa_r(cfg_info.b_ip, ch_buf, sizeof(ch_buf)));
    LOG_I("b_port: %u", cfg_info.b_port);
    LOG_I("ulog_glb_lvl: %u", cfg_info.ulog_glb_lvl);
    LOG_I("acquisition: %u", cfg_info.acquisition);
    LOG_I("cycle: %u", cfg_info.cycle);
    LOG_I("productkey: %s", cfg_info.productkey ? cfg_info.productkey : "");
    LOG_I("deviceid: %s", cfg_info.deviceid ? cfg_info.deviceid : "");
    
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
            LOG_I("%s", cfg_info.uart_x_cfg[i].variable[j]);
        }
        LOG_I("uart%dstartaddr:", x);
        for (j = 0; j < cfg_info.uart_x_cfg[i].variablecnt; ++j)
        {
            LOG_I("0x%04x", cfg_info.uart_x_cfg[i].startaddr[j]);
        }
        LOG_I("uart%dlength:", x);
        for (j = 0; j < cfg_info.uart_x_cfg[i].variablecnt; ++j)
        {
            LOG_I("0x%04x", cfg_info.uart_x_cfg[i].length[j]);
        }
        LOG_I("uart%dbaudrate: %u", x, cfg_info.uart_x_cfg[i].baudrate);
        LOG_I("uart%dwordlength: %u", x, cfg_info.uart_x_cfg[i].wordlength);
        LOG_I("uart%dparity: %u", x, cfg_info.uart_x_cfg[i].parity);
        LOG_I("uart%dstopbits: %u", x, cfg_info.uart_x_cfg[i].stopbits);
        LOG_I("uart%dslaveraddr: 0x%02x", x, cfg_info.uart_x_cfg[i].slaveraddr);
        LOG_I("uart%dfunction: 0x%02x", x, cfg_info.uart_x_cfg[i].function);
    }
}

/**---------------------------------------------------------------------------*
 **                         Compiler Flag                                     *
 **---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif
// End of config.c
