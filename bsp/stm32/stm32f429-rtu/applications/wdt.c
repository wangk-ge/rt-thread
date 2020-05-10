/****************************************************************************
 *
 * File Name
 *  wdt.c
 * Author
 *  
 * Date
 *  2020/05/10
 * Descriptions:
 * 看门狗功能实现
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
#include <rtdevice.h>

#include "util.h"
#include "strref.h"
#include "common.h"

/**---------------------------------------------------------------------------*
 **                            Debugging Flag                                 *
 **---------------------------------------------------------------------------*/
#define LOG_TAG              "wdt"
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
/* 看门狗设备名称 */
#define WDT_DEVICE_NAME "wdt"

/*----------------------------------------------------------------------------*
**                             Data Structures                                *
**----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*
**                             Local Vars                                     *
**----------------------------------------------------------------------------*/
/* 看门狗设备句柄 */
static rt_device_t wdt_dev = RT_NULL;

/*----------------------------------------------------------------------------*
**                             Local Function                                 *
**----------------------------------------------------------------------------*/
/* 系统空闲时被调用 */
static void wdt_idle_hook(void)
{
    /* 在空闲线程的回调函数里喂狗 */
    rt_device_control(wdt_dev, RT_DEVICE_CTRL_WDT_KEEPALIVE, NULL);
    
    //LOG_D("%s()", __FUNCTION__);
}

/*----------------------------------------------------------------------------*
**                             Public Function                                *
**----------------------------------------------------------------------------*/
/*************************************************
* Function: wdt_start
* Description: 启动开门狗
* Author: 
* Returns:
* Parameter: seconds  看门狗超时时间(秒)
* History:
*************************************************/
rt_err_t wdt_start(rt_uint32_t seconds)
{
    LOG_D("%s() seconds=%u", __FUNCTION__, seconds);
    
    /* 根据设备名称查找看门狗设备，获取设备句柄 */
    wdt_dev = rt_device_find(WDT_DEVICE_NAME);
    if (!wdt_dev)
    {
        LOG_E("%s() find %s failed!\n", __FUNCTION__, WDT_DEVICE_NAME);
        return -RT_ERROR;
    }
    
    /* 初始化设备 */
    rt_err_t ret = rt_device_init(wdt_dev);
    if (ret != RT_EOK)
    {
        LOG_E("%s() initialize %s failed!\n", __FUNCTION__, WDT_DEVICE_NAME);
        return ret;
    }
    
    /* 设置看门狗溢出时间 */
    ret = rt_device_control(wdt_dev, RT_DEVICE_CTRL_WDT_SET_TIMEOUT, &seconds);
    if (ret != RT_EOK)
    {
        LOG_E("%s() set %s timeout failed!\n", __FUNCTION__, WDT_DEVICE_NAME);
        return ret;
    }
    
    /* 启动看门狗 */
    ret = rt_device_control(wdt_dev, RT_DEVICE_CTRL_WDT_START, RT_NULL);
    if (ret != RT_EOK)
    {
        LOG_E("%s() start %s failed!\n", __FUNCTION__, WDT_DEVICE_NAME);
        return ret;
    }
    
    /* 注册空闲线程回调函数 */
    rt_thread_idle_delhook(wdt_idle_hook); // 避免重复注册
    ret = rt_thread_idle_sethook(wdt_idle_hook);
    if (ret != RT_EOK)
    {
        LOG_E("%s() rt_thread_idle_sethook(wdt_idle_hook) failed!\n", __FUNCTION__);
    }
    
    return ret;
}

/**---------------------------------------------------------------------------*
 **                         Compiler Flag                                     *
 **---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif
// End of wdt.c
