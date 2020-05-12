/****************************************************************************
 *
 * File Name
 *  app.h
 * Author
 *  
 * Date
 *  2020/04/28
 * Descriptions:
 * APP相关接口定义
 *
 ****************************************************************************/

#ifndef __APP_H__
#define __APP_H__

#include <stdint.h>
#include <stdbool.h>
#include <rtthread.h>
#include <at.h>

/**---------------------------------------------------------------------------*
**                             Compiler Flag                                  *
**----------------------------------------------------------------------------*/
#ifdef __cplusplus
extern   "C"
{
#endif

/*----------------------------------------------------------------------------*
**                             Mcaro Definitions                              *
**----------------------------------------------------------------------------*/
/* APP内存池大小 */
#define APP_MP_BLOCK_NUM (8) // APP内存池预留块数
#define APP_MP_BLOCK_SIZE (1024) // APP内存池每块大小(字节)

/*----------------------------------------------------------------------------*
**                             Data Structures                                *
**----------------------------------------------------------------------------*/
    
/*----------------------------------------------------------------------------*
**                             Function Define                                *
**----------------------------------------------------------------------------*/
/*************************************************
* 从APP内存池分配一块内存 */
void *app_mp_alloc(void);

/* 释放APP内存池分配的内存块 */
void app_mp_free(void *buf);

/* 从内存池分配at_response_t对象 */
at_response_t app_alloc_at_resp(rt_size_t line_num, rt_int32_t timeout);

/* 释放at_response_t对象 */
void app_free_at_resp(at_response_t resp);

/* 清空历史数据 */
rt_err_t clear_history_data(void);

/* 读取前n个时刻的一条历史数据(JSON格式) */
uint32_t read_history_data_json(uint32_t n, char* json_data_buf, uint32_t json_buf_len, bool need_timestamp);

/* 取得模组信号强度指示 */
int get_modem_rssi(int *rssi);

/* 取得时区 */
int get_timezone(int *zz);

/* 取得时间戳(UNIX时间戳格式) */
uint32_t get_timestamp(void);

/* 取得客户端唯一编号 */
uint32_t get_clientid(void);

/* 取得产品编号 */
const char* get_productkey(void);

/* 取得设备ID */
const char* get_devicecode(void);

/* 取得标签ID */
const char* get_itemid(void);

/* 取得历史数据条目数 */
uint32_t get_history_data_num(void);

/* 请求采集数据 */
rt_err_t req_data_acquisition(void);

/* 请求上报数据 */
rt_err_t req_data_report(void);

/* 请求重启系统 */
rt_err_t req_restart(void);

/**--------------------------------------------------------------------------*
**                         Compiler Flag                                     *
**---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif

#endif // __APP_H__
