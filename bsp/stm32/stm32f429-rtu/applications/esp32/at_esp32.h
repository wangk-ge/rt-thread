/****************************************************************************
 *
 * File Name
 *  at_esp32.h
 * Author
 *  
 * Date
 *  2020/05/01
 * Descriptions:
 * ESP32模块AT通信接口定义
 *
 ****************************************************************************/

#ifndef __AT_ESP32_H__
#define __AT_ESP32_H__

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
    
/*----------------------------------------------------------------------------*
**                             Function Define                                *
**----------------------------------------------------------------------------*/
/*************************************************
* Function: at_esp32_init
* Description: ESP32模块初始化
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
rt_err_t at_esp32_init(void);

/**--------------------------------------------------------------------------*
**                         Compiler Flag                                     *
**---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif

#endif // __AT_ESP32_H__
