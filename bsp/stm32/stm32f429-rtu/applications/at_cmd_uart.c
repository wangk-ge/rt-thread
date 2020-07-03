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
#include <stdio.h>
#include "common.h"
#include "strref.h"
#include "app.h"

#define LOG_TAG              "main.at_cmd_uart"
#define LOG_LVL              LOG_LVL_DBG
#include <rtdbg.h>

/* 每路UARTX最大变量个数 */
#define MAX_UARTX_VAR_CNT 100
/* 最大变量名长度 */
#define MAX_UARTX_VAR_NAME_LEN 20

/* 最长100个可变参数个数格式AT指令表达式 */
#define ARGS_EXPR_VAR_PARM100 "=<p1>[,<p2>][,<p3>][,<p4>][,<p5>][,<p6>][,<p7>][,<p8>][,<p9>][,<p10>]" \
    "[,<p11>][,<p12>][,<p13>][,<p14>][,<p15>][,<p16>][,<p17>][,<p18>][,<p19>][,<p20>]" \
    "[,<p21>][,<p22>][,<p23>][,<p24>][,<p25>][,<p26>][,<p27>][,<p28>][,<p29>][,<p30>]" \
    "[,<p31>][,<p32>][,<p33>][,<p34>][,<p35>][,<p36>][,<p37>][,<p38>][,<p39>][,<p40>]" \
    "[,<p41>][,<p42>][,<p43>][,<p44>][,<p45>][,<p46>][,<p47>][,<p48>][,<p49>][,<p50>]" \
    "[,<p51>][,<p52>][,<p53>][,<p54>][,<p55>][,<p56>][,<p57>][,<p58>][,<p59>][,<p60>]" \
    "[,<p61>][,<p62>][,<p63>][,<p64>][,<p65>][,<p66>][,<p67>][,<p68>][,<p69>][,<p70>]" \
    "[,<p71>][,<p72>][,<p73>][,<p74>][,<p75>][,<p76>][,<p77>][,<p78>][,<p79>][,<p80>]" \
    "[,<p81>][,<p82>][,<p83>][,<p84>][,<p85>][,<p86>][,<p87>][,<p88>][,<p89>][,<p90>]" \
    "[,<p91>][,<p92>][,<p93>][,<p94>][,<p95>][,<p96>][,<p97>][,<p98>][,<p99>][,<p100>]"

/* 读取串口号对应的变量个数 */
static bool get_variablecnt(char uart_x, uint8_t *var_cnt)
{
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXvariablecnt";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 读取配置 */
    uint8_t cnt = 0;
    size_t len = ef_get_env_blob(cfg_key, &cnt, sizeof(cnt), RT_NULL);
    if (len != sizeof(cnt))
    {
        LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, cfg_key);
        return false;
    }
    
    *var_cnt = cnt;
    
    return true;
}

/* MODBUS相关AT指令 */

/* AT+UARTXVARIABLE 设置/读取UART X相关的变量名列表 */

static at_result_t at_uartxvariable_println(char uart_x)
{
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXvariable";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 分配内存 */
    char *var_list = (char*)app_mp_alloc();
    RT_ASSERT(var_list != RT_NULL)
    
    /* 读取配置 */
    size_t data_len = ef_get_env_blob(cfg_key, var_list, APP_MP_BLOCK_SIZE, RT_NULL);
    if (data_len <= 0)
    {
        /* 释放内存 */
        app_mp_free(var_list);
        
        LOG_E("%s ef_get_env_blob(%s) error(data_len=%u)!", __FUNCTION__, cfg_key, data_len);
        return AT_RESULT_FAILE;
    }
    var_list[data_len] = '\0';
    
    at_server_printfln("+UART%cVARIABLE: %s", uart_x, var_list);
    
    /* 释放内存 */
    app_mp_free(var_list);

    return AT_RESULT_OK;
}

static at_result_t at_uartxvariable_query(const struct at_cmd *cmd)
{
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    return at_uartxvariable_println(uart_x);
}

static at_result_t at_uartxvariable_setup(const struct at_cmd *cmd, const char *args)
{
    const char *var_list = args + 1; // 跳过'='
    size_t data_len = rt_strlen(args) - 1 - STR_LEN(AT_CMD_END_MARK); // 排除结尾符
    
    /* 拆分参数列表并检查参数个数 */
    c_str_ref str_ref = { data_len, var_list };
    c_str_ref *param_list = (c_str_ref*)app_mp_alloc();
    RT_ASSERT(param_list);
    uint32_t param_count = strref_split(&str_ref, ',', param_list, MAX_UARTX_VAR_CNT + 1);
    if ((param_count < 1) || (param_count > MAX_UARTX_VAR_CNT))
    {
        LOG_E("%s strref_split() param_count(%d) not in range[1,%d]!", __FUNCTION__, param_count, MAX_UARTX_VAR_CNT);
        app_mp_free(param_list);
        return AT_RESULT_PARSE_FAILE;
    }
    
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    /* 读取配置的变量个数 */
    uint8_t cfg_var_cnt = 0;
    bool ret = get_variablecnt(uart_x, &cfg_var_cnt);
    if (!ret)
    {
        LOG_E("%s get_variablecnt(UART%c) failed!", __FUNCTION__, uart_x);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }
    
    /* 变量个数检查 */
    if (param_count != cfg_var_cnt)
    {
        LOG_E("%s param_count=%u cfg_var_cnt=%u!", __FUNCTION__, param_count, cfg_var_cnt);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }
    
    // 检查参数格式(长度)
    int i = 0;
    for (i = 0; i < param_count; ++i)
    {
        if (param_list[i].len > MAX_UARTX_VAR_NAME_LEN)
        {
            LOG_E("%s param[%d] len(%u)>%u!", __FUNCTION__, i, param_list[i].len, MAX_UARTX_VAR_NAME_LEN);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
    }

    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXvariable";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 保存配置 */
    EfErrCode ef_ret = ef_set_env_blob(cfg_key, var_list, data_len);
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(%s) error(%d)!", __FUNCTION__, cfg_key, ef_ret);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }
    
    app_mp_free(param_list);

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+UART1VARIABLE", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxvariable_query, at_uartxvariable_setup, RT_NULL, 1);
AT_CMD_EXPORT("AT+UART2VARIABLE", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxvariable_query, at_uartxvariable_setup, RT_NULL, 2);
AT_CMD_EXPORT("AT+UART3VARIABLE", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxvariable_query, at_uartxvariable_setup, RT_NULL, 3);
AT_CMD_EXPORT("AT+UART4VARIABLE", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxvariable_query, at_uartxvariable_setup, RT_NULL, 4);

/* AT+UARTXVARIABLECNT 设置/读取UART X相关的变量个数 */

static at_result_t at_uartxvariablecnt_println(char uart_x)
{
    uint8_t var_cnt = 0;
    bool ret = get_variablecnt(uart_x, &var_cnt);
    if (!ret)
    {
        LOG_E("%s get_variablecnt(UART%c) failed!", __FUNCTION__, uart_x);
        return AT_RESULT_FAILE;
    }
    
    at_server_printfln("+UART%cVARIABLECNT: %u", uart_x, var_cnt);

    return AT_RESULT_OK;
}

static at_result_t at_uartxvariablecnt_query(const struct at_cmd *cmd)
{
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    return at_uartxvariablecnt_println(uart_x);
}

static at_result_t at_uartxvariablecnt_setup(const struct at_cmd *cmd, const char *args)
{
    rt_uint32_t cnt = 0;
    const char *req_expr = "=%u";

    int argc = at_req_parse_args(args, req_expr, &cnt);
    if (argc != 1)
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }
    
    if (cnt > MAX_UARTX_VAR_CNT)
    {
        LOG_E("%s variable_cnt(%u)>%u!", __FUNCTION__, cnt, MAX_UARTX_VAR_CNT);
        return AT_RESULT_CHECK_FAILE;
    }
    
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXvariablecnt";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 保存配置 */
    uint8_t cnt_val = (uint8_t)cnt;
    EfErrCode ef_ret = ef_set_env_blob(cfg_key, &cnt_val, sizeof(cnt_val));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(%s,%u) error(%d)!", __FUNCTION__, cfg_key, cnt_val, ef_ret);
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+UART1VARIABLECNT", "=<cnt>", RT_NULL, at_uartxvariablecnt_query, at_uartxvariablecnt_setup, RT_NULL, 1);
AT_CMD_EXPORT("AT+UART2VARIABLECNT", "=<cnt>", RT_NULL, at_uartxvariablecnt_query, at_uartxvariablecnt_setup, RT_NULL, 2);
AT_CMD_EXPORT("AT+UART3VARIABLECNT", "=<cnt>", RT_NULL, at_uartxvariablecnt_query, at_uartxvariablecnt_setup, RT_NULL, 3);
AT_CMD_EXPORT("AT+UART4VARIABLECNT", "=<cnt>", RT_NULL, at_uartxvariablecnt_query, at_uartxvariablecnt_setup, RT_NULL, 4);

/* AT+UARTXBAUDRATE 设置/读取UART X波特率 */

static at_result_t at_uartxbaudrate_println(char uart_x)
{
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXbaudrate";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 读取配置 */
    uint32_t baudrate = 0;
    size_t len = ef_get_env_blob(cfg_key, &baudrate, sizeof(baudrate), RT_NULL);
    if (len != sizeof(baudrate))
    {
        LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, cfg_key);
        return AT_RESULT_FAILE;
    }
    
    at_server_printfln("+UART%cBAUDRATE: %u", uart_x, baudrate);

    return AT_RESULT_OK;
}

static at_result_t at_uartxbaudrate_query(const struct at_cmd *cmd)
{
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    return at_uartxbaudrate_println(uart_x);
}

static at_result_t at_uartxbaudrate_setup(const struct at_cmd *cmd, const char *args)
{
    rt_uint32_t baudrate = 0;
    const char *req_expr = "=%u";

    int argc = at_req_parse_args(args, req_expr, &baudrate);
    if (argc != 1)
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }
    
    // TODO 检查波特率有效性
    
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXbaudrate";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 保存配置 */
    EfErrCode ef_ret = ef_set_env_blob(cfg_key, &baudrate, sizeof(baudrate));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(%s,%u) error(%d)!", __FUNCTION__, cfg_key, baudrate, ef_ret);
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+UART1BAUDRATE", "=<baudrate>", RT_NULL, at_uartxbaudrate_query, at_uartxbaudrate_setup, RT_NULL, 1);
AT_CMD_EXPORT("AT+UART2BAUDRATE", "=<baudrate>", RT_NULL, at_uartxbaudrate_query, at_uartxbaudrate_setup, RT_NULL, 2);
AT_CMD_EXPORT("AT+UART3BAUDRATE", "=<baudrate>", RT_NULL, at_uartxbaudrate_query, at_uartxbaudrate_setup, RT_NULL, 3);
AT_CMD_EXPORT("AT+UART4BAUDRATE", "=<baudrate>", RT_NULL, at_uartxbaudrate_query, at_uartxbaudrate_setup, RT_NULL, 4);

/* AT+UARTXWORDLENGTH 设置/读取UART X数据位数 */

static at_result_t at_uartxwordlength_println(char uart_x)
{
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXwordlength";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 读取配置 */
    uint8_t bits = 0;
    size_t len = ef_get_env_blob(cfg_key, &bits, sizeof(bits), RT_NULL);
    if (len != sizeof(bits))
    {
        LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, cfg_key);
        return AT_RESULT_FAILE;
    }
    
    at_server_printfln("+UART%cWORDLENGTH: %u", uart_x, bits);

    return AT_RESULT_OK;
}

static at_result_t at_uartxwordlength_query(const struct at_cmd *cmd)
{
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    return at_uartxwordlength_println(uart_x);
}

static at_result_t at_uartxwordlength_setup(const struct at_cmd *cmd, const char *args)
{
    rt_uint32_t bits = 0;
    const char *req_expr = "=%u";

    int argc = at_req_parse_args(args, req_expr, &bits);
    if (argc != 1)
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }
    
    // 检查数据位数有效性(5,6,7,8)
    if ((bits < 5) || (bits > 8))
    {
        LOG_E("%s bits(%u) range[5,8]!", __FUNCTION__, bits);
        return AT_RESULT_CHECK_FAILE;
    }
    
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXwordlength";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 保存配置 */
    uint8_t bits_val = (uint8_t)bits;
    EfErrCode ef_ret = ef_set_env_blob(cfg_key, &bits_val, sizeof(bits_val));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(%s,%u) error(%d)!", __FUNCTION__, cfg_key, bits_val, ef_ret);
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+UART1WORDLENGTH", "=<bits>", RT_NULL, at_uartxwordlength_query, at_uartxwordlength_setup, RT_NULL, 1);
AT_CMD_EXPORT("AT+UART2WORDLENGTH", "=<bits>", RT_NULL, at_uartxwordlength_query, at_uartxwordlength_setup, RT_NULL, 2);
AT_CMD_EXPORT("AT+UART3WORDLENGTH", "=<bits>", RT_NULL, at_uartxwordlength_query, at_uartxwordlength_setup, RT_NULL, 3);
AT_CMD_EXPORT("AT+UART4WORDLENGTH", "=<bits>", RT_NULL, at_uartxwordlength_query, at_uartxwordlength_setup, RT_NULL, 4);

/* AT+UARTXPARITY 设置/读取UART X校验方式(无校验/奇校验/偶校验) */

static at_result_t at_uartxparity_println(char uart_x)
{
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXparity";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 读取配置 */
    uint8_t parity = 0;
    size_t len = ef_get_env_blob(cfg_key, &parity, sizeof(parity), RT_NULL);
    if (len != sizeof(parity))
    {
        LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, cfg_key);
        return AT_RESULT_FAILE;
    }
    
    at_server_printfln("+UART%cPARITY: %u", uart_x, parity);

    return AT_RESULT_OK;
}

static at_result_t at_uartxparity_query(const struct at_cmd *cmd)
{
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    return at_uartxparity_println(uart_x);
}

static at_result_t at_uartxparity_setup(const struct at_cmd *cmd, const char *args)
{
    rt_uint32_t parity = 0;
    const char *req_expr = "=%u";

    int argc = at_req_parse_args(args, req_expr, &parity);
    if (argc != 1)
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }
    
    // 检查数据位数有效性(0=无,1=奇,2=偶)
    if (parity > 2)
    {
        LOG_E("%s parity(%u) range[0,2]!", __FUNCTION__, parity);
        return AT_RESULT_CHECK_FAILE;
    }
    
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXparity";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 保存配置 */
    uint8_t parity_val = (uint8_t)parity;
    EfErrCode ef_ret = ef_set_env_blob(cfg_key, &parity_val, sizeof(parity_val));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(%s,%u) error(%d)!", __FUNCTION__, cfg_key, parity_val, ef_ret);
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+UART1PARITY", "=<parity>", RT_NULL, at_uartxparity_query, at_uartxparity_setup, RT_NULL, 1);
AT_CMD_EXPORT("AT+UART2PARITY", "=<parity>", RT_NULL, at_uartxparity_query, at_uartxparity_setup, RT_NULL, 2);
AT_CMD_EXPORT("AT+UART3PARITY", "=<parity>", RT_NULL, at_uartxparity_query, at_uartxparity_setup, RT_NULL, 3);
AT_CMD_EXPORT("AT+UART4PARITY", "=<parity>", RT_NULL, at_uartxparity_query, at_uartxparity_setup, RT_NULL, 4);

/* AT+UARTXSTOPBITS 设置/读取UART X停止位 */

static at_result_t at_uartxstopbits_println(char uart_x)
{
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXstopbits";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 读取配置 */
    uint8_t bits = 0;
    size_t len = ef_get_env_blob(cfg_key, &bits, sizeof(bits), RT_NULL);
    if (len != sizeof(bits))
    {
        LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, cfg_key);
        return AT_RESULT_FAILE;
    }
    
    at_server_printfln("+UART%cSTOPBITS: %u", uart_x, bits);
    
    return AT_RESULT_OK;
}

static at_result_t at_uartxstopbits_query(const struct at_cmd *cmd)
{
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    return at_uartxstopbits_println(uart_x);
}

static at_result_t at_uartxstopbits_setup(const struct at_cmd *cmd, const char *args)
{
    rt_uint32_t bits = 0;
    const char *req_expr = "=%u";

    int argc = at_req_parse_args(args, req_expr, &bits);
    if (argc != 1)
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }

    // 检查数据位数有效性(1,2)
    if ((bits != 1) && (bits != 2))
    {
        LOG_E("%s stop_bits(%u) can only be 1 or 2!", __FUNCTION__, bits);
        return AT_RESULT_CHECK_FAILE;
    }
    
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXstopbits";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 保存配置 */
    uint8_t bits_val = (uint8_t)bits;
    EfErrCode ef_ret = ef_set_env_blob(cfg_key, &bits_val, sizeof(bits_val));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(%s,0x%x) error(%d)!", __FUNCTION__, cfg_key, bits_val, ef_ret);
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+UART1STOPBITS", "=<bits>", RT_NULL, at_uartxstopbits_query, at_uartxstopbits_setup, RT_NULL, 1);
AT_CMD_EXPORT("AT+UART2STOPBITS", "=<bits>", RT_NULL, at_uartxstopbits_query, at_uartxstopbits_setup, RT_NULL, 2);
AT_CMD_EXPORT("AT+UART3STOPBITS", "=<bits>", RT_NULL, at_uartxstopbits_query, at_uartxstopbits_setup, RT_NULL, 3);
AT_CMD_EXPORT("AT+UART4STOPBITS", "=<bits>", RT_NULL, at_uartxstopbits_query, at_uartxstopbits_setup, RT_NULL, 4);

/* AT+UARTXSLAVERADDR 设置/读取UART X相关的从机地址列表(每个1字节) */

static at_result_t at_uartxslaveraddr_println(char uart_x)
{
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXslaveraddr";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 读取配置 */
    uint8_t slaveraddr_list[MAX_UARTX_VAR_CNT] = {0x00};
    size_t data_size = 0;
    size_t read_len = ef_get_env_blob(cfg_key, slaveraddr_list, sizeof(slaveraddr_list), &data_size);
    if (read_len != data_size)
    {
        LOG_E("%s ef_get_env_blob(%s) error(read_len=%u)!", __FUNCTION__, cfg_key, read_len);
        return AT_RESULT_FAILE;
    }

    at_server_printf("+UART%cSLAVERADDR: ", uart_x);
    
    /* 转换成字符串输出 */
    size_t slaveraddr_cnt = data_size / sizeof(slaveraddr_list[0]);
    int i = 0;
    if (slaveraddr_cnt > 0)
    {
        for (i = 0; i < (slaveraddr_cnt - 1); ++i)
        {
            at_server_printf("0x%02x,", slaveraddr_list[i]);
        }
        
        at_server_printfln("0x%02x", slaveraddr_list[i]); // 最后一个没有逗号
    }
    
    return AT_RESULT_OK;
}

static at_result_t at_uartxslaveraddr_query(const struct at_cmd *cmd)
{
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    return at_uartxslaveraddr_println(uart_x);
}

static at_result_t at_uartxslaveraddr_setup(const struct at_cmd *cmd, const char *args)
{
    c_str_ref str_ref = { rt_strlen(args) - 1, args + 1 }; // 不包含'='
    c_str_ref *param_list = (c_str_ref*)app_mp_alloc();
    RT_ASSERT(param_list);
    uint32_t param_count = strref_split(&str_ref, ',', param_list, MAX_UARTX_VAR_CNT + 1);
    if ((param_count < 1) || (param_count > MAX_UARTX_VAR_CNT))
    {
        LOG_E("%s strref_split() param_count(%d) not in range[1,%u]!", __FUNCTION__, param_count, MAX_UARTX_VAR_CNT);
        app_mp_free(param_list);
        return AT_RESULT_PARSE_FAILE;
    }
    
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    /* 读取配置的变量个数 */
    uint8_t cfg_var_cnt = 0;
    bool ret = get_variablecnt(uart_x, &cfg_var_cnt);
    if (!ret)
    {
        LOG_E("%s get_variablecnt(UART%c) failed!", __FUNCTION__, uart_x);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }
    
    /* 变量个数检查 */
    if (param_count != cfg_var_cnt)
    {
        LOG_E("%s param_count=%u cfg_var_cnt=%u!", __FUNCTION__, param_count, cfg_var_cnt);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }
    
    /* 转换成u8数组并检查格式 */
    uint8_t slaveraddr_list[MAX_UARTX_VAR_CNT] = {0x00};
    int i = 0;
    for (i = 0; i < param_count; ++i)
    {
        if (param_list[i].len <= 0)
        {
            LOG_E("%s param[%d] is empty!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        int32_t slaveraddr = 0;
        /* 
            %i:整数，如果字符串以0x或者0X开头，则按16进制进行转换，
            如果以0开头，则按8进制进行转换，否则按10进制转换，
            需要一个类型为int*的的参数存放转换结果
        */
        rt_int32_t ret = sscanf(param_list[i].c_str, "%i", &slaveraddr);
        if (ret != 1)
        {
            LOG_E("%s param[%d] format invalid!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        
        if (slaveraddr > 0xFF)
        {
            LOG_E("%s param[%d] not in range[0x00,0xFF]!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        slaveraddr_list[i] = (uint8_t)((uint32_t)slaveraddr);
    }
    
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXslaveraddr";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 保存配置 */
    size_t list_len = (size_t)i;
    EfErrCode ef_ret = ef_set_env_blob(cfg_key, slaveraddr_list, list_len * sizeof(slaveraddr_list[0]));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(%s) error(%d)!", __FUNCTION__, cfg_key, ef_ret);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }

    app_mp_free(param_list);
    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+UART1SLAVERADDR", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxslaveraddr_query, at_uartxslaveraddr_setup, RT_NULL, 1);
AT_CMD_EXPORT("AT+UART2SLAVERADDR", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxslaveraddr_query, at_uartxslaveraddr_setup, RT_NULL, 2);
AT_CMD_EXPORT("AT+UART3SLAVERADDR", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxslaveraddr_query, at_uartxslaveraddr_setup, RT_NULL, 3);
AT_CMD_EXPORT("AT+UART4SLAVERADDR", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxslaveraddr_query, at_uartxslaveraddr_setup, RT_NULL, 4);

/* AT+UARTXFUNCTION 设置/读取UART X相关的功能码列表(每个1字节) */

static at_result_t at_uartxfunction_println(char uart_x)
{
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXfunction";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 读取配置 */
    uint8_t function_list[MAX_UARTX_VAR_CNT] = {0x00};
    size_t data_size = 0;
    size_t read_len = ef_get_env_blob(cfg_key, function_list, sizeof(function_list), &data_size);
    if (read_len != data_size)
    {
        LOG_E("%s ef_get_env_blob(%s) error(read_len=%u)!", __FUNCTION__, cfg_key, read_len);
        return AT_RESULT_FAILE;
    }

    at_server_printf("+UART%cFUNCTION: ", uart_x);
    
    /* 转换成字符串输出 */
    size_t function_cnt = data_size / sizeof(function_list[0]);
    int i = 0;
    if (function_cnt > 0)
    {
        for (i = 0; i < (function_cnt - 1); ++i)
        {
            at_server_printf("0x%02x,", function_list[i]);
        }
        
        at_server_printfln("0x%02x", function_list[i]); // 最后一个没有逗号
    }
    
    return AT_RESULT_OK;
}

static at_result_t at_uartxfunction_query(const struct at_cmd *cmd)
{
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    return at_uartxfunction_println(uart_x);
}

static at_result_t at_uartxfunction_setup(const struct at_cmd *cmd, const char *args)
{
    c_str_ref str_ref = { rt_strlen(args) - 1, args + 1 }; // 不包含'='
    c_str_ref *param_list = (c_str_ref*)app_mp_alloc();
    RT_ASSERT(param_list);
    uint32_t param_count = strref_split(&str_ref, ',', param_list, MAX_UARTX_VAR_CNT + 1);
    if ((param_count < 1) || (param_count > MAX_UARTX_VAR_CNT))
    {
        LOG_E("%s strref_split() param_count(%d) not in range[1,%u]!", __FUNCTION__, param_count, MAX_UARTX_VAR_CNT);
        app_mp_free(param_list);
        return AT_RESULT_PARSE_FAILE;
    }
    
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    /* 读取配置的变量个数 */
    uint8_t cfg_var_cnt = 0;
    bool ret = get_variablecnt(uart_x, &cfg_var_cnt);
    if (!ret)
    {
        LOG_E("%s get_variablecnt(UART%c) failed!", __FUNCTION__, uart_x);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }
    
    /* 变量个数检查 */
    if (param_count != cfg_var_cnt)
    {
        LOG_E("%s param_count=%u cfg_var_cnt=%u!", __FUNCTION__, param_count, cfg_var_cnt);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }
    
    /* 转换成u8数组并检查格式 */
    uint8_t function_list[MAX_UARTX_VAR_CNT] = {0x00};
    int i = 0;
    for (i = 0; i < param_count; ++i)
    {
        if (param_list[i].len <= 0)
        {
            LOG_E("%s param[%d] is empty!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        int32_t function_code = 0;
        /* 
            %i:整数，如果字符串以0x或者0X开头，则按16进制进行转换，
            如果以0开头，则按8进制进行转换，否则按10进制转换，
            需要一个类型为int*的的参数存放转换结果
        */
        rt_int32_t ret = sscanf(param_list[i].c_str, "%i", &function_code);
        if (ret != 1)
        {
            LOG_E("%s param[%d] format invalid!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        /* uartXfunction
            MODBUS_FC_READ_COILS                0x01
            MODBUS_FC_READ_DISCRETE_INPUTS      0x02
            MODBUS_FC_READ_HOLDING_REGISTERS    0x03
            MODBUS_FC_READ_INPUT_REGISTERS      0x04
         */
        if ((function_code < 0x01) || (function_code > 0x04))
        {
            LOG_E("%s param[%d] not in range[0x01,0x04]!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        function_list[i] = (uint8_t)((uint32_t)function_code);
    }
    
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXfunction";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 保存配置 */
    size_t list_len = (size_t)i;
    EfErrCode ef_ret = ef_set_env_blob(cfg_key, function_list, list_len * sizeof(function_list[0]));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(%s) error(%d)!", __FUNCTION__, cfg_key, ef_ret);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }

    app_mp_free(param_list);
    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+UART1FUNCTION", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxfunction_query, at_uartxfunction_setup, RT_NULL, 1);
AT_CMD_EXPORT("AT+UART2FUNCTION", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxfunction_query, at_uartxfunction_setup, RT_NULL, 2);
AT_CMD_EXPORT("AT+UART3FUNCTION", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxfunction_query, at_uartxfunction_setup, RT_NULL, 3);
AT_CMD_EXPORT("AT+UART4FUNCTION", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxfunction_query, at_uartxfunction_setup, RT_NULL, 4);

/* AT+UARTXSTARTADDR 设置/读取UART X相关的寄存器开始地址列表(每个地址2字节) */

static at_result_t at_uartxstartaddr_println(char uart_x)
{
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXstartaddr";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 读取配置 */
    uint16_t addr_list[MAX_UARTX_VAR_CNT] = {0x0000};
    size_t data_size = 0;
    size_t read_len = ef_get_env_blob(cfg_key, addr_list, sizeof(addr_list), &data_size);
    if (read_len != data_size)
    {
        LOG_E("%s ef_get_env_blob(%s) error(read_len=%u)!", __FUNCTION__, cfg_key, read_len);
        return AT_RESULT_FAILE;
    }

    at_server_printf("+UART%cSTARTADDR: ", uart_x);
    
    /* 转换成字符串输出 */
    size_t addr_cnt = data_size / sizeof(addr_list[0]);
    if (addr_cnt > 0)
    {
        int i = 0;
        for (i = 0; i < (addr_cnt - 1); ++i)
        {
            at_server_printf("0x%04x,", addr_list[i]);
        }
        
        at_server_printfln("0x%04x", addr_list[i]);
    }
    
    return AT_RESULT_OK;
}

static at_result_t at_uartxstartaddr_query(const struct at_cmd *cmd)
{
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    return at_uartxstartaddr_println(uart_x);
}

static at_result_t at_uartxstartaddr_setup(const struct at_cmd *cmd, const char *args)
{
    c_str_ref str_ref = { rt_strlen(args) - 1, args + 1 }; // 不包含'='
    c_str_ref *param_list = (c_str_ref*)app_mp_alloc();
    RT_ASSERT(param_list);
    uint32_t param_count = strref_split(&str_ref, ',', param_list, MAX_UARTX_VAR_CNT + 1);
    if ((param_count < 1) || (param_count > MAX_UARTX_VAR_CNT))
    {
        LOG_E("%s strref_split() param_count(%d) not in ramge[1,%u]!", __FUNCTION__, param_count, MAX_UARTX_VAR_CNT);
        app_mp_free(param_list);
        return AT_RESULT_PARSE_FAILE;
    }
    
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    /* 读取配置的变量个数 */
    uint8_t cfg_var_cnt = 0;
    bool ret = get_variablecnt(uart_x, &cfg_var_cnt);
    if (!ret)
    {
        LOG_E("%s get_variablecnt(UART%c) failed!", __FUNCTION__, uart_x);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }
    
    /* 变量个数检查 */
    if (param_count != cfg_var_cnt)
    {
        LOG_E("%s param_count=%u cfg_var_cnt=%u!", __FUNCTION__, param_count, cfg_var_cnt);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }
    
    /* 转换成u16数组并检查格式 */
    uint16_t addr_list[MAX_UARTX_VAR_CNT] = {0x0000};
    int i = 0;
    for (i = 0; i < param_count; ++i)
    {
        if (param_list[i].len <= 0)
        {
            LOG_E("%s param[%d] is empty!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        int32_t addr = 0;
        /* 
            %i:整数，如果字符串以0x或者0X开头，则按16进制进行转换，
            如果以0开头，则按8进制进行转换，否则按10进制转换，
            需要一个类型为int*的的参数存放转换结果
        */
        rt_int32_t ret = sscanf(param_list[i].c_str, "%i", &addr);
        if (ret != 1)
        {
            LOG_E("%s param[%d] format invalid!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        if ((addr < 0) || (addr > 0xFFFF))
        {
            LOG_E("%s param[%d] not in range[0x0000,0xFFFF]!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        addr_list[i] = (uint16_t)((uint32_t)addr);
    }
    
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXstartaddr";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 保存配置 */
    size_t list_len = (size_t)i;
    EfErrCode ef_ret = ef_set_env_blob(cfg_key, addr_list, list_len * sizeof(addr_list[0]));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(%s) error(%d)!", __FUNCTION__, cfg_key, ef_ret);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }
    
    app_mp_free(param_list);

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+UART1STARTADDR", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxstartaddr_query, at_uartxstartaddr_setup, RT_NULL, 1);
AT_CMD_EXPORT("AT+UART2STARTADDR", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxstartaddr_query, at_uartxstartaddr_setup, RT_NULL, 2);
AT_CMD_EXPORT("AT+UART3STARTADDR", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxstartaddr_query, at_uartxstartaddr_setup, RT_NULL, 3);
AT_CMD_EXPORT("AT+UART4STARTADDR", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxstartaddr_query, at_uartxstartaddr_setup, RT_NULL, 4);

/* AT+UARTXLENGTH 设置/读取UART X相关的寄存器数量列表(每个2字节) */

static at_result_t at_uartxlength_println(char uart_x)
{
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXlength";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 读取配置 */
    uint16_t num_list[MAX_UARTX_VAR_CNT] = {0x0000};
    size_t data_size = 0;
    size_t read_len = ef_get_env_blob(cfg_key, num_list, sizeof(num_list), &data_size);
    if (read_len != data_size)
    {
        LOG_E("%s ef_get_env_blob(%s) error(read_len=%u)!", __FUNCTION__, cfg_key, read_len);
        return AT_RESULT_FAILE;
    }

    at_server_printf("+UART%cLENGTH: ", uart_x);
    
    /* 转换成字符串输出 */
    size_t num_cnt = data_size / sizeof(num_list[0]);
    int i = 0;
    if (num_cnt > 0)
    {
        for (i = 0; i < (num_cnt - 1); ++i)
        {
            at_server_printf("0x%04x,", num_list[i]);
        }
        
        at_server_printfln("0x%04x", num_list[i]);
    }
    
    return AT_RESULT_OK;
}

static at_result_t at_uartxlength_query(const struct at_cmd *cmd)
{
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    return at_uartxlength_println(uart_x);
}

static at_result_t at_uartxlength_setup(const struct at_cmd *cmd, const char *args)
{
    c_str_ref str_ref = { rt_strlen(args) - 1, args + 1 }; // 不包含'='
    c_str_ref *param_list = (c_str_ref*)app_mp_alloc();
    RT_ASSERT(param_list);
    uint32_t param_count = strref_split(&str_ref, ',', param_list, MAX_UARTX_VAR_CNT + 1);
    if ((param_count < 1) || (param_count > MAX_UARTX_VAR_CNT))
    {
        LOG_E("%s strref_split() param_count(%d) not in range[1,%u]!", __FUNCTION__, param_count, MAX_UARTX_VAR_CNT);
        app_mp_free(param_list);
        return AT_RESULT_PARSE_FAILE;
    }
    
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    /* 读取配置的变量个数 */
    uint8_t cfg_var_cnt = 0;
    bool ret = get_variablecnt(uart_x, &cfg_var_cnt);
    if (!ret)
    {
        LOG_E("%s get_variablecnt(UART%c) failed!", __FUNCTION__, uart_x);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }
    
    /* 变量个数检查 */
    if (param_count != cfg_var_cnt)
    {
        LOG_E("%s param_count=%u cfg_var_cnt=%u!", __FUNCTION__, param_count, cfg_var_cnt);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }
    
    /* 转换成u16数组并检查格式 */
    uint16_t num_list[MAX_UARTX_VAR_CNT] = {0x0000};
    int i = 0;
    for (i = 0; i < param_count; ++i)
    {
        if (param_list[i].len <= 0)
        {
            LOG_E("%s param[%d] is empty!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        int32_t num = 0;
        /* 
            %i:整数，如果字符串以0x或者0X开头，则按16进制进行转换，
            如果以0开头，则按8进制进行转换，否则按10进制转换，
            需要一个类型为int*的的参数存放转换结果
        */
        rt_int32_t ret = sscanf(param_list[i].c_str, "%i", &num);
        if (ret != 1)
        {
            LOG_E("%s param[%d] format invalid!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        if ((num < 0) || (num > 0xFFFF))
        {
            LOG_E("%s param[%d] not in range[0x0000,0xFFFF]!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        num_list[i] = (uint16_t)((uint32_t)num);
    }
    
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXlength";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 保存配置 */
    size_t list_len = (size_t)i;
    EfErrCode ef_ret = ef_set_env_blob(cfg_key, num_list, list_len * sizeof(num_list[0]));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(%s) error(%d)!", __FUNCTION__, cfg_key, ef_ret);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }
    
    app_mp_free(param_list);

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+UART1LENGTH", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxlength_query, at_uartxlength_setup, RT_NULL, 1);
AT_CMD_EXPORT("AT+UART2LENGTH", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxlength_query, at_uartxlength_setup, RT_NULL, 2);
AT_CMD_EXPORT("AT+UART3LENGTH", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxlength_query, at_uartxlength_setup, RT_NULL, 3);
AT_CMD_EXPORT("AT+UART4LENGTH", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxlength_query, at_uartxlength_setup, RT_NULL, 4);

/* AT+UARTXTYPE 设置/读取UART X相关的数据类型列表(每个1字节) */

static at_result_t at_uartxtype_println(char uart_x)
{
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXtype";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 读取配置 */
    uint8_t type_list[MAX_UARTX_VAR_CNT] = {0x00};
    size_t data_size = 0;
    size_t read_len = ef_get_env_blob(cfg_key, type_list, sizeof(type_list), &data_size);
    if (read_len != data_size)
    {
        LOG_E("%s ef_get_env_blob(%s) error(read_len=%u)!", __FUNCTION__, cfg_key, read_len);
        return AT_RESULT_FAILE;
    }

    at_server_printf("+UART%cTYPE: ", uart_x);
    
    /* 转换成字符串输出 */
    size_t type_cnt = data_size / sizeof(type_list[0]);
    int i = 0;
    if (type_cnt > 0)
    {
        for (i = 0; i < (type_cnt - 1); ++i)
        {
            at_server_printf("0x%02x,", type_list[i]);
        }
        
        at_server_printfln("0x%02x", type_list[i]); // 最后一个没有逗号
    }
    
    return AT_RESULT_OK;
}

static at_result_t at_uartxtype_query(const struct at_cmd *cmd)
{
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    return at_uartxtype_println(uart_x);
}

static at_result_t at_uartxtype_setup(const struct at_cmd *cmd, const char *args)
{
    c_str_ref str_ref = { rt_strlen(args) - 1, args + 1 }; // 不包含'='
    c_str_ref *param_list = (c_str_ref*)app_mp_alloc();
    RT_ASSERT(param_list);
    uint32_t param_count = strref_split(&str_ref, ',', param_list, MAX_UARTX_VAR_CNT + 1);
    if ((param_count < 1) || (param_count > MAX_UARTX_VAR_CNT))
    {
        LOG_E("%s strref_split() param_count(%d) not in range[1,%u]!", __FUNCTION__, param_count, MAX_UARTX_VAR_CNT);
        app_mp_free(param_list);
        return AT_RESULT_PARSE_FAILE;
    }
    
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    /* 读取配置的变量个数 */
    uint8_t cfg_var_cnt = 0;
    bool ret = get_variablecnt(uart_x, &cfg_var_cnt);
    if (!ret)
    {
        LOG_E("%s get_variablecnt(UART%c) failed!", __FUNCTION__, uart_x);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }
    
    /* 变量个数检查 */
    if (param_count != cfg_var_cnt)
    {
        LOG_E("%s param_count=%u cfg_var_cnt=%u!", __FUNCTION__, param_count, cfg_var_cnt);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }
    
    /* 转换成u8数组并检查格式 */
    uint8_t type_list[MAX_UARTX_VAR_CNT] = {0x00};
    int i = 0;
    for (i = 0; i < param_count; ++i)
    {
        if (param_list[i].len <= 0)
        {
            LOG_E("%s param[%d] is empty!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        int32_t type = 0;
        /* 
            %i:整数，如果字符串以0x或者0X开头，则按16进制进行转换，
            如果以0开头，则按8进制进行转换，否则按10进制转换，
            需要一个类型为int*的的参数存放转换结果
        */
        rt_int32_t ret = sscanf(param_list[i].c_str, "%i", &type);
        if (ret != 1)
        {
            LOG_E("%s param[%d] format invalid!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        /* uartXtype
         *  0x00=有符号16位int(AB)
         *  0x01=有符号16位int(BA)
         *  0x02=无符号16位int(AB)
         *  0x03=无符号16位int(BA)
         *  0x04=有符号32位int(ABCD)
         *  0x05=有符号32位int(DCBA)
         *  0x06=有符号32位int(BADC)
         *  0x07=有符号32位int(CDAB)
         *  0x08=无符号32位int(ABCD)
         *  0x09=无符号32位int(DCBA)
         *  0x0A=无符号32位int(BADC)
         *  0x0B=无符号32位int(CDAB)
         *  0x0C=IEEE754浮点数(ABCD)
         *  0x0D=IEEE754浮点数(DCBA)
         *  0x0E=IEEE754浮点数(BADC)
         *  0x0F=IEEE754浮点数(CDAB)
         *  0x10=位(只有功能码01和02有此类型,数据为0或者1)
         *  0x11=无符号8位int,取字节高位：AB取A
         *  0x12=无符号8位int,取字节低位：AB取B
         */
        if ((type < 0x00) || (type > 0x12))
        {
            LOG_E("%s param[%d] not in range[0x00,0x12]!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        type_list[i] = (uint8_t)((uint32_t)type);
    }
    
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXtype";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 保存配置 */
    size_t list_len = (size_t)i;
    EfErrCode ef_ret = ef_set_env_blob(cfg_key, type_list, list_len * sizeof(type_list[0]));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(%s) error(%d)!", __FUNCTION__, cfg_key, ef_ret);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }

    app_mp_free(param_list);
    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+UART1TYPE", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxtype_query, at_uartxtype_setup, RT_NULL, 1);
AT_CMD_EXPORT("AT+UART2TYPE", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxtype_query, at_uartxtype_setup, RT_NULL, 2);
AT_CMD_EXPORT("AT+UART3TYPE", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxtype_query, at_uartxtype_setup, RT_NULL, 3);
AT_CMD_EXPORT("AT+UART4TYPE", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxtype_query, at_uartxtype_setup, RT_NULL, 4);

/* AT+UARTXDELAY 设置/读取UART X相关的采集延时列表(每个1字节) */

static at_result_t at_uartxdelay_println(char uart_x)
{
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXdelay";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 读取配置 */
    uint8_t delay_list[MAX_UARTX_VAR_CNT] = {0x00};
    size_t data_size = 0;
    size_t read_len = ef_get_env_blob(cfg_key, delay_list, sizeof(delay_list), &data_size);
    if (read_len != data_size)
    {
        LOG_E("%s ef_get_env_blob(%s) error(read_len=%u)!", __FUNCTION__, cfg_key, read_len);
        return AT_RESULT_FAILE;
    }

    at_server_printf("+UART%cDELAY: ", uart_x);
    
    /* 转换成字符串输出 */
    size_t delay_cnt = data_size / sizeof(delay_list[0]);
    int i = 0;
    if (delay_cnt > 0)
    {
        for (i = 0; i < (delay_cnt - 1); ++i)
        {
            at_server_printf("0x%02x,", delay_list[i]);
        }
        
        at_server_printfln("0x%02x", delay_list[i]); // 最后一个没有逗号
    }
    
    return AT_RESULT_OK;
}

static at_result_t at_uartxdelay_query(const struct at_cmd *cmd)
{
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    return at_uartxdelay_println(uart_x);
}

static at_result_t at_uartxdelay_setup(const struct at_cmd *cmd, const char *args)
{
    c_str_ref str_ref = { rt_strlen(args) - 1, args + 1 }; // 不包含'='
    c_str_ref *param_list = (c_str_ref*)app_mp_alloc();
    RT_ASSERT(param_list);
    uint32_t param_count = strref_split(&str_ref, ',', param_list, MAX_UARTX_VAR_CNT + 1);
    if ((param_count < 1) || (param_count > MAX_UARTX_VAR_CNT))
    {
        LOG_E("%s strref_split() param_count(%d) not in range[1,%u]!", __FUNCTION__, param_count, MAX_UARTX_VAR_CNT);
        app_mp_free(param_list);
        return AT_RESULT_PARSE_FAILE;
    }
    
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    /* 读取配置的变量个数 */
    uint8_t cfg_var_cnt = 0;
    bool ret = get_variablecnt(uart_x, &cfg_var_cnt);
    if (!ret)
    {
        LOG_E("%s get_variablecnt(UART%c) failed!", __FUNCTION__, uart_x);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }
    
    /* 变量个数检查 */
    if (param_count != cfg_var_cnt)
    {
        LOG_E("%s param_count=%u cfg_var_cnt=%u!", __FUNCTION__, param_count, cfg_var_cnt);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }
    
    /* 转换成u8数组并检查格式 */
    uint8_t delay_list[MAX_UARTX_VAR_CNT] = {0x00};
    int i = 0;
    for (i = 0; i < param_count; ++i)
    {
        if (param_list[i].len <= 0)
        {
            LOG_E("%s param[%d] is empty!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        int32_t delay = 0;
        /* 
            %i:整数，如果字符串以0x或者0X开头，则按16进制进行转换，
            如果以0开头，则按8进制进行转换，否则按10进制转换，
            需要一个类型为int*的的参数存放转换结果
        */
        rt_int32_t ret = sscanf(param_list[i].c_str, "%i", &delay);
        if (ret != 1)
        {
            LOG_E("%s param[%d] format invalid!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        /* uartXdelay
         * 范围0x01~0x14，对应100ms~2000ms
         */
        if ((delay < 0x01) || (delay > 0x14))
        {
            LOG_E("%s param[%d] not in range[0x01,0x14]!", __FUNCTION__, i);
            app_mp_free(param_list);
            return AT_RESULT_PARSE_FAILE;
        }
        delay_list[i] = (uint8_t)((uint32_t)delay);
    }
    
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXdelay";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 保存配置 */
    size_t list_len = (size_t)i;
    EfErrCode ef_ret = ef_set_env_blob(cfg_key, delay_list, list_len * sizeof(delay_list[0]));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(%s) error(%d)!", __FUNCTION__, cfg_key, ef_ret);
        app_mp_free(param_list);
        return AT_RESULT_FAILE;
    }

    app_mp_free(param_list);
    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+UART1DELAY", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxdelay_query, at_uartxdelay_setup, RT_NULL, 1);
AT_CMD_EXPORT("AT+UART2DELAY", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxdelay_query, at_uartxdelay_setup, RT_NULL, 2);
AT_CMD_EXPORT("AT+UART3DELAY", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxdelay_query, at_uartxdelay_setup, RT_NULL, 3);
AT_CMD_EXPORT("AT+UART4DELAY", ARGS_EXPR_VAR_PARM100, RT_NULL, at_uartxdelay_query, at_uartxdelay_setup, RT_NULL, 4);

/* AT+UARTXSETTINGINF 读取UART X相关的配置信息 */

static at_result_t at_uartxsettinginf_exec(const struct at_cmd *cmd)
{
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    at_server_printfln("+UART%cSETTINGINF: ", uart_x);
    
    at_uartxvariable_println(uart_x);
    at_uartxvariablecnt_println(uart_x);
    at_uartxbaudrate_println(uart_x);
    at_uartxwordlength_println(uart_x);
    at_uartxparity_println(uart_x);
    at_uartxstopbits_println(uart_x);
    at_uartxslaveraddr_println(uart_x);
    at_uartxfunction_println(uart_x);
    at_uartxstartaddr_println(uart_x);
    at_uartxlength_println(uart_x);
    at_uartxtype_println(uart_x);
    at_uartxdelay_println(uart_x);
    
    return AT_RESULT_OK;
}
AT_CMD_EXPORT("AT+UART1SETTINGINF", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_uartxsettinginf_exec, 1);
AT_CMD_EXPORT("AT+UART2SETTINGINF", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_uartxsettinginf_exec, 2);
AT_CMD_EXPORT("AT+UART3SETTINGINF", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_uartxsettinginf_exec, 3);
AT_CMD_EXPORT("AT+UART4SETTINGINF", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_uartxsettinginf_exec, 4);
