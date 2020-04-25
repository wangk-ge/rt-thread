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

#define LOG_TAG              "main.at_cmd_uart"
#define LOG_LVL              LOG_LVL_DBG
#include <rtdbg.h>

/* 最长20个可变参数个数格式AT指令表达式 */
#define ARGS_EXPR_VAR_PARM20 "=<p1>[,<p2>][,<p3>][,<p4>][,<p5>][,<p6>][,<p7>][,<p8>][,<p9>][,<p10>]" \
    "[,<p11>][,<p12>][,<p13>][,<p14>][,<p15>][,<p16>][,<p17>][,<p18>][,<p19>][,<p20>]"

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

/* AT+UARTXVARIABLE 设置/读取UART X相关的变量名列表(变量名最长20个字符,个数不定,最多20个) */

static at_result_t at_uartxvariable_println(char uart_x)
{
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXvariable";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 取得配置数据长度 */
    size_t data_len = 0;
    ef_get_env_blob(cfg_key, RT_NULL, 0, &data_len);
    if (data_len > 0)
    {
        /* 分配内存 */
        char *var_list = (char*)rt_malloc(data_len + 1);
        if (var_list == RT_NULL)
        {
            LOG_E("%s rt_malloc(%u) failed!", __FUNCTION__, data_len);
            return AT_RESULT_FAILE;
        }
        
        /* 读取配置 */
        size_t read_len = ef_get_env_blob(cfg_key, var_list, data_len, RT_NULL);
        if (read_len != data_len)
        {
            /* 释放内存 */
            rt_free(var_list);
            
            LOG_E("%s ef_get_env_blob(%s) error(data_len=%u,read_len=%u)!", __FUNCTION__, cfg_key, data_len, read_len);
            return AT_RESULT_FAILE;
        }
        var_list[data_len] = '\0';
        
        at_server_printfln("+UART%cVARIABLE: %s", uart_x, var_list);
        
        /* 释放内存 */
        rt_free(var_list);
    }

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
    c_str_ref param_list[21] = {{0}};
    uint32_t param_count = strref_split(&str_ref, ',', param_list, ARRAY_SIZE(param_list));
    if ((param_count < 1) || (param_count > 20))
    {
        LOG_E("%s strref_split() param number(%d)<20!", __FUNCTION__, param_count);
        return AT_RESULT_PARSE_FAILE;
    }
    
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    /* 读取配置的变量个数 */
    uint8_t cfg_var_cnt = 0;
    bool ret = get_variablecnt(uart_x, &cfg_var_cnt);
    if (!ret)
    {
        return AT_RESULT_FAILE;
    }
    
    /* 变量个数检查 */
    if (param_count != cfg_var_cnt)
    {
        LOG_E("%s param_count=%u cfg_var_cnt=%u!", __FUNCTION__, param_count, cfg_var_cnt);
        return AT_RESULT_FAILE;
    }
    
    // 检查参数格式(长度)
    int i = 0;
    for (i = 0; i < param_count; ++i)
    {
        if (param_list[i].len > 20)
        {
            LOG_E("%s param[%d] format invalid!", __FUNCTION__, i);
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
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+UART1VARIABLE", ARGS_EXPR_VAR_PARM20, RT_NULL, at_uartxvariable_query, at_uartxvariable_setup, RT_NULL, 1);
AT_CMD_EXPORT("AT+UART2VARIABLE", ARGS_EXPR_VAR_PARM20, RT_NULL, at_uartxvariable_query, at_uartxvariable_setup, RT_NULL, 2);
AT_CMD_EXPORT("AT+UART3VARIABLE", ARGS_EXPR_VAR_PARM20, RT_NULL, at_uartxvariable_query, at_uartxvariable_setup, RT_NULL, 3);
AT_CMD_EXPORT("AT+UART4VARIABLE", ARGS_EXPR_VAR_PARM20, RT_NULL, at_uartxvariable_query, at_uartxvariable_setup, RT_NULL, 4);

/* AT+UARTXVARIABLECNT 设置/读取UART X相关的变量个数(最多20个) */

static at_result_t at_uartxvariablecnt_println(char uart_x)
{
    uint8_t var_cnt = 0;
    bool ret = get_variablecnt(uart_x, &var_cnt);
    if (!ret)
    {
        //LOG_E("%s get_variablecnt error!");
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
    
    if (cnt > 20)
    {
        LOG_E("%s client_id(%u)>20!", __FUNCTION__, cnt);
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

/* AT+UARTXSLAVERADDR 设置/读取UART X从机地址 */

static at_result_t at_uartxslaveraddr_println(char uart_x)
{
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXslaveraddr";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 读取配置 */
    uint8_t addr = 0;
    size_t len = ef_get_env_blob(cfg_key, &addr, sizeof(addr), RT_NULL);
    if (len != sizeof(addr))
    {
        LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, cfg_key);
        return AT_RESULT_FAILE;
    }
    
    at_server_printfln("+UART%cSLAVERADDR: 0x%02x", uart_x, addr);

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
    rt_uint32_t addr = 0;
    const char *req_expr = "=%x";

    int argc = at_req_parse_args(args, req_expr, &addr);
    if (argc != 1)
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }
    
    // 检查数据从机地址有效性
    if (addr > 0xFF)
    {
        LOG_E("%s addr(0x%x) range[0,0xFF]!", __FUNCTION__, addr);
        return AT_RESULT_CHECK_FAILE;
    }
    
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXslaveraddr";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 保存配置 */
    uint8_t addr_val = (uint8_t)addr;
    EfErrCode ef_ret = ef_set_env_blob(cfg_key, &addr_val, sizeof(addr_val));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(%s,0x%02x) error(%d)!", __FUNCTION__, cfg_key, addr_val, ef_ret);
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+UART1SLAVERADDR", "=<addr>", RT_NULL, at_uartxslaveraddr_query, at_uartxslaveraddr_setup, RT_NULL, 1);
AT_CMD_EXPORT("AT+UART2SLAVERADDR", "=<addr>", RT_NULL, at_uartxslaveraddr_query, at_uartxslaveraddr_setup, RT_NULL, 2);
AT_CMD_EXPORT("AT+UART3SLAVERADDR", "=<addr>", RT_NULL, at_uartxslaveraddr_query, at_uartxslaveraddr_setup, RT_NULL, 3);
AT_CMD_EXPORT("AT+UART4SLAVERADDR", "=<addr>", RT_NULL, at_uartxslaveraddr_query, at_uartxslaveraddr_setup, RT_NULL, 4);

/* AT+UARTXFUNCTION 设置/读取UART X功能吗 */

static at_result_t at_uartxfunction_println(char uart_x)
{
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXfunction";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 读取配置 */
    uint8_t code = 0;
    size_t len = ef_get_env_blob(cfg_key, &code, sizeof(code), RT_NULL);
    if (len != sizeof(code))
    {
        LOG_E("%s ef_get_env_blob(%s) error!", __FUNCTION__, cfg_key);
        return AT_RESULT_FAILE;
    }
    
    at_server_printfln("+UART%cSLAVERADDR: 0x%02x", uart_x, code);

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
    rt_uint32_t code = 0;
    const char *req_expr = "=%x";

    int argc = at_req_parse_args(args, req_expr, &code);
    if (argc != 1)
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }
    
    // 检查功能吗有效性
    if (code > 0xFF)
    {
        LOG_E("%s code(0x%x) range[0,0xFF]!", __FUNCTION__, code);
        return AT_RESULT_CHECK_FAILE;
    }
    
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXfunction";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 保存配置 */
    uint8_t code_val = (uint8_t)code;
    EfErrCode ef_ret = ef_set_env_blob(cfg_key, &code_val, sizeof(code_val));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(%s,0x%02x) error(%d)!", __FUNCTION__, cfg_key, code_val, ef_ret);
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+UART1FUNCTION", "=<code>", RT_NULL, at_uartxfunction_query, at_uartxfunction_setup, RT_NULL, 1);
AT_CMD_EXPORT("AT+UART2FUNCTION", "=<code>", RT_NULL, at_uartxfunction_query, at_uartxfunction_setup, RT_NULL, 2);
AT_CMD_EXPORT("AT+UART3FUNCTION", "=<code>", RT_NULL, at_uartxfunction_query, at_uartxfunction_setup, RT_NULL, 3);
AT_CMD_EXPORT("AT+UART4FUNCTION", "=<code>", RT_NULL, at_uartxfunction_query, at_uartxfunction_setup, RT_NULL, 4);

/* AT+UARTXSTARTADDR 设置/读取UART X相关的寄存器开始地址列表(每个地址2字节,个数不定,最多20个) */

static at_result_t at_uartxstartaddr_println(char uart_x)
{
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXstartaddr";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 读取配置 */
    uint16_t addr_list[20] = {0x0000};
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
    c_str_ref param_list[21] = {{0}};
    uint32_t param_count = strref_split(&str_ref, ',', param_list, ARRAY_SIZE(param_list));
    if ((param_count < 1) || (param_count > 20))
    {
        LOG_E("%s strref_split() param number(%d)<20!", __FUNCTION__, param_count);
        return AT_RESULT_PARSE_FAILE;
    }
    
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    /* 读取配置的变量个数 */
    uint8_t cfg_var_cnt = 0;
    bool ret = get_variablecnt(uart_x, &cfg_var_cnt);
    if (!ret)
    {
        return AT_RESULT_FAILE;
    }
    
    /* 变量个数检查 */
    if (param_count != cfg_var_cnt)
    {
        LOG_E("%s param_count=%u cfg_var_cnt=%u!", __FUNCTION__, param_count, cfg_var_cnt);
        return AT_RESULT_FAILE;
    }
    
    /* 转换成u16数组并检查格式 */
    uint16_t addr_list[20] = {0x0000};
    int i = 0;
    for (i = 0; i < param_count; ++i)
    {
        if (param_list[i].len <= 0)
        {
            LOG_E("%s param[%d] is empty!", __FUNCTION__, i);
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
            return AT_RESULT_PARSE_FAILE;
        }
        if ((addr < 0) || (addr > 0xFFFF))
        {
            LOG_E("%s param[%d] not in range[0x0000,0xFFFF]!", __FUNCTION__, i);
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
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+UART1STARTADDR", ARGS_EXPR_VAR_PARM20, RT_NULL, at_uartxstartaddr_query, at_uartxstartaddr_setup, RT_NULL, 1);
AT_CMD_EXPORT("AT+UART2STARTADDR", ARGS_EXPR_VAR_PARM20, RT_NULL, at_uartxstartaddr_query, at_uartxstartaddr_setup, RT_NULL, 2);
AT_CMD_EXPORT("AT+UART3STARTADDR", ARGS_EXPR_VAR_PARM20, RT_NULL, at_uartxstartaddr_query, at_uartxstartaddr_setup, RT_NULL, 3);
AT_CMD_EXPORT("AT+UART4STARTADDR", ARGS_EXPR_VAR_PARM20, RT_NULL, at_uartxstartaddr_query, at_uartxstartaddr_setup, RT_NULL, 4);

/* AT+UARTXLENGTH 设置/读取UART X相关的寄存器数量列表(每个2字节,个数不定,最多20个) */

static at_result_t at_uartxlength_println(char uart_x)
{
    /* 生成配置KEY值 */
    char cfg_key[32] = "uartXlength";
    cfg_key[STR_LEN("uart")] = uart_x;
    
    /* 读取配置 */
    uint16_t num_list[20] = {0x0000};
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
    c_str_ref param_list[21] = {{0}};
    uint32_t param_count = strref_split(&str_ref, ',', param_list, ARRAY_SIZE(param_list));
    if ((param_count < 1) || (param_count > 20))
    {
        LOG_E("%s strref_split() param number(%d)<20!", __FUNCTION__, param_count);
        return AT_RESULT_PARSE_FAILE;
    }
    
    /* 串口号UART X的X实际字符 */
    char uart_x = *(cmd->name + STR_LEN("AT+UART"));
    
    /* 读取配置的变量个数 */
    uint8_t cfg_var_cnt = 0;
    bool ret = get_variablecnt(uart_x, &cfg_var_cnt);
    if (!ret)
    {
        return AT_RESULT_FAILE;
    }
    
    /* 变量个数检查 */
    if (param_count != cfg_var_cnt)
    {
        LOG_E("%s param_count=%u cfg_var_cnt=%u!", __FUNCTION__, param_count, cfg_var_cnt);
        return AT_RESULT_FAILE;
    }
    
    /* 转换成u16数组并检查格式 */
    uint16_t num_list[20] = {0x0000};
    int i = 0;
    for (i = 0; i < param_count; ++i)
    {
        if (param_list[i].len <= 0)
        {
            LOG_E("%s param[%d] is empty!", __FUNCTION__, i);
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
            return AT_RESULT_PARSE_FAILE;
        }
        if ((num < 0) || (num > 0xFFFF))
        {
            LOG_E("%s param[%d] not in range[0x0000,0xFFFF]!", __FUNCTION__, i);
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
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+UART1LENGTH", ARGS_EXPR_VAR_PARM20, RT_NULL, at_uartxlength_query, at_uartxlength_setup, RT_NULL, 1);
AT_CMD_EXPORT("AT+UART2LENGTH", ARGS_EXPR_VAR_PARM20, RT_NULL, at_uartxlength_query, at_uartxlength_setup, RT_NULL, 2);
AT_CMD_EXPORT("AT+UART3LENGTH", ARGS_EXPR_VAR_PARM20, RT_NULL, at_uartxlength_query, at_uartxlength_setup, RT_NULL, 3);
AT_CMD_EXPORT("AT+UART4LENGTH", ARGS_EXPR_VAR_PARM20, RT_NULL, at_uartxlength_query, at_uartxlength_setup, RT_NULL, 4);

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
    
    return AT_RESULT_OK;
}
AT_CMD_EXPORT("AT+UART1SETTINGINF", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_uartxsettinginf_exec, 1);
AT_CMD_EXPORT("AT+UART2SETTINGINF", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_uartxsettinginf_exec, 2);
AT_CMD_EXPORT("AT+UART3SETTINGINF", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_uartxsettinginf_exec, 3);
AT_CMD_EXPORT("AT+UART4SETTINGINF", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_uartxsettinginf_exec, 4);
