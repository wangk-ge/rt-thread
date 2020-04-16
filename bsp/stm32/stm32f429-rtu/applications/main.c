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

/* defined the LED1 pin: PD14 */
#define LED0_PIN    GET_PIN(D, 14)

#define AT_NB_UART "uart3"

int main(void)
{
    int count = 1;
    /* set LED0 pin mode to output */
    rt_pin_mode(LED0_PIN, PIN_MODE_OUTPUT);
    
        rt_device_t at_nb_uart_dev = rt_device_find(AT_NB_UART);
    if (at_nb_uart_dev != RT_NULL)
        {
            struct serial_configure cfg = {
                    BAUD_RATE_9600,   /* 9600 bits/s */
                DATA_BITS_8,      /* 8 databits */
            STOP_BITS_1,      /* 1 stopbit */
            PARITY_NONE,      /* No parity  */
            BIT_ORDER_LSB,    /* LSB first sent */
            NRZ_NORMAL,       /* Normal mode */
            RT_SERIAL_RB_BUFSZ, /* Buffer size */
            0
                };
            rt_err_t eret = rt_device_control(at_nb_uart_dev, RT_DEVICE_CTRL_CONFIG, &cfg);
                if (eret != RT_EOK)
            {
                    rt_kprintf("rt_device_control(%s) error(%d)!", AT_NB_UART, eret);
            }
                
            int ret = at_client_init(AT_NB_UART, 512);
            if (ret != RT_EOK)
            {
                    rt_kprintf("at_client_init(%s) error(%d)!", AT_NB_UART, ret);
            }
        }
      else
        {
            rt_kprintf("rt_device_find(%s) failed!", AT_NB_UART);
        }
    
    
    while (count++)
    {
        rt_pin_write(LED0_PIN, PIN_HIGH);
        rt_thread_mdelay(500);
        rt_pin_write(LED0_PIN, PIN_LOW);
        rt_thread_mdelay(500);
    }

    return RT_EOK;
}
