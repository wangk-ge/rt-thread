/****************************************************************************
 *
 * File Name
 *  util.h
 * Author
 *  
 * Date
 *  2020/04/19
 * Descriptions:
 * UTIL实用工具接口定义
 *
 ****************************************************************************/

#ifndef __UTIL_H__
#define __UTIL_H__

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
* Function: util_str_to_u32
* Description: 字符串转32位无符号整数
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
uint32_t util_str_to_u32(const char* str, uint32_t str_len);

/*************************************************
* Function: util_to_hex_str
* Description: convert data to hex string
* Author: 
* Returns: the data length of convert success
* Parameter:
*  data data to convert
*  data_len data length (in bytes)
*  hex_str_buf buffer to hex string
*  buf_len buffer length (in bytes)
* History:
*************************************************/
uint32_t util_to_hex_str(const uint8_t* data, size_t data_len, 
    char* hex_str_buf, size_t buf_len);

/*************************************************
* Function: util_from_hex_str
* Description: convert hex string to byte data(can be convert in place)
* Author: 
* Returns: the byte data length of convert success
* Parameter:
*  hex_str hex string to convert
*  str_len string length (in bytes)
*  data_buf buffer to byte data
*  buf_len buffer length (in bytes)
* History:
*************************************************/
uint32_t util_from_hex_str(const char* hex_str, size_t str_len, 
    uint8_t* data_buf, size_t buf_len);

/**--------------------------------------------------------------------------*
**                         Compiler Flag                                     *
**---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif

#endif // __UTIL_H__
