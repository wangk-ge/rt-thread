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

#define LOG_TAG                        "at.tb22"
#include <at_log.h>

#define TB22_DEIVCE_NAME        "tb22"

static struct at_device_tb22 _dev =
{
    TB22_DEIVCE_NAME,
    TB22_CLIENT_NAME,

    TB22_POWER_PIN,
    TB22_STATUS_PIN,
    TB22_RECV_BUFF_LEN,
};

static int tb22_device_register(void)
{
    struct at_device_tb22 *tb22 = &_dev;

    return at_device_register(&(tb22->device),
                              tb22->device_name,
                              tb22->client_name,
                              AT_DEVICE_CLASS_TB22,
                              (void *) tb22);
}
INIT_APP_EXPORT(tb22_device_register);

