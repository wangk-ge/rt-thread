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
#include <at_device.h>
#include "common.h"
#include "config.h"
#include "app.h"
#include "util.h"

#define LOG_TAG              "main.at_cmd_rtu"
#define LOG_LVL              LOG_LVL_DBG
#include <rtdbg.h>

#define JSON_DATA_BUF_LEN (APP_MP_BLOCK_SIZE)

/* RTU相关AT指令 */

/* AT+CLIENTID 查询/读取客户端编号 */

static at_result_t at_clientid_query(const struct at_cmd *cmd)
{
    rt_uint32_t client_id = 0;
    size_t len = ef_get_env_blob("client_id", &client_id, sizeof(client_id), RT_NULL);
    if (len != sizeof(client_id))
    {
        LOG_E("%s ef_get_env_blob(client_id) error!", __FUNCTION__);
        return AT_RESULT_FAILE;
    }
    
    at_server_printfln("+CLIENTID: %010u", client_id);

    return AT_RESULT_OK;
}

static at_result_t at_clientid_setup(const struct at_cmd *cmd, const char *args)
{
    rt_uint32_t client_id = 0;
    const char *req_expr = "=%u";

    int argc = at_req_parse_args(args, req_expr, &client_id);
    if (argc != 1)
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }
    
    /* 0000000000-0099999999 */
    if (client_id > 99999999)
    {
        LOG_E("%s client_id(%u)>99999999!", __FUNCTION__, client_id);
        return AT_RESULT_CHECK_FAILE;
    }
    
    EfErrCode ef_ret = ef_set_env_blob("client_id", &client_id, sizeof(client_id));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(client_id,%u) error(%d)!", __FUNCTION__, client_id, ef_ret);
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+CLIENTID", "=<client_id>", RT_NULL, at_clientid_query, at_clientid_setup, RT_NULL, 0);

/* AT+AIP 查询/读取通道A IP */

static at_result_t at_aip_query(const struct at_cmd *cmd)
{
    char a_ip[64] = "";
    size_t len = ef_get_env_blob("a_ip", a_ip, sizeof(a_ip), RT_NULL);
    a_ip[len] = '\0';
    at_server_printfln("+AIP: %s", a_ip);
    
    return AT_RESULT_OK;
}

static at_result_t at_aip_setup(const struct at_cmd *cmd, const char *args)
{
    char a_ip[64] = "";
    char *req_expr = "=%s";

    if (rt_strlen(args) > sizeof(a_ip))
    {
        LOG_E("%s rt_strlen(args)>%d!", __FUNCTION__, sizeof(a_ip));
        return AT_RESULT_CHECK_FAILE;
    }
    
    int argc = at_req_parse_args(args, req_expr, a_ip);
    if (argc != 1)
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }
    
    /* 检查地址有效性 */
    bool addr_valid = util_is_ip_valid(a_ip);
    if (!addr_valid)
    {
        addr_valid = util_is_domainname_valid(a_ip);
    }
    
    if (!addr_valid)
    { // 地址无效
        LOG_E("%s invalid ip/domainname(%s)!", __FUNCTION__, a_ip);
        return AT_RESULT_PARSE_FAILE;
    }

    EfErrCode ef_ret = ef_set_env_blob("a_ip", a_ip, rt_strlen(a_ip));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(a_ip,%s) error(%d)!", __FUNCTION__, a_ip, ef_ret);
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+AIP", "=<ip>", RT_NULL, at_aip_query, at_aip_setup, RT_NULL, 0);

/* AT+APORT 查询/读取通道A端口 */

static at_result_t at_aport_query(const struct at_cmd *cmd)
{
    uint16_t port = 0;
    size_t len = ef_get_env_blob("a_port", &port, sizeof(port), RT_NULL);
    if (len != sizeof(port))
    {
        LOG_E("%s ef_get_env_blob(a_port) error!", __FUNCTION__);
        return AT_RESULT_FAILE;
    }
    
    at_server_printfln("+APORT: %u", port);

    return AT_RESULT_OK;
}

static at_result_t at_aport_setup(const struct at_cmd *cmd, const char *args)
{
    uint32_t port = 0;
    const char *req_expr = "=%u";

    int argc = at_req_parse_args(args, req_expr, &port);
    if (argc != 1)
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }
    
    if (port > 65535)
    {
        LOG_E("%s <port>(%u)>65535!", __FUNCTION__, port);
        return AT_RESULT_CHECK_FAILE;
    }
    
    uint16_t port_val = (uint16_t)port;
    EfErrCode ef_ret = ef_set_env_blob("a_port", &port_val, sizeof(port_val));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(a_port,%u) error(%d)!", __FUNCTION__, port_val, ef_ret);
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+APORT", "=<port>", RT_NULL, at_aport_query, at_aport_setup, RT_NULL, 0);

/* AT+BIP 查询/读取通道B IP */

static at_result_t at_bip_query(const struct at_cmd *cmd)
{
    char b_ip[64] = "";
    size_t len = ef_get_env_blob("b_ip", b_ip, sizeof(b_ip), RT_NULL);
    b_ip[len] = '\0';
    at_server_printfln("+BIP: %s", b_ip);

    return AT_RESULT_OK;
}

static at_result_t at_bip_setup(const struct at_cmd *cmd, const char *args)
{
    char b_ip[64] = "";
    char *req_expr = "=%s";

    if (rt_strlen(args) > sizeof(b_ip))
    {
        LOG_E("%s rt_strlen(args)>%d!", __FUNCTION__, sizeof(b_ip));
        return AT_RESULT_CHECK_FAILE;
    }
    
    int argc = at_req_parse_args(args, req_expr, b_ip);
    if (argc != 1)
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }
    
    /* 检查地址有效性 */
    bool addr_valid = util_is_ip_valid(b_ip);
    if (!addr_valid)
    {
        addr_valid = util_is_domainname_valid(b_ip);
    }
    
    if (!addr_valid)
    { // 地址无效
        LOG_E("%s invalid ip/domainname(%s)!", __FUNCTION__, b_ip);
        return AT_RESULT_PARSE_FAILE;
    }

    EfErrCode ef_ret = ef_set_env_blob("b_ip", b_ip, rt_strlen(b_ip));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(b_ip,%s) error(%d)!", __FUNCTION__, b_ip, ef_ret);
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+BIP", "=<ip>", RT_NULL, at_bip_query, at_bip_setup, RT_NULL, 0);

/* AT+BPORT 查询/读取通道B端口 */

static at_result_t at_bport_query(const struct at_cmd *cmd)
{
    uint16_t port = 0;
    size_t len = ef_get_env_blob("b_port", &port, sizeof(port), RT_NULL);
    if (len != sizeof(port))
    {
        LOG_E("%s ef_get_env_blob(b_port) error!", __FUNCTION__);
        return AT_RESULT_FAILE;
    }
    
    at_server_printfln("+BPORT: %u", port);

    return AT_RESULT_OK;
}

static at_result_t at_bport_setup(const struct at_cmd *cmd, const char *args)
{
    uint32_t port = 0;
    const char *req_expr = "=%u";

    int argc = at_req_parse_args(args, req_expr, &port);
    if (argc != 1)
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }
    
    if (port > 65535)
    {
        LOG_E("%s <port>(%u)>65535!", __FUNCTION__, port);
        return AT_RESULT_CHECK_FAILE;
    }
    
    uint16_t port_val = (uint16_t)port;
    EfErrCode ef_ret = ef_set_env_blob("b_port", &port_val, sizeof(port_val));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(b_port,%u) error(%d)!", __FUNCTION__, port_val, ef_ret);
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+BPORT", "=<port>", RT_NULL, at_bport_query, at_bport_setup, RT_NULL, 0);

/* AT+ACQUISITION 查询/读取数据采集间隔时间 */

static at_result_t at_acquisition_query(const struct at_cmd *cmd)
{
    uint8_t minutes = 0;
    size_t len = ef_get_env_blob("acquisition", &minutes, sizeof(minutes), RT_NULL);
    if (len != sizeof(minutes))
    {
        LOG_E("%s ef_get_env_blob(acquisition) error!", __FUNCTION__);
        return AT_RESULT_FAILE;
    }
    
    at_server_printfln("+ACQUISITION: %u", minutes);

    return AT_RESULT_OK;
}

static at_result_t at_acquisition_setup(const struct at_cmd *cmd, const char *args)
{
    uint32_t minutes = 0;
    const char *req_expr = "=%u";

    int argc = at_req_parse_args(args, req_expr, &minutes);
    if (argc != 1)
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }
    
    if ((minutes < 1) || (minutes > 30))
    {
        LOG_E("%s <minutes>(%u) not in range[1,30]!", __FUNCTION__, minutes);
        return AT_RESULT_CHECK_FAILE;
    }
    
    uint8_t minutes_val = (uint8_t)minutes;
    EfErrCode ef_ret = ef_set_env_blob("acquisition", &minutes_val, sizeof(minutes_val));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(acquisition,%u) error(%d)!", __FUNCTION__, minutes_val, ef_ret);
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+ACQUISITION", "=<minutes>", RT_NULL, at_acquisition_query, at_acquisition_setup, RT_NULL, 0);

/* AT+CYCLE 查询/读取数据发布间隔时间 */

static at_result_t at_cycle_query(const struct at_cmd *cmd)
{
    uint8_t minutes = 0;
    size_t len = ef_get_env_blob("cycle", &minutes, sizeof(minutes), RT_NULL);
    if (len != sizeof(minutes))
    {
        LOG_E("%s ef_get_env_blob(cycle) error!", __FUNCTION__);
        return AT_RESULT_FAILE;
    }
    
    at_server_printfln("+CYCLE: %u", minutes);

    return AT_RESULT_OK;
}

static at_result_t at_cycle_setup(const struct at_cmd *cmd, const char *args)
{
    uint32_t minutes = 0;
    const char *req_expr = "=%u";

    int argc = at_req_parse_args(args, req_expr, &minutes);
    if (argc != 1)
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }
    
    if ((minutes < 1) || (minutes > 180))
    {
        LOG_E("%s <minutes>(%u) not in range[1,180]!", __FUNCTION__, minutes);
        return AT_RESULT_CHECK_FAILE;
    }
    
    uint8_t minutes_val = (uint8_t)minutes;
    EfErrCode ef_ret = ef_set_env_blob("cycle", &minutes_val, sizeof(minutes_val));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(cycle,%u) error(%d)!", __FUNCTION__, minutes_val, ef_ret);
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+CYCLE", "=<minutes>", RT_NULL, at_cycle_query, at_cycle_setup, RT_NULL, 0);

/* AT+CLR 清空采集数据记录 */

static at_result_t at_clr_exec(const struct at_cmd *cmd)
{
    rt_err_t ret = clear_history_data();
    if (ret != RT_EOK)
    {
        LOG_E("%s clear_history_data() error(%d)!", __FUNCTION__, ret);
        return AT_RESULT_FAILE;
    }
    
    return AT_RESULT_OK;
}
AT_CMD_EXPORT("AT+CLR", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_clr_exec, 0);

/* AT+DEFT 恢复出厂设置 */

static at_result_t at_deft_exec(const struct at_cmd *cmd)
{
    bool ret = cfg_set_default();
    if (!ret)
    {
        LOG_E("%s set_default_config() failed!", __FUNCTION__);
        return AT_RESULT_FAILE;
    }
    
    return AT_RESULT_OK;
}
AT_CMD_EXPORT("AT+DEFT", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_deft_exec, 0);

/* AT+RESTART 重新启动RTU */

static at_result_t at_restart_exec(const struct at_cmd *cmd)
{
    rt_hw_cpu_reset();
    
    return AT_RESULT_OK;
}
AT_CMD_EXPORT("AT+RESTART", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_restart_exec, 0);

/* AT+SENSORHVER 查询泵站RTU固件硬件版本号 */

static at_result_t at_sensorhver_exec(const struct at_cmd *cmd)
{
    at_server_printfln("+SENSORHVER: %s", HW_VERSION);
    
    return AT_RESULT_OK;
}
AT_CMD_EXPORT("AT+SENSORHVER", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_sensorhver_exec, 0);

/* AT+SENSORSVER 查询泵站RTU软件版本号 */

static at_result_t at_sensorsver_exec(const struct at_cmd *cmd)
{
    at_server_printfln("+SENSORSVER: %s", SW_VERSION);
    
    return AT_RESULT_OK;
}
AT_CMD_EXPORT("AT+SENSORSVER", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_sensorsver_exec, 0);

/* AT+DATARD RTU历史数据读取 */

static at_result_t at_datard_setup(const struct at_cmd *cmd, const char *args)
{
    uint32_t n = 0;
    const char *req_expr = "=%u";

    int argc = at_req_parse_args(args, req_expr, &n);
    if (argc != 1)
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }
    
    char* json_data_buf = (char*)app_mp_alloc();
    RT_ASSERT(json_data_buf != NULL)
    
    /* 读取前n个时刻的一条历史数据(JSON格式)  */
    uint32_t read_len = read_history_data_json(n, json_data_buf, JSON_DATA_BUF_LEN, true);
    
    /* 输出JSON数据 */
    if (read_len > 0)
    { // 读取到数据
        at_server_printfln("+DATARD: %s", json_data_buf);
    }
    else
    { // 没有读取到数据
        at_server_printfln("+DATARD: ");
    }
    
    app_mp_free(json_data_buf);
    json_data_buf = NULL;
    
    return AT_RESULT_OK;
}
AT_CMD_EXPORT("AT+DATARD", "=<n>", RT_NULL, RT_NULL, at_datard_setup, RT_NULL, 0);

/* AT+RSSI 查询当前信号强度 */

static at_result_t at_rssi_exec(const struct at_cmd *cmd)
{
    int rssi = 0;
    int ret = get_modem_rssi(&rssi);
    if (ret != RT_EOK)
    {
        LOG_E("%s get_modem_rssi error(%d)!", __FUNCTION__, ret);
        return AT_RESULT_FAILE;
    }
    
    at_server_printfln("+RSSI: %d", rssi);
    
    return AT_RESULT_OK;
}
AT_CMD_EXPORT("AT+RSSI", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_rssi_exec, 0);

