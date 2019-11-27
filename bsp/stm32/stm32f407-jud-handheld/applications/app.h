/****************************************************************************
 *
 * File Name
 *  app.h
 * Author
 *  wangk
 * Date
 *  2019/08/04
 * Descriptions:
 * app接口定义头文件
 *
 ****************************************************************************/

#ifndef __APP_H__
#define __APP_H__

#include <stdint.h>
#include <stdbool.h>

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
* Function: vcom_send_data
* Description: 通过VCOM输出数据
* Author: wangk
* Returns: 返回实际输出的字节数
* Parameter:
* History:
*************************************************/
uint32_t vcom_send_data(const uint8_t* data, uint32_t len);

/*************************************************
* Function: adc_calibration
* Description: 执行ADC通道校准
* Author: wangk
* Returns: 
* Parameter:
* History:
*************************************************/
bool adc_calibration(int adc_channel);

/*************************************************
* Function: adc_start
* Description: 启动ADC采集
* Author: wangk
* Returns: 
* Parameter:
* History:
*************************************************/
bool adc_start(int adc_channel);

/*************************************************
* Function: adc_stop
* Description: 停止ADC采集
* Author: wangk
* Returns: 
* Parameter:
* History:
*************************************************/
bool adc_stop(void);

/*************************************************
* Function: bme280_get_temp
* Description: 通过bme280读取温度
* Author: wangk
* Returns: 温度单位,摄氏度
* Parameter:
* History:
*************************************************/
float bme280_get_temp(void);

/*************************************************
* Function: bme280_get_humi
* Description: 通过bme280读取相对湿度
* Author: wangk
* Returns: 相对湿度单位,百分比
* Parameter:
* History:
*************************************************/
float bme280_get_humi(void);

/*************************************************
* Function: bme280_get_baro
* Description: 通过bme280读取大气压
* Author: wangk
* Returns: 大气压单位,帕
* Parameter:
* History:
*************************************************/
float bme280_get_baro(void);

/*************************************************
* Function: sht20_get_temp
* Description: 通过sht20读取温度
* Author: wangk
* Returns: 温度单位,摄氏度
* Parameter:
* History:
*************************************************/
float sht20_get_temp(void);

/*************************************************
* Function: sht20_get_humi
* Description: 通过sht20读取相对湿度
* Author: wangk
* Returns: 相对湿度单位,百分比
* Parameter:
* History:
*************************************************/
float sht20_get_humi(void);

/**--------------------------------------------------------------------------*
**                         Compiler Flag                                     *
**---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif

#endif // __APP_H__
