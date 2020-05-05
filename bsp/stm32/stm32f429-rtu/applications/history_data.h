/****************************************************************************
 *
 * File Name
 *  history_data.h
 * Author
 *  
 * Date
 *  2020/05/05
 * Descriptions:
 * 历史数据存取相关接口定义
 *
 ****************************************************************************/

#ifndef __HISTORY_DATA_H__
#define __HISTORY_DATA_H__

#include <stdint.h>
#include <stdbool.h>
#include <rtthread.h>

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

/*----------------------------------------------------------------------------*
**                             Data Structures                                *
**----------------------------------------------------------------------------*/
/* 历史数据FIFO队列信息(头部插入、尾部删除) */
typedef struct
{
    uint32_t length; // 队列长度
    uint32_t head_pos; // 头部位置
    uint32_t tail_pos; // 尾部位置
} history_data_fifo_info;

/*----------------------------------------------------------------------------*
**                             Function Define                                *
**----------------------------------------------------------------------------*/
/*************************************************
* Function: history_data_init
* Description: 模块初始化
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
rt_err_t history_data_init(const char* partition);

/*************************************************
* Function: history_data_get_max_num
* Description: 取得最大可存储的历史数据条目数
* Author:
* Returns: 
* Parameter:
* History:
*************************************************/
uint32_t history_data_get_max_num(void);

/*************************************************
* Function: history_data_get_fifo_info
* Description: 读取历史数据FIFO队列信息
* Author:
* Returns: 
* Parameter:
* History:
*************************************************/
rt_err_t history_data_get_fifo_info(history_data_fifo_info *fifo_info);

/*************************************************
* Function: history_data_save
* Description: 保存历史数据(按队列顺序存储)
* Author:
* Returns: 
* Parameter:
* History:
*************************************************/
rt_err_t history_data_save(const uint8_t* data, size_t data_len);

/*************************************************
* Function: history_data_load_pos
* Description: 加载历史数据(指定位置)
* Author:
* Returns: 返回实际加载的字节数
* Parameter:
* History:
*************************************************/
uint32_t history_data_load_pos(uint32_t pos, uint8_t* data_buf, size_t buf_len);

/*************************************************
* Function: history_data_clear
* Description: 清空历史数据
* Author:
* Returns: 
* Parameter:
* History:
*************************************************/
rt_err_t history_data_clear(void);

/**--------------------------------------------------------------------------*
**                         Compiler Flag                                     *
**---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif

#endif // __HISTORY_DATA_H__
