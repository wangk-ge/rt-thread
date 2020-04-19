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

/* RTC相关AT指令 */

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
