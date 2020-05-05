/****************************************************************************
 *
 * File Name
 *  history_data.c
 * Author
 *  
 * Date
 *  2020/05/05
 * Descriptions:
 * 历史数据存取相关接口实现
 *
 ******************************************************************************/
/*----------------------------------------------------------------------------*
**                             Dependencies                                   *
**----------------------------------------------------------------------------*/
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <rtthread.h>
#include <fal.h>
#include <easyflash.h>

#include "util.h"
#include "strref.h"
#include "common.h"
#include "history_data.h"

/**---------------------------------------------------------------------------*
 **                            Debugging Flag                                 *
 **---------------------------------------------------------------------------*/
#define LOG_TAG              "history_data"
#define LOG_LVL              LOG_LVL_DBG
#include <rtdbg.h>

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
#define HISTORY_DATA_ITEM_MAX_SIZE (512) // 历史数据条目最大长度(字节数)[要求能整除Flash块大小]
#define HISTORY_DATA_BLOCK_SIZE (4096) // Flash块大小(按块擦除)
    
/*----------------------------------------------------------------------------*
**                             Data Structures                                *
**----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*
**                             Local Vars                                     *
**----------------------------------------------------------------------------*/
/* 历史数据FAL分区 */
static const struct fal_partition *history_data_partition = NULL;
/* 用于保护历史数据的并发访问 */
static rt_mutex_t history_data_mutex = RT_NULL;
/* 最大能够保存的历史数据条目数 */
static uint32_t history_data_max_num = 0;

/*----------------------------------------------------------------------------*
**                             Local Function                                 *
**----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*
**                             Public Function                                *
**----------------------------------------------------------------------------*/
/*************************************************
* Function: history_data_init
* Description: 模块初始化
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
rt_err_t history_data_init(const char* partition)
{
    rt_err_t ret = RT_EOK;

    LOG_D("%s() partition=%s", __FUNCTION__, partition);
    
    /* 确保历史数据条目最大长度能整除Flash块大小 */
    RT_ASSERT((HISTORY_DATA_BLOCK_SIZE % HISTORY_DATA_ITEM_MAX_SIZE) == 0);
    
    /* 找到历史数据FAL分区 */
    history_data_partition = fal_partition_find(partition);
    RT_ASSERT(history_data_partition);
    
    /* 计算能够保存的最大历史数据条目数 */
    history_data_max_num = history_data_partition->len / HISTORY_DATA_ITEM_MAX_SIZE;
    LOG_D("%s() history_data_max_num=%u", __FUNCTION__, history_data_max_num);
    
    /* create history fifo mutex */
    history_data_mutex = rt_mutex_create("history_data", RT_IPC_FLAG_FIFO);
    if (RT_NULL == history_data_mutex)
    {
        LOG_E("%s rt_mutex_create(history_data) failed!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
__exit:
    
    if (ret != RT_EOK)
    {
        if (RT_NULL != history_data_mutex)
        {
            rt_mutex_delete(history_data_mutex);
            history_data_mutex = RT_NULL;
        }
        
        history_data_partition = RT_NULL;
    }
    
    return ret;
}

/*************************************************
* Function: history_data_get_max_num
* Description: 取得最大可存储的历史数据条目数
* Author:
* Returns: 
* Parameter:
* History:
*************************************************/
uint32_t history_data_get_max_num(void)
{
    //LOG_D("%s()", __FUNCTION__);
    
    return history_data_max_num;
}

/*************************************************
* Function: history_data_get_fifo_info
* Description: 读取历史数据FIFO队列信息
* Author:
* Returns: 
* Parameter:
* History:
*************************************************/
rt_err_t history_data_get_fifo_info(history_data_fifo_info *fifo_info)
{
    //LOG_D("%s()", __FUNCTION__);
    
    /* 加载FIFO队列信息 */
    size_t len = ef_get_env_blob("history_fifo_info", fifo_info, sizeof(history_data_fifo_info), NULL);
    if (len != sizeof(history_data_fifo_info))
    {
        /* 加载FIFO队列失败(第一次运行?) */
        LOG_E("%s ef_get_env_blob(history_fifo_info) failed!", __FUNCTION__);
        return -RT_ERROR;
    }
    
    return RT_EOK;
}

/*************************************************
* Function: history_data_save
* Description: 保存历史数据(按队列顺序存储)
* Author:
* Returns: 
* Parameter:
* History:
*************************************************/
rt_err_t history_data_save(const uint8_t* data, size_t data_len)
{
    rt_err_t ret = RT_EOK;
    int fal_ret = 0;
    EfErrCode ef_ret = EF_NO_ERR;
    
    LOG_D("%s() data_len=%u", __FUNCTION__, data_len);
    
    /* 确保互斥修改FIFO队列 */
    rt_mutex_take(history_data_mutex, RT_WAITING_FOREVER);
    
    /* 读取历史数据队列信息 */
    history_data_fifo_info fifo_info = {0};
    ret = history_data_get_fifo_info(&fifo_info);
    if (ret != RT_EOK)
    {
        /* 加载FIFO队列失败,队列为空 */
        LOG_W("%s history_data_get_fifo_info() failed, history fifo is empty.", __FUNCTION__);
        
        /* 设置队列为空 */
        fifo_info.length = 0; // 队列长度
        fifo_info.head_pos = 0; // 头部位置
        fifo_info.tail_pos = 0; // 尾部位置
        
        ret = RT_EOK;
    }
    
    /* 本次采集将保存在FIFO中的位置 */
    uint32_t pos = fifo_info.head_pos;
    
    /* 计算保存的Flash地址 */
    uint32_t addr = pos * HISTORY_DATA_ITEM_MAX_SIZE;
    /* 如果地址位于块起始,则需要执行块擦除 */
    if ((addr % HISTORY_DATA_BLOCK_SIZE) == 0)
    {
        /* 擦除新块 */
        fal_ret = fal_partition_erase(history_data_partition, addr, HISTORY_DATA_BLOCK_SIZE);
        if (fal_ret != HISTORY_DATA_BLOCK_SIZE)
        {
            LOG_E("%s fal_partition_erase(%u,%u) error!", __FUNCTION__, addr, HISTORY_DATA_BLOCK_SIZE);
            ret = -RT_ERROR;
            goto __exit;
        }
    }
    /* 写入数据 */
    fal_ret = fal_partition_write(history_data_partition, addr, data, data_len);
    if (fal_ret != data_len)
    {
        LOG_E("%s fal_partition_write(%u,%u) error!", __FUNCTION__, addr, data_len);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    /* 更新FIFO信息 */
    fifo_info.head_pos++; // 指向下一个空位置
    if (fifo_info.head_pos >= history_data_max_num)
    { // 超出边界
        fifo_info.head_pos = 0; // 回到起点
    }
    fifo_info.length++;
    if (fifo_info.length > history_data_max_num)
    { // FIFO已满
        /* 尾部块已被整体擦除 */
        uint32_t erase_num = (HISTORY_DATA_BLOCK_SIZE / HISTORY_DATA_ITEM_MAX_SIZE); // 被擦除的条目数
        fifo_info.tail_pos += erase_num; // 跳过已擦除块内的条目
        if (fifo_info.tail_pos >= history_data_max_num)
        { // 超出边界
            fifo_info.tail_pos = 0; // 回到起点
        }
        fifo_info.length -= erase_num;
    }
    /* 保存新的FIFO信息 */
    ef_ret = ef_set_env_blob("history_fifo_info", &fifo_info, sizeof(fifo_info));
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_set_env_blob(history_fifo_info) error!", __FUNCTION__);
        ret = -RT_ERROR;
        goto __exit;
    }
    
    ret = RT_EOK;
    
__exit:
    
    /* 释放互斥锁 */
    rt_mutex_release(history_data_mutex);
    
    return ret;
}

/*************************************************
* Function: history_data_load_pos
* Description: 加载历史数据(指定位置)
* Author:
* Returns: 返回实际加载的字节数
* Parameter:
* History:
*************************************************/
uint32_t history_data_load_pos(uint32_t pos, uint8_t* data_buf, size_t read_len)
{
    LOG_D("%s() pos=%u, read_len=%u", __FUNCTION__, pos, read_len);
    
    history_data_fifo_info fifo_info = {0};
    rt_err_t ret = history_data_get_fifo_info(&fifo_info);
    if (ret != RT_EOK)
    {
        /* 加载FIFO队列失败,队列为空 */
        LOG_W("%s history_data_get_fifo_info() failed, history data is empty!", __FUNCTION__);
        return 0;
    }
    
    /* 状态检查 */
    if (fifo_info.length <= 0)
    { // 队列为空
        LOG_W("%s history data is empty!", __FUNCTION__);
        return 0;
    }
    
    /* 范围检查 */
    if (pos >= history_data_max_num)
    {
        /* 超范围 */
        LOG_E("%s pos(%u) not in range[0,%u]!", __FUNCTION__, pos, history_data_max_num);
        return 0;
    }
    
    /* 读取pos处的一条历史数据 */
    uint32_t addr = pos * HISTORY_DATA_ITEM_MAX_SIZE;
    int fal_ret = fal_partition_read(history_data_partition, addr, data_buf, read_len);
    if (fal_ret < 0)
    {
        LOG_E("%s fal_partition_read failed(%d)!", __FUNCTION__, fal_ret);
        return 0;
    }
    
    return (uint32_t)fal_ret;
}

/*************************************************
* Function: history_data_clear
* Description: 清空历史数据
* Author:
* Returns: 
* Parameter:
* History:
*************************************************/
rt_err_t history_data_clear(void)
{
    rt_err_t ret = RT_EOK;
    
    LOG_D("%s()", __FUNCTION__);
    
    /* 删除历史数据队列信息 */
    EfErrCode ef_ret = ef_del_env("history_fifo_info");
    if (ef_ret != EF_NO_ERR)
    {
        LOG_E("%s ef_del_env(history_fifo_info) error(%d)!", __FUNCTION__, ef_ret);
        ret = -RT_ERROR;
    }
    
    return ret;
}

/**---------------------------------------------------------------------------*
 **                         Compiler Flag                                     *
 **---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif
// End of history_data.c
