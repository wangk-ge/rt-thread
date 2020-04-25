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

#define LOG_TAG              "main.at_cmd_ulog"
#define LOG_LVL              LOG_LVL_DBG
#include <rtdbg.h>

/* ULOG相关AT指令 */

/* AT+ULOGTAGLVL 设置指定tag对应日志的输出等级 */

static at_result_t at_ulog_tag_lvl_query(const struct at_cmd *cmd)
{
    if (!rt_slist_isempty(ulog_tag_lvl_list_get()))
    {
        rt_slist_t *node;
        ulog_tag_lvl_filter_t tag_lvl = NULL;
        
        /* show the tag level list */
        for (node = rt_slist_first(ulog_tag_lvl_list_get()); node; node = rt_slist_next(node))
        {
            tag_lvl = rt_slist_entry(node, struct ulog_tag_lvl_filter, list);
            at_server_printfln("%.*s,%d", ULOG_FILTER_TAG_MAX_LEN, tag_lvl->tag, tag_lvl->level);
        }
    }

    return AT_RESULT_OK;
}

static at_result_t at_ulog_tag_lvl_setup(const struct at_cmd *cmd, const char *args)
{
    char tag[ULOG_FILTER_TAG_MAX_LEN] = "";
    rt_uint32_t level = 0;
    char req_expr[16] = "";
    rt_sprintf(req_expr, "=%%%d[^,],%%d", sizeof(tag) - 1);

    int argc = at_req_parse_args(args, req_expr, tag, &level);
    if ((argc < 1) || (argc > 2))
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1 or 2!", __FUNCTION__, req_expr, argc);
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
            LOG_E("%s <level>(%d)>%d!", __FUNCTION__, level, LOG_FILTER_LVL_ALL);
            return AT_RESULT_CHECK_FAILE;
        }
        
        int ret = ulog_tag_lvl_filter_set(tag, level);
        if (ret != RT_EOK)
        {
            LOG_E("%s ulog_tag_lvl_filter_set(%s,%d) error(%d)!", __FUNCTION__, tag, level, ret);
            return AT_RESULT_FAILE;
        }
    }

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+ULOGTAGLVL", "=<tag>[,<level>]", RT_NULL, at_ulog_tag_lvl_query, at_ulog_tag_lvl_setup, RT_NULL, 0);

/* AT+ULOGTAG 设置全局日志过滤tag */

static at_result_t at_ulog_global_tag_query(const struct at_cmd *cmd)
{
    const char* tag = ulog_global_filter_tag_get();
    if (tag != NULL)
    {
        at_server_printfln("%s", tag);
    }
    
    return AT_RESULT_OK;
}

static at_result_t at_ulog_global_tag_setup(const struct at_cmd *cmd, const char *args)
{
    char tag[ULOG_FILTER_TAG_MAX_LEN] = "";
    char req_expr[16] = "";
    rt_sprintf(req_expr, "=%%%ds", sizeof(tag) - 1);

    if (rt_strlen(args) > sizeof(tag))
    {
        LOG_E("%s rt_strlen(args)>%d!", __FUNCTION__, sizeof(tag));
        return AT_RESULT_CHECK_FAILE;
    }
    
    int argc = at_req_parse_args(args, req_expr, tag);
    if (argc != 1)
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }

    ulog_global_filter_tag_set(tag);

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+ULOGTAG", "=<tag>", RT_NULL, at_ulog_global_tag_query, at_ulog_global_tag_setup, RT_NULL, 0);

/* AT+ULOGLVL 设置全局日志等级 */

static at_result_t at_ulog_global_lvl_query(const struct at_cmd *cmd)
{
    at_server_printfln("%d", ulog_global_filter_lvl_get());

    return AT_RESULT_OK;
}

static at_result_t at_ulog_global_lvl_setup(const struct at_cmd *cmd, const char *args)
{
    rt_uint32_t level = 0;
    rt_uint32_t save = 0;
    const char *req_expr = "=%d,%d";

    int argc = at_req_parse_args(args, req_expr, &level, &save);
    if ((argc < 1) || (argc > 2))
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1 or 2!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }
    
    if (level > LOG_FILTER_LVL_ALL)
    {
        LOG_E("%s <level>(%d)>%d!", __FUNCTION__, level, LOG_FILTER_LVL_ALL);
        return AT_RESULT_CHECK_FAILE;
    }
    
    if (argc == 2)
    {
        if (save > 1)
        {
            LOG_E("%s <save>(%d) !=0 or 1!", __FUNCTION__, save);
            return AT_RESULT_CHECK_FAILE;
        }

        if (save)
        {
            uint8_t level_val = (uint8_t)level;
            EfErrCode ef_ret = ef_set_env_blob("ulog_glb_lvl", &level_val, 1);
            if (ef_ret != EF_NO_ERR)
            {
                LOG_E("%s ef_set_env_blob(ulog_glb_lvl,%u) error(%d)!", __FUNCTION__, level_val, ef_ret);
                return AT_RESULT_FAILE;
            }
        }
    }
    
    ulog_global_filter_lvl_set(level);

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+ULOGLVL", "=<level>[,<save>]", RT_NULL, at_ulog_global_lvl_query, at_ulog_global_lvl_setup, RT_NULL, 0);

/* AT+ULOGKW 设置全局日志过滤关键字 */

static at_result_t at_ulog_global_kw_query(const struct at_cmd *cmd)
{
    const char* kw = ulog_global_filter_kw_get();
    if (kw != NULL)
    {
        at_server_printfln("%s", kw);
    }

    return AT_RESULT_OK;
}

static at_result_t at_ulog_global_kw_setup(const struct at_cmd *cmd, const char *args)
{
    char keyword[ULOG_FILTER_KW_MAX_LEN] = "";
    char req_expr[16] = "";
    rt_sprintf(req_expr, "=%%%ds", sizeof(keyword) - 1);

    if (rt_strlen(args) > sizeof(keyword))
    {
        LOG_E("%s rt_strlen(args)>%d!", __FUNCTION__, sizeof(keyword));
        return AT_RESULT_CHECK_FAILE;
    }
    
    int argc = at_req_parse_args(args, req_expr, keyword);
    if (argc != 1)
    {
        LOG_E("%s at_req_parse_args(%s) argc(%d)!=1!", __FUNCTION__, req_expr, argc);
        return AT_RESULT_PARSE_FAILE;
    }

    at_server_printfln("keyword : %s", keyword);

    ulog_global_filter_kw_set(keyword);

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+ULOGKW", "=<keyword>", RT_NULL, at_ulog_global_kw_query, at_ulog_global_kw_setup, RT_NULL, 0);
