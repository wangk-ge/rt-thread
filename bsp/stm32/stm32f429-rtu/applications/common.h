/****************************************************************************
 *
 * File Name
 *  common.h
 * Author
 *  
 * Date
 *  2020/04/19
 * Descriptions:
 * 公共定义头文件
 *
 ****************************************************************************/

#ifndef __COMMON_H__
#define __COMMON_H__

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
#define SW_VERSION ("V2.0.0")
#define HW_VERSION ("V1.0.0")
    
#if !defined(MAX)
#define MAX(a,b)    (((a) > (b)) ? (a) : (b))
#endif

#if !defined(MIN)
#define MIN(a,b)    (((a) < (b)) ? (a) : (b))
#endif
    
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

#ifndef STR_LEN
#define STR_LEN(str) (sizeof(str) - 1)
#endif

#ifndef STR_ITEM
#define STR_ITEM(s) (s), STR_LEN(s)
#endif

/* 半字节转换为HEX字符 */
#define TO_HEX_CHAR(b) (((b) <= 0x09) ? ((b) + (uint8_t)'0') : (((b) - 0x0A) + (uint8_t)'A'))

// Case返回枚举常量(或宏)名称字符串
#define MACRO_NAME_CASE(eMacro) case eMacro: return #eMacro

/*----------------------------------------------------------------------------*
**                             Data Structures                                *
**----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*
**                             Function Define                                *
**----------------------------------------------------------------------------*/
    
/**--------------------------------------------------------------------------*
**                         Compiler Flag                                     *
**---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif

#endif // __COMMON_H__
