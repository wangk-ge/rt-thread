/****************************************************************************
 *
 * File Name
 *  strref.h
 * Author
 *  wangk
 * Date
 *  2018/01/16
 * Descriptions:
 * 字符串reference接口定义
 *
 ****************************************************************************/

#ifndef __STRREF_H__
#define __STRREF_H__

#include <stdbool.h>
#include <stdint.h>

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
// 字符串reference类型定义
typedef struct
{
    uint32_t len;
    const char* c_str;
} c_str_ref;

/*----------------------------------------------------------------------------*
**                             Function Define                                *
**----------------------------------------------------------------------------*/
/*************************************************
* Function: strref_is_empty
* Description: 判断c_str_ref类型字符串是否为空
* Author: wangk
* Returns: 
* Parameter:
* History:
*************************************************/
bool strref_is_empty(const c_str_ref* str_ref);

/*************************************************
* Function: strref_str_cpy
* Description:  拷贝c_str_ref类型的字符串到指定长度缓冲区
*               超过长度会被截断,末尾自动添加'\0'
* Author: wangk
* Returns: 返回实际拷贝的长度(字节)
* Parameter:
* History:
*************************************************/
uint32_t strref_str_cpy(char* buf, uint32_t buf_len, const c_str_ref* str_ref);

/*************************************************
* Function: strref_str_cmp
* Description:  比较c_str_ref类型的字符串和C字符串的内容
*               行为类似于strcmp
* Author: wangk
* Returns: 1 0 -1
* Parameter:
* History:
*************************************************/
int32_t strref_str_cmp(const char* c_str, const c_str_ref* str_ref);

/*************************************************
* Function: strref_to_int
* Description:  转换c_str_ref类型的字符串为有符号整数值(可处理正负号)
* Author: wangk
* Returns: 转换结果
* Parameter:
* History:
*************************************************/
int32_t strref_to_int(const c_str_ref* str_ref);

/*************************************************
* Function: strref_to_u32
* Description:  转换c_str_ref类型的字符串为32位无符号整数值
* Author: wangk
* Returns: 转换结果
* Parameter:
* History:
*************************************************/
uint32_t strref_to_u32(const c_str_ref* str_ref);

/*************************************************
* Function: strref_is_int
* Description: 判断c_str_ref类型的字符串是否为整型数字串形式
* Author: wangk
* Returns: 判断结果
* Parameter:
* History:
*************************************************/
bool strref_is_int(const c_str_ref* str_ref);

/*************************************************
* Function: strref_split
* Description: 将字符串按指定字符分解成子串
* Author: wangk
* Returns: 返回分解结果子串个数
* Parameter:
* History:
*************************************************/
uint32_t strref_split(const c_str_ref* str_ref, char ch_sep, c_str_ref list_buf[], uint32_t buf_len);

/**--------------------------------------------------------------------------*
**                         Compiler Flag                                     *
**---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif

#endif // __STRREF_H__
