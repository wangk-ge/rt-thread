/****************************************************************************
 *
 * File Name
 *  util.c
 * Author
 *  
 * Date
 *  2020/04/19
 * Descriptions:
 * UTIL实用工具接口实现
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

#include "util.h"
#include "strref.h"
#include "common.h"

/**---------------------------------------------------------------------------*
 **                            Debugging Flag                                 *
 **---------------------------------------------------------------------------*/

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
**                             Local Vars                                     *
**----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*
**                             Local Function                                 *
**----------------------------------------------------------------------------*/
    
/*----------------------------------------------------------------------------*
**                             Public Function                                *
**----------------------------------------------------------------------------*/
/*************************************************
* Function: util_str_to_u32
* Description: 字符串转32位无符号整数
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
uint32_t util_str_to_u32(const char* str, uint32_t str_len)
{
    if ((NULL == str) 
        || (str_len <= 0))
    {
        return 0;
    }
    
    uint32_t val = 0;
    uint32_t i = 0;
    for (i = 0; i < str_len; ++i)
    {
        char ch = str[i];
        if (!isdigit(ch))
        {
            break;
        }
        val = val * 10 + (ch - '0');
    }

    return val;
}

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
    char* hex_str_buf, size_t buf_len)
{
    if ((NULL == hex_str_buf)
        || (buf_len <=0 ))
    { // 缓冲区无效
        return 0;
    }
    
    /* 清空缓冲区为"" */
    hex_str_buf[0] = '\0';
    
    if ((NULL == data)
        || (data_len <=0 ))
    { // 数据为空
        return 0;
    }

    /* 最大可转换长度为: ((buf_len - 1) / 2) */
    uint32_t convert_len = MIN(data_len, ((buf_len - 1) / 2));
    uint32_t i = 0;
    uint32_t j = 0;
    for (i = 0; i < convert_len; ++i)
    {
        uint8_t data_byte = data[i];
        uint8_t high = (data_byte >> 4) & 0x0F;
        uint8_t low = data_byte & 0x0F;
        hex_str_buf[j++] = TO_HEX_CHAR(high);
        hex_str_buf[j++] = TO_HEX_CHAR(low);
    }
    hex_str_buf[j] = '\0';

    /* 返回实际转换的数据长度 */
    return convert_len;
}

/**---------------------------------------------------------------------------*
 **                         Compiler Flag                                     *
 **---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif
// End of util.c
