/****************************************************************************
 *
 * File Name
 *  config.h
 * Author
 *  
 * Date
 *  2020/04/20
 * Descriptions:
 * 参数配置接口定义
 *
 ****************************************************************************/

#ifndef __CONFIG_H__
#define __CONFIG_H__

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
/* UARTX配置信息 */
typedef struct
{
    char** variable; /* uartXvariable: 变量名 */
    uint32_t baudrate; /* uartXbaudrate: 波特率 */
    uint16_t* startaddr; /* uartXstartaddr: 开始地址 */
    uint16_t* length; /* uartXlength: 读取寄存器数量 */
    uint8_t variablecnt; /* uartXvariablecnt: 变量个数 */
    uint8_t wordlength; /* uartXwordlength: 数据位 */
    uint8_t parity; /* uartXparity: 校验位 */
    uint8_t stopbits; /* uartXstopbits: 停止位 */
    uint8_t slaveraddr; /* uartXslaveraddr: 从机地址 */
    uint8_t function; /* uartXfunction: 功能码 */
} uart_x_config_info;

/* 全部配置信息 */
typedef struct
{
    uart_x_config_info uart_x_cfg[4]; /* UARTX配置信息 */
    uint32_t client_id; // 客户端编号
    uint32_t a_ip; // 通道A IP
    uint32_t b_ip; // 通道B IP
    uint16_t a_port; // 通道A 端口
    uint16_t b_port; // 通道B 端口
    uint8_t ulog_glb_lvl; // LOG输出等级
    uint8_t acquisition; // 数据采集间隔时间(分钟)
    uint8_t cycle; // 数据发布间隔时间(分钟)
} config_info;

/*----------------------------------------------------------------------------*
**                             Function Define                                *
**----------------------------------------------------------------------------*/
/*************************************************
* Function: cfg_load
* Description:  从Flash加载配置项到内存
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
bool cfg_load(void);

/*************************************************
* Function: cfg_get()
* Description: 取得缓存的配置信息
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
config_info* cfg_get(void);

/*************************************************
* Function: cfg_print()
* Description: 输出缓存的配置信息
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
void cfg_print(void);

/**--------------------------------------------------------------------------*
**                         Compiler Flag                                     *
**---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif

#endif // __CONFIG_H__
