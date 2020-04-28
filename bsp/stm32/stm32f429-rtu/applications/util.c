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
#include <rtthread.h>

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
* Function: util_is_ip_valid
* Description: 判断字符串是否为有效的IP地址
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
bool util_is_ip_valid(const char* ip)
{
	bool ret = false;
#if 0
	int n[4] = {0};
	char c[4] = {0};

	// 参数检查
	if (NULL == ip || '\0' == ip[0])
	{
		ret = false;
		goto __exit;
	}

	if (sscanf(ip, "%d%c%d%c%d%c%d%c",
				 &n[0], &c[0], &n[1], &c[1],
				 &n[2], &c[2], &n[3], &c[3]) == 7)
	{
		int i = 0;
		for(i = 0; i < 3; ++i)
		{
			if (c[i] != '.')
			{
				ret = false;
				goto __exit;
			}
		}
		for(i = 0; i < 4; ++i)
		{
			if ((n[i] > 255) || (n[i] < 0))
			{
				ret = false;
				goto __exit;
			}
		}
		ret = true;
	}
#else
	/* 分解为4个子串 */
	c_str_ref str_ref_list[4] = {{0, NULL},};
	uint32_t i = 0;
	uint32_t list_len = 0;
	
	// 参数检查
	if (NULL == ip || '\0' == ip[0])
	{
		ret = false;
		goto __exit;
	}
	
	// 第一个子串起始地址和长度初始化
	str_ref_list[list_len].c_str = ip;
	str_ref_list[list_len].len = 0;
	while ('\0' != ip[i])
	{
		if ('.' == ip[i])
		{
			++list_len;
			if (list_len >= ARRAY_SIZE(str_ref_list))
			{ // 子串超过4个
				ret = false;
				goto __exit;
			}
			// 下一个子串起始地址和长度初始化
			str_ref_list[list_len].c_str = ip + i + 1;
			str_ref_list[list_len].len = 0;
		}
		else
		{
			++(str_ref_list[list_len].len);
		}
		i++;
	}
	if (list_len < ARRAY_SIZE(str_ref_list))
	{ // 最后一个子串
		++list_len;
	}

	// 子串不足4个
	if (ARRAY_SIZE(str_ref_list) != list_len)
	{
		ret = false;
		goto __exit;
	}

	// 检查每个子串有效性
	for (i = 0; i < list_len; ++i)
	{
		const c_str_ref* str_ref = &(str_ref_list[i]);
		if ((str_ref->len <= 0) || (str_ref->len > 3))
		{ // 子串长度无效
			ret = false;
			goto __exit;
		}
		uint32_t j = 0;
		for (j = 0; j < str_ref->len; ++j)
		{
			if (!isdigit(str_ref->c_str[j]))
			{ // 包含非数字字符
				ret = false;
				goto __exit;
			}
		}
		int32_t tmp = atoi(str_ref->c_str);
		if ((tmp > 255) || (tmp < 0))
		{ // 超范围
			ret = false;
			goto __exit;
		}
	}
	ret = true;
#endif

__exit:
	return ret;
}

/*************************************************
* Function: util_is_domainname_valid
* Description: 判断字符串是否为有效的域名
* Author:
* Returns:
* Parameter:
* History:
*************************************************/
bool util_is_domainname_valid(const char* domainname)
{
	bool ret = true;

	/*
			域名中只能包含以下字符：
	　　26个字母(a~z)不区分大小写。
	　　0、1、2、3、4、5、6、7、8、9。
	　　"-" 中横线。
	*/
	uint32_t i = 0;
	while ('\0' != domainname[i])
	{
		if (((domainname[i] >= 'A') && (domainname[i] <= 'Z'))
			|| ((domainname[i] >= 'a') && (domainname[i] <= 'z'))
			|| ((domainname[i] >= '0') && (domainname[i] <= '9'))
			|| ('-' == domainname[i]) || ('.' == domainname[i]))
		{ // 合法字符
			i++;
		}
		else
		{ // 非法字符
			ret = false;
			break;
		}
	}

	return ret;
}

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

/*************************************************
* Function: hex_char_to_byte
* Description: convert hex char to byte data(Example: '0'->0x00,'A'->0x0A)
* Author: 
* Returns: the byte data
* Parameter:
*  hex_char hex char
* History:
*************************************************/
rt_inline uint8_t hex_char_to_byte(char hex_char)
{
    uint8_t byte_val = 0;
    
    if ((hex_char >= '0') && (hex_char <= '9'))
    {
        byte_val = hex_char - '0';
    }
    else if ((hex_char >= 'a') && (hex_char <= 'f'))
    {
        byte_val = 0x0A + (hex_char - 'a');
    }
    else if ((hex_char >= 'A') && (hex_char <= 'F'))
    {
        byte_val = 0x0A + (hex_char - 'A');
    } // else 非法字符
    
    return byte_val;
}

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
    uint8_t* data_buf, size_t buf_len)
{
    if ((NULL == data_buf)
        || (buf_len <=0 ))
    { // 缓冲区无效
        return 0;
    }
    
    if ((NULL == hex_str)
        || (str_len <=0 ))
    { // 数据为空
        return 0;
    }
    
    uint32_t convert_len = MIN(buf_len, (str_len / 2));
    uint32_t i = 0;
    uint32_t j = 0;
    for (i = 0; i < convert_len; ++i)
    {
        char hight_char = hex_str[j++];
        char low_char = hex_str[j++];
        uint8_t high_byte = hex_char_to_byte(hight_char);
        uint8_t low_byte = hex_char_to_byte(low_char);
        data_buf[i] = (high_byte << 4) | low_byte;
    }
    
    return convert_len;
}

/**---------------------------------------------------------------------------*
 **                         Compiler Flag                                     *
 **---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif
// End of util.c
