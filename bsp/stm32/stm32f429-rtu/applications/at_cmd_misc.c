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
#include <easyflash.h>
#include <netdev_ipaddr.h>
#include <at.h>

#define LOG_TAG              "main.at_cmd_misc"
#define LOG_LVL              LOG_LVL_DBG
#include <rtdbg.h>

/* 取得历史数据条目数 */
extern uint32_t get_history_data_num(void);

/* AT+DATETIME 读取当前日期时间 */

static at_result_t at_date_time_query(const struct at_cmd *cmd)
{
    /* 获取时间 */
    time_t now = time(RT_NULL);
    struct tm* local_time = localtime(&now);
    char str_time[32] = "";
    strftime(str_time, sizeof(str_time) - 1, "%Y-%m-%d %H:%M:%S", local_time);
    
    /* 打印输出时间信息 */
    at_server_printfln("+DATETIME: %s", str_time);

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+DATETIME", RT_NULL, RT_NULL, at_date_time_query, RT_NULL, RT_NULL, 0);

/* AT+HISTORYDATANUM 读取历史数据条目数 */

static at_result_t at_history_data_num_query(const struct at_cmd *cmd)
{
    at_server_printfln("+HISTORYDATANUM: %u", get_history_data_num());

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+HISTORYDATANUM", RT_NULL, RT_NULL, at_history_data_num_query, RT_NULL, RT_NULL, 0);

/* AT+PRODUCTKEY 设置/读取productKey */

static at_result_t at_productkey_query(const struct at_cmd *cmd)
{
    char key[32] = "";
    size_t len = ef_get_env_blob("productkey", key, sizeof(key) - 1, RT_NULL);
    key[len] = '\0';
    at_server_printfln("+PRODUCTKEY: %s", key);
    
    return AT_RESULT_OK;
}

static at_result_t at_productkey_setup(const struct at_cmd *cmd, const char *args)
{
    char key[32] = "";
    char *req_expr = "=%s";

    if (rt_strlen(args) > sizeof(key))
    {
        LOG_E("rt_strlen(args)>%d!", sizeof(key));
        return AT_RESULT_CHECK_FAILE;
    }
    
    int argc = at_req_parse_args(args, req_expr, key);
    if (argc != 1)
    {
        LOG_E("at_req_parse_args(%s) argc(%d)!=1!", req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }

    EfErrCode ef_ret = ef_set_env_blob("productkey", key, rt_strlen(key));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("ef_set_env_blob(productkey,%s) error(%d)!", key, ef_ret);
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+PRODUCTKEY", "=<key>", RT_NULL, at_productkey_query, at_productkey_setup, RT_NULL, 0);

/* AT+DEVICEID 设置/读取deviceId */

static at_result_t at_deviceid_query(const struct at_cmd *cmd)
{
    char deviceid[32] = "";
    size_t len = ef_get_env_blob("deviceid", deviceid, sizeof(deviceid) - 1, RT_NULL);
    deviceid[len] = '\0';
    at_server_printfln("+DEVICEID: %s", deviceid);
    
    return AT_RESULT_OK;
}

static at_result_t at_deviceid_setup(const struct at_cmd *cmd, const char *args)
{
    char deviceid[32] = "";
    char *req_expr = "=%s";

    if (rt_strlen(args) > sizeof(deviceid))
    {
        LOG_E("rt_strlen(args)>%d!", sizeof(deviceid));
        return AT_RESULT_CHECK_FAILE;
    }
    
    int argc = at_req_parse_args(args, req_expr, deviceid);
    if (argc != 1)
    {
        LOG_E("at_req_parse_args(%s) argc(%d)!=1!", req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }

    EfErrCode ef_ret = ef_set_env_blob("deviceid", deviceid, rt_strlen(deviceid));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("ef_set_env_blob(deviceid,%s) error(%d)!", deviceid, ef_ret);
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+DEVICEID", "=<id>", RT_NULL, at_deviceid_query, at_deviceid_setup, RT_NULL, 0);

/* AT+ITEMID 设置/读取itemId */

static at_result_t at_itemid_query(const struct at_cmd *cmd)
{
    char itemid[32] = "";
    size_t len = ef_get_env_blob("itemid", itemid, sizeof(itemid) - 1, RT_NULL);
    itemid[len] = '\0';
    at_server_printfln("+ITEMID: %s", itemid);
    
    return AT_RESULT_OK;
}

static at_result_t at_itemid_setup(const struct at_cmd *cmd, const char *args)
{
    char itemid[32] = "";
    char *req_expr = "=%s";

    if (rt_strlen(args) > sizeof(itemid))
    {
        LOG_E("rt_strlen(args)>%d!", sizeof(itemid));
        return AT_RESULT_CHECK_FAILE;
    }
    
    int argc = at_req_parse_args(args, req_expr, itemid);
    if (argc != 1)
    {
        LOG_E("at_req_parse_args(%s) argc(%d)!=1!", req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }

    EfErrCode ef_ret = ef_set_env_blob("itemid", itemid, rt_strlen(itemid));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("ef_set_env_blob(itemid,%s) error(%d)!", itemid, ef_ret);
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+ITEMID", "=<id>", RT_NULL, at_itemid_query, at_itemid_setup, RT_NULL, 0);
