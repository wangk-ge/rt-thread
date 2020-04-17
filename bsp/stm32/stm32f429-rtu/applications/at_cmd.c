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

#define LOG_TAG              "main.at_cmd"
#define LOG_LVL              LOG_LVL_DBG
#include <rtdbg.h>

/* 重启系统 */
static at_result_t at_reboot_exec(void)
{
    rt_hw_cpu_reset();
    
    return AT_RESULT_OK;
}
AT_CMD_EXPORT("AT+REBOOT", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_reboot_exec);

/* ULOG相关AT指令 */

static at_result_t at_ulog_tag_lvl_query(void)
{
    if (!rt_slist_isempty(ulog_tag_lvl_list_get()))
    {
        rt_slist_t *node;
        ulog_tag_lvl_filter_t tag_lvl = NULL;
        
        /* show the tag level list */
        for (node = rt_slist_first(ulog_tag_lvl_list_get()); node; node = rt_slist_next(node))
        {
            tag_lvl = rt_slist_entry(node, struct ulog_tag_lvl_filter, list);
            at_server_printfln("%*.s,%d", ULOG_FILTER_TAG_MAX_LEN, tag_lvl->tag, tag_lvl->level);
        }
    }

    return AT_RESULT_OK;
}

static at_result_t at_ulog_tag_lvl_setup(const char *args)
{
    char tag[ULOG_FILTER_TAG_MAX_LEN] = "";
    rt_uint32_t level = 0;
    char req_expr[16] = "";
    rt_sprintf(req_expr, "=%%%d[^,],%%d", sizeof(tag) - 1);

    int argc = at_req_parse_args(args, req_expr, tag, &level);
    if ((argc < 1) || (argc > 2))
    {
        return AT_RESULT_PARSE_FAILE;
    }
    
    if (argc == 1)
    {
        level = ulog_tag_lvl_filter_get(tag);
        at_server_printfln("%s,%d", tag, level);
    }
    else //if (argc == 2)
    {
        if (level > LOG_FILTER_LVL_ALL)
        {
            return AT_RESULT_CHECK_FAILE;
        }
        
        int ret = ulog_tag_lvl_filter_set(tag, level);
        if (ret != RT_EOK)
        {
            return AT_RESULT_FAILE;
        }
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+ULOGTAGLVL", "=<tag>[,<level>]", RT_NULL, at_ulog_tag_lvl_query, at_ulog_tag_lvl_setup, RT_NULL);

static at_result_t at_ulog_global_tag_query(void)
{
    const char* tag = ulog_global_filter_tag_get();
    if (tag != NULL)
    {
        at_server_printfln("%s", tag);
    }
    
    return AT_RESULT_OK;
}

static at_result_t at_ulog_global_tag_setup(const char *args)
{
    char tag[ULOG_FILTER_TAG_MAX_LEN] = "";
    char req_expr[16] = "";
    rt_sprintf(req_expr, "=%%%ds", sizeof(tag) - 1);

    if (rt_strlen(args) > sizeof(tag))
    {
        return AT_RESULT_CHECK_FAILE;
    }
    
    int argc = at_req_parse_args(args, req_expr, tag);
    if (argc != 1)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    ulog_global_filter_tag_set(tag);

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+ULOGGLBTAG", "=<tag>", RT_NULL, at_ulog_global_tag_query, at_ulog_global_tag_setup, RT_NULL);

static at_result_t at_ulog_global_lvl_query(void)
{
    at_server_printfln("%d", ulog_global_filter_lvl_get());

    return AT_RESULT_OK;
}

static at_result_t at_ulog_global_lvl_setup(const char *args)
{
    rt_uint32_t level = 0;
    const char *req_expr = "=%d";

    int argc = at_req_parse_args(args, req_expr, &level);
    if (argc != 1)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    
    if (level > LOG_FILTER_LVL_ALL)
    {
        return AT_RESULT_CHECK_FAILE;
    }

    ulog_global_filter_lvl_set(level);

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+ULOGGLBLVL", "=<level>", RT_NULL, at_ulog_global_lvl_query, at_ulog_global_lvl_setup, RT_NULL);

static at_result_t at_ulog_global_kw_query(void)
{
    const char* kw = ulog_global_filter_kw_get();
    if (kw != NULL)
    {
        at_server_printfln("%s", kw);
    }

    return AT_RESULT_OK;
}

static at_result_t at_ulog_global_kw_setup(const char *args)
{
    char keyword[ULOG_FILTER_KW_MAX_LEN] = "";
    char req_expr[16] = "";
    rt_sprintf(req_expr, "=%%%ds", sizeof(keyword) - 1);

    if (rt_strlen(args) > sizeof(keyword))
    {
        return AT_RESULT_CHECK_FAILE;
    }
    
    int argc = at_req_parse_args(args, req_expr, keyword);
    if (argc != 1)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    at_server_printfln("keyword : %s", keyword);

    ulog_global_filter_kw_set(keyword);

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+ULOGGLBKW", "=<keyword>", RT_NULL, at_ulog_global_kw_query, at_ulog_global_kw_setup, RT_NULL);
