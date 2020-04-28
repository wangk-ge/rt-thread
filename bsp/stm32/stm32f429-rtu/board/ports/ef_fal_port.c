/*
 * This file is part of the EasyFlash Library.
 *
 * Copyright (c) 2015, Armink, <armink.ztl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Function: Portable interface for FAL (Flash Abstraction Layer) partition.
 * Created on: 2018-05-19
 */

#include <easyflash.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <rthw.h>
#include <rtthread.h>
#include <fal.h>

#define LOG_TAG              "ef"
#define LOG_LVL              LOG_LVL_DBG
#include <rtdbg.h>

#define STR_LEN(str) (sizeof(str) - 1)
#define STR_ITEM(s) (s), STR_LEN(s)

/* EasyFlash partition name on FAL partition table */
#define FAL_EF_PART_NAME               "easyflash"

/* default ENV set for user */
static const ef_env default_env_set[] = {
        {"ulog_glb_lvl", "\0", 1}, // ULOG全局日志level,默认值0
        {"client_id", "\0\0\0\0", 4}, // 默认值0000000000
        {"a_ip", STR_ITEM("47.103.22.229")}, // 默认值47.103.22.229
        {"a_port", "\x5B\x07", 2}, // 默认值1883
        {"b_ip", STR_ITEM("47.103.22.229")}, // 默认值47.103.22.229
        {"b_port", "\x5B\x07", 2}, // 默认值1883
        {"acquisition", "\x05", 1}, // 默认值5
        {"cycle", "\x1E", 1}, // 默认值30
        /* uartXvariable */
        {"uart1variable", STR_ITEM("temp1")},
        {"uart2variable", STR_ITEM("temp2")},
        {"uart3variable", STR_ITEM("temp3")},
        {"uart4variable", STR_ITEM("temp4")},
        /* uartXvariablecnt */
        {"uart1variablecnt", "\x01", 1}, // 默认值1
        {"uart2variablecnt", "\x01", 1}, // 默认值1
        {"uart3variablecnt", "\x01", 1}, // 默认值1
        {"uart4variablecnt", "\x01", 1}, // 默认值1
        /* uartXbaudrate */
        {"uart1baudrate", "\x80\x25\x00\x00", 4}, // 默认值9600
        {"uart2baudrate", "\x80\x25\x00\x00", 4}, // 默认值9600
        {"uart3baudrate", "\x80\x25\x00\x00", 4}, // 默认值9600
        {"uart4baudrate", "\x80\x25\x00\x00", 4}, // 默认值9600
        /* uartXwordlength */
        {"uart1wordlength", "\x08", 1}, // 默认值8
        {"uart2wordlength", "\x08", 1}, // 默认值8
        {"uart3wordlength", "\x08", 1}, // 默认值8
        {"uart4wordlength", "\x08", 1}, // 默认值8
        /* uartXparity */
        {"uart1parity", "\x00", 1}, // 默认值0(0:无校验,1:奇校验,2:偶校验)
        {"uart2parity", "\x00", 1}, // 默认值0(0:无校验,1:奇校验,2:偶校验)
        {"uart3parity", "\x00", 1}, // 默认值0(0:无校验,1:奇校验,2:偶校验)
        {"uart4parity", "\x00", 1}, // 默认值0(0:无校验,1:奇校验,2:偶校验)
        /* uartXstopbits */
        {"uart1stopbits", "\x02", 1}, // 默认值2(高半字节整数部分,低半字节小数部分)
        {"uart2stopbits", "\x02", 1}, // 默认值2(高半字节整数部分,低半字节小数部分)
        {"uart3stopbits", "\x02", 1}, // 默认值2(高半字节整数部分,低半字节小数部分)
        {"uart4stopbits", "\x02", 1}, // 默认值2(高半字节整数部分,低半字节小数部分)
        /* uartXslaveraddr */
        {"uart1slaveraddr", "\x01", 1}, // 默认值0x01
        {"uart2slaveraddr", "\x01", 1}, // 默认值0x01
        {"uart3slaveraddr", "\x01", 1}, // 默认值0x01
        {"uart4slaveraddr", "\x01", 1}, // 默认值0x01
        /* uartXfunction */
        {"uart1function", "\x03", 1}, // 默认值0x03
        {"uart2function", "\x03", 1}, // 默认值0x03
        {"uart3function", "\x03", 1}, // 默认值0x03
        {"uart4function", "\x03", 1}, // 默认值0x03
        /* uartXstartaddr */
        {"uart1startaddr", "\x00\x26", 2}, // 默认值0x2600
        {"uart2startaddr", "\x00\x26", 2}, // 默认值0x2600
        {"uart3startaddr", "\x00\x26", 2}, // 默认值0x2600
        {"uart4startaddr", "\x00\x26", 2}, // 默认值0x2600
        /* uartXlength */
        {"uart1length", "\x07\x00", 2}, // 默认值0x0007
        {"uart2length", "\x07\x00", 2}, // 默认值0x0007
        {"uart3length", "\x07\x00", 2}, // 默认值0x0007
        {"uart4length", "\x07\x00", 2}, // 默认值0x0007
        /* uartXtype
         *  0x00=有符号16位int
         *  0x01=无符号16位int
         *  0x02=有符号32位int(ABCD)
         *  0x03=有符号32位int(CDAB)
         *  0x04=无符号32位int(ABCD)
         *  0x05=无符号32位int(CDAB)
         *  0x06=IEEE754浮点数(ABCD)
         *  0x07=IEEE754浮点数(CDAB)
         */
        {"uart1type", "\x06", 1}, // 默认值0x06
        {"uart2type", "\x06", 1}, // 默认值0x06
        {"uart3type", "\x06", 1}, // 默认值0x06
        {"uart4type", "\x06", 1}, // 默认值0x06
};

static char log_buf[RT_CONSOLEBUF_SIZE];
static struct rt_semaphore env_cache_lock;
static const struct fal_partition *part = NULL;

/**
 * Flash port for hardware initialize.
 *
 * @param default_env default ENV set for user
 * @param default_env_size default ENV size
 *
 * @return result
 */
EfErrCode ef_port_init(ef_env const **default_env, size_t *default_env_size) {
    EfErrCode result = EF_NO_ERR;

    *default_env = default_env_set;
    *default_env_size = sizeof(default_env_set) / sizeof(default_env_set[0]);

    rt_sem_init(&env_cache_lock, "env lock", 1, RT_IPC_FLAG_PRIO);

    part = fal_partition_find(FAL_EF_PART_NAME);
    EF_ASSERT(part);

    return result;
}

/**
 * Read data from flash.
 * @note This operation's units is word.
 *
 * @param addr flash address
 * @param buf buffer to store read data
 * @param size read bytes size
 *
 * @return result
 */
EfErrCode ef_port_read(uint32_t addr, uint32_t *buf, size_t size) {
    EfErrCode result = EF_NO_ERR;

    fal_partition_read(part, addr, (uint8_t *)buf, size);

    return result;
}

/**
 * Erase data on flash.
 * @note This operation is irreversible.
 * @note This operation's units is different which on many chips.
 *
 * @param addr flash address
 * @param size erase bytes size
 *
 * @return result
 */
EfErrCode ef_port_erase(uint32_t addr, size_t size) {
    EfErrCode result = EF_NO_ERR;

    /* make sure the start address is a multiple of FLASH_ERASE_MIN_SIZE */
    EF_ASSERT(addr % EF_ERASE_MIN_SIZE == 0);

    if (fal_partition_erase(part, addr, size) < 0)
    {
        result = EF_ERASE_ERR;
    }

    return result;
}
/**
 * Write data to flash.
 * @note This operation's units is word.
 * @note This operation must after erase. @see flash_erase.
 *
 * @param addr flash address
 * @param buf the write data buffer
 * @param size write bytes size
 *
 * @return result
 */
EfErrCode ef_port_write(uint32_t addr, const uint32_t *buf, size_t size) {
    EfErrCode result = EF_NO_ERR;

    if (fal_partition_write(part, addr, (uint8_t *)buf, size) < 0)
    {
        result = EF_WRITE_ERR;
    }

    return result;
}

/**
 * lock the ENV ram cache
 */
void ef_port_env_lock(void) {
    rt_sem_take(&env_cache_lock, RT_WAITING_FOREVER);
}

/**
 * unlock the ENV ram cache
 */
void ef_port_env_unlock(void) {
    rt_sem_release(&env_cache_lock);
}

/**
 * This function is print flash debug info.
 *
 * @param file the file which has call this function
 * @param line the line number which has call this function
 * @param format output format
 * @param ... args
 *
 */
void ef_log_debug(const char *file, const long line, const char *format, ...) {

#ifdef PRINT_DEBUG

    va_list args;

    /* args point to the first variable parameter */
    va_start(args, format);
#if 0
    ef_print("[Flash] (%s:%ld) ", file, line);
    /* must use vprintf to print */
    rt_vsprintf(log_buf, format, args);
    ef_print("%s", log_buf);
#else
    LOG_D("[Flash] (%s:%ld) ", file, line);
    rt_vsprintf(log_buf, format, args);
    LOG_D("%s", log_buf);
#endif
    va_end(args);

#endif

}

/**
 * This function is print flash routine info.
 *
 * @param format output format
 * @param ... args
 */
void ef_log_info(const char *format, ...) {
    va_list args;

    /* args point to the first variable parameter */
    va_start(args, format);
#if 0
    ef_print("[Flash] ");
    /* must use vprintf to print */
    rt_vsprintf(log_buf, format, args);
    ef_print("%s", log_buf);
#else
    LOG_I("[Flash] ");
    rt_vsprintf(log_buf, format, args);
    LOG_I("%s", log_buf);
#endif
    va_end(args);
}
/**
 * This function is print flash non-package info.
 *
 * @param format output format
 * @param ... args
 */
void ef_print(const char *format, ...) {
    va_list args;

    /* args point to the first variable parameter */
    va_start(args, format);
    /* must use vprintf to print */
    rt_vsprintf(log_buf, format, args);
    rt_kprintf("%s", log_buf);
    va_end(args);
}

/**
 * get default ENV
 */
void ef_get_default_env(ef_env const **default_env, size_t *default_env_size) {
    *default_env = default_env_set;
    *default_env_size = sizeof(default_env_set) / sizeof(default_env_set[0]);
}
