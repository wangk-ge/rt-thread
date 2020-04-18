/****************************************************************************
 *
 * File Name
 *  strref.c
 * Author
 *  wangk
 * Date
 *  2018/01/16
 * Descriptions:
 * 字符串reference接口实现
 *
 ******************************************************************************/
/*----------------------------------------------------------------------------*
**                             Dependencies                                   *
**----------------------------------------------------------------------------*/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "common.h"
#include "util.h"
#include "strref.h"

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
* Function: strref_is_empty
* Description: 判断c_str_ref类型字符串是否为空
* Author: wangk
* Returns: 
* Parameter:
* History:
*************************************************/
bool strref_is_empty(const c_str_ref* str_ref)
{
    if (NULL == str_ref)
    {
        return true;
    }
    
    if ((NULL == str_ref->c_str) || (str_ref->len <= 0))
    {
        return true;
    }
    
    return false;
}

/*************************************************
* Function: strref_str_cpy
* Description:  拷贝c_str_ref类型的字符串到指定长度缓冲区
*               超过长度会被截断,末尾自动添加'\0'
* Author: wangk
* Returns: 返回实际拷贝的长度(字节)
* Parameter:
* History:
*************************************************/
uint32_t strref_str_cpy(char* buf, uint32_t buf_len, const c_str_ref* str_ref)
{
    if ((NULL == buf) || (buf_len <= 0))
    {
        return 0;
    }
    if ((NULL == str_ref) || (str_ref->len <= 0))
    {
        buf[0] = '\0';
        return 0;
    }
    uint32_t copy_len = MIN(buf_len - 1, str_ref->len);
    memcpy(buf, str_ref->c_str, copy_len);
    buf[copy_len] = '\0';
    return copy_len;
}

/*************************************************
* Function: strref_str_cmp
* Description:  比较c_str_ref类型的字符串和C字符串的内容
*               行为类似于strcmp
* Author: wangk
* Returns: 1 0 -1
* Parameter:
* History:
*************************************************/
int32_t strref_str_cmp(const char* str, const c_str_ref* str_ref)
{
    if ((NULL == str) && (NULL == str_ref))
    {
        return 0;
    }
    if ((NULL == str) && (NULL != str_ref))
    {
        return -1;
    }
    if ((NULL != str) && (NULL == str_ref))
    {
        return 1;
    }
    if (('\0' == str[0]) && (0 == str_ref->len))
    {
        return 0;
    }
    if (('\0' == str[0]) && (0 != str_ref->len))
    {
        return -1;
    }
    if (('\0' != str[0]) && (0 == str_ref->len))
    {
        return 1;
    }
    return strncmp(str, str_ref->c_str, str_ref->len);
}

/*************************************************
* Function: strref_to_int
* Description:  转换c_str_ref类型的字符串为有符号整数值(可处理正负号)
* Author: wangk
* Returns: 转换结果
* Parameter:
* History:
*************************************************/
int32_t strref_to_int(const c_str_ref* str_ref)
{
    if ((NULL == str_ref) || (str_ref->len <= 0))
    {
        return 0;
    }
    char buf[32] = "";
    strref_str_cpy(buf, sizeof(buf), str_ref);

    int32_t val = 0;
    const char* pszStr = buf;

    if (('+' == buf[0]) || ('-' == buf[0]))
    { // 处理正负号
        ++pszStr;
    }

    val = (int32_t)atoi(pszStr);

    if ('-' == buf[0])
    { // 负数
        val = -val;
    }

    return val;
}

/*************************************************
* Function: strref_to_u32
* Description:  转换c_str_ref类型的字符串为32位无符号整数值
* Author: wangk
* Returns: 转换结果
* Parameter:
* History:
*************************************************/
uint32_t strref_to_u32(const c_str_ref* str_ref)
{
    if (strref_is_empty(str_ref))
    {
        return 0;
    }

    return util_str_to_u32(str_ref->c_str, str_ref->len);
}

/*************************************************
* Function: strref_is_int
* Description: 判断c_str_ref类型的字符串是否为整型数字串形式
* Author: wangk
* Returns: 判断结果
* Parameter:
* History:
*************************************************/
bool strref_is_int(const c_str_ref* str_ref)
{
    if ((NULL == str_ref) || (str_ref->len <= 0))
    {
        return false;
    }
    bool bRet = true;
    uint32_t i = 0;
    if (('+' == str_ref->c_str[0]) || ('-' == str_ref->c_str[0]))
    {
        ++i;
    }
    if (i >= str_ref->len)
    {
        return false;
    }
    for (; i < str_ref->len; ++i)
    {
        if (!isdigit(str_ref->c_str[i]))
        {
            bRet = false;
            break;
        }
    }
    return bRet;
}

/*************************************************
* Function: strref_split
* Description: 将字符串按指定字符分解成子串
* Author: wangk
* Returns: 返回分解结果子串个数
* Parameter:
* History:
*************************************************/
uint32_t strref_split(const c_str_ref* str_ref, char ch_sep, c_str_ref list_buf[], uint32_t buf_len)
{
    // 参数检查
    if (strref_is_empty(str_ref) 
        || (NULL == list_buf)
        || (buf_len <= 0))
    {
        return 0;
    }
    
    uint32_t list_len = 0;
    uint32_t i = 0;

    // 第一个子串起始地址和长度初始化
    list_buf[list_len].c_str = str_ref->c_str;
    list_buf[list_len].len = 0;
    for (i = 0; i < str_ref->len; ++i)
    {
        if (ch_sep == str_ref->c_str[i])
        {
            ++list_len;
            if (list_len >= buf_len)
            { // 超过最大Split数
                break;
            }
            // 下一个子串起始地址和长度初始化
            list_buf[list_len].c_str = str_ref->c_str + i + 1;
            list_buf[list_len].len = 0;
        }
        else
        {
            ++(list_buf[list_len].len);
        }
    }
    
    if (list_len >= buf_len)
    { // 子串个数超过期望
        return 0;
    }
    
    // 计入最后一个子串
    ++list_len;

    return list_len;
}

/**---------------------------------------------------------------------------*
 **                         Compiler Flag                                     *
 **---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif
// End of strref.c
