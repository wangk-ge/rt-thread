/*
 * File      : at_tb22.c
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
 * 2020-02-13     luhuadong         first version
 */

#include <at_device_tb22.h>
#include <rtconfig.h>

#define LOG_TAG    "at.tb22"
#include <at_log.h>

static struct at_device_tb22 _dev =
{
    .device_name = TB22_DEVICE_NAME,
    .client_name = TB22_CLIENT_NAME,

    .power_pin = TB22_POWER_PIN,
    .reset_pin = TB22_RESET_PIN,
    .recv_bufsz = TB22_RECV_BUFF_LEN,
};

static int tb22_device_register(void)
{
    /* set baud rate to 9600 */
    struct serial_configure uart_cfg = {
        BAUD_RATE_9600,     /* 9600 bits/s */
        DATA_BITS_8,        /* 8 databits */
        STOP_BITS_1,        /* 1 stopbit */
        PARITY_NONE,        /* No parity  */
        BIT_ORDER_LSB,      /* LSB first sent */
        NRZ_NORMAL,         /* Normal mode */
        RT_SERIAL_RB_BUFSZ, /* Buffer size */
        0
    };
    rt_device_t at_client_dev = rt_device_find(TB22_CLIENT_NAME);
    if (at_client_dev == RT_NULL)
    {
        rt_kprintf("rt_device_find(%s) failed!", TB22_CLIENT_NAME);
        return -RT_ERROR;
    }
    
    rt_err_t ret = rt_device_control(at_client_dev, RT_DEVICE_CTRL_CONFIG, &uart_cfg);
    if (ret != RT_EOK)
    {
        rt_kprintf("rt_device_control(%s) error(%d)!", TB22_CLIENT_NAME, ret);
        return ret;
    }
    
    struct at_device_tb22 *tb22 = &_dev;

    return at_device_register(&(tb22->device),
                              tb22->device_name,
                              tb22->client_name,
                              AT_DEVICE_CLASS_TB22,
                              (void *) tb22);
}
INIT_APP_EXPORT(tb22_device_register);

