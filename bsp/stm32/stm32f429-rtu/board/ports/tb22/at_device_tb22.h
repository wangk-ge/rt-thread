 /*
 * File      : at_device_tb22.h
 * This file is part of RT-Thread RTOS
 * Copyright (c) 2020, RudyLo <luhuadong@163.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Change Logs:
 * Date           Author            Notes
 * 2020-02-12     luhuadong         first version
 */

#ifndef __AT_DEVICE_TB22_H__
#define __AT_DEVICE_TB22_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

#include <at_device.h>

/* The maximum number of sockets supported by the TB22 device */
#define AT_DEVICE_TB22_SOCKETS_NUM  7

struct at_device_tb22
{
    char *device_name;
    char *client_name;

    int power_pin; // 电源控制GPIO
    int reset_pin; // 复位控制GPIO
    size_t recv_bufsz;
    struct at_device device;

    void *socket_data;
    void *user_data;

    rt_bool_t power_status;
    rt_bool_t sleep_status;
    
    /* at_response_t对象内存池 */
    rt_mp_t at_resp_mp;
    /* socket接收缓存内存池 */
    rt_mp_t sock_recv_mp;
    /* socket接收线程 */
    rt_thread_t sock_recv_thread;
};

#ifdef AT_USING_SOCKET

/* tb22 device socket initialize */
int tb22_socket_init(struct at_device *device);

/* tb22 device class socket register */
int tb22_socket_class_register(struct at_device_class *class);

/* 从内存池分配at_response_t对象 */
at_response_t tb22_alloc_at_resp(struct at_device *device, rt_size_t line_num, rt_int32_t timeout);

/* 释放at_response_t对象 */
void tb22_free_at_resp(at_response_t resp);

#endif /* AT_USING_SOCKET */

#ifdef __cplusplus
}
#endif

#endif /* __AT_DEVICE_TB22_H__ */
