/****************************************************************************
 *
 * File Name
 *  http_ota.c
 * Author
 *  
 * Date
 *  2020/04/29
 * Descriptions:
 * HTTP OTA固件下载和升级接口实现
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
#include <tinycrypt.h>
#include <fal.h>

#include "util.h"
#include "strref.h"
#include "common.h"
#include "http_parser.h"
#include "app.h"

#define LOG_TAG              "http_ota"
#define LOG_LVL              LOG_LVL_DBG
#include <rtdbg.h>

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
#define HTTP_OTA_SOCKET_TIMEO (60 * 1000)

/*----------------------------------------------------------------------------*
**                             Data Structures                                *
**----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*
**                             Local Vars                                     *
**----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*
**                             Local Function                                 *
**----------------------------------------------------------------------------*/
static int http_req_read_line(int sock, char *buffer, int size)
{
    int count = 0;
    char ch = 0, last_ch = 0;

    //RT_ASSERT(buffer);

    while (count < size)
    {
        int recv_ret = recv(sock, http_rsp_buf, APP_MP_BLOCK_SIZE, 0);
		if(recv_ret <= 0)
		{
			return recv_ret;
		}

        if ((ch == '\n') 
			&& (last_ch == '\r'))
		{
            break;
		}

		if (count >= size)
		{
			LOG_E(%s "read line failed. The line data length is out of buffer size(%d)!", __FUNCTION__, count);
			return -1;
		}
		
        buffer[count++] = ch;

        last_ch = ch;
    }

    return count;
}

/*----------------------------------------------------------------------------*
**                             Public Function                                *
**----------------------------------------------------------------------------*/
/* 下载固件并执行MD5校验,返回值: 0=下载并校验成功, -1=下载失败, -2=校验失败 */
http_ota_result http_ota_fw_download(const char *url, int firmware_size, uint8_t *orign_md5)
{
	http_ota_result result = HTTP_OTA_DOWNLOAD_AND_VERIFY_SUCCESS;
	int sock = -1;
	struct timeval timeout;
	int iret = 0;
	struct http_parser_url http_url;
	uint16_t host_addr_len = 0;
	const char *host_addr_ptr = NULL;
	uint16_t file_path_len = 0;
	const char *file_path_ptr = NULL;
	char host_addr[128] = "";
	uint16_t port = 80;
	char port_str[16] = "";
	struct addrinfo hint;
	struct addrinfo *res = RT_NULL;
	int file_size = 0;
	char *http_req_buf = app_mp_alloc(); // 请求缓冲区
	char *http_rsp_buf = app_mp_alloc(); // 响应缓冲区
	
	LOG_D("%s url(%s) firmware_size(%d)", __FUNCTION__, url, firmware_size);
	
	/* 解析URL */
	http_parser_url_init(&http_url);
	iret = http_parser_parse_url(url, rt_strlen(url), 1, &http_url);
	if (iret != 0)
	{
		LOG_E("%s http_parser_parse_url(%s) failed(%d)!", __FUNCTION__, url, iret);
		result = HTTP_OTA_DOWNLOAD_FAIL;
		goto __exit;
	}
	if (!(http_url.field_set & (1 << UF_HOST)))
	{
		LOG_E("%s url(%s) host_addr not found!", __FUNCTION__, url);
		result = HTTP_OTA_DOWNLOAD_FAIL;
		goto __exit;
	}
	
	/* 得到主机地址 */
	host_addr_len = http_url.field_data[UF_HOST].len;
	host_addr_ptr = &(url[http_url.field_data[UF_HOST].off]);
	if (host_addr_len >= sizeof(host_addr))
	{
		LOG_E("%s host_addr_len(%u)>=%u!", __FUNCTION__, host_addr_len, sizeof(host_addr));
		result = HTTP_OTA_DOWNLOAD_FAIL;
		goto __exit;
	}
	memcpy(host_addr, host_addr_ptr, host_addr_len);
	host_addr[host_addr_len] = '\0';
	
	/* 得到文件路径 */
	file_path_len = http_url.field_data[UF_PATH].len;
	file_path_ptr = &(url[http_url.field_data[UF_PATH].off]);
	
	/* 得到端口号 */
	if (http_url.field_set & (1 << UF_PORT))
	{
		port = http_url.port;
	}
	rt_snprintf(port_str, sizeof(port_str), "%u", port);
	
	/* 执行DNS解析 */
	iret = getaddrinfo(host_addr, port_str, &hint, &res);
	if (iret != 0)
	{
		LOG_E("%s getaddrinfo(%s:%s) failed(%d)!", __FUNCTION__, host_addr, port_str, iret);
		result = HTTP_OTA_DOWNLOAD_FAIL;
		goto __exit;
	}
	
	/* 连接到HTTP服务器 */
	if ((sock = socket(addr_res->ai_family, SOCK_STREAM, 0)) == -1)
	{
		LOG_E("%s create socket error!", __FUNCTION__);
		result = HTTP_OTA_DOWNLOAD_FAIL;
		goto __exit;
	}

	if (connect(sock, addr_res->ai_addr, addr_res->ai_addrlen) == -1)
	{
		LOG_E("%s connect err!", __FUNCTION__);
		result = HTTP_OTA_DOWNLOAD_FAIL;
		goto __exit;
	}

	/* set send/recv timeout option */
	timeout.tv_sec = HTTP_OTA_SOCKET_TIMEO / 1000;
	timeout.tv_usec = 0;
	setsockopt(c->sock, SOL_SOCKET, SO_RCVTIMEO, (void *)&timeout, sizeof(timeout));
	setsockopt(c->sock, SOL_SOCKET, SO_SNDTIMEO, (void *)&timeout, sizeof(timeout));
	
	/* 取得文件大小 */
	if (file_size == 0)
	{
		/* 发送HTTP HEAD请求 */
		rt_int32_t http_req_len = rt_snprintf(http_req_buf, APP_MP_BLOCK_SIZE, 
			"HEAD %.*s HTTP/1.1\r\nHost: %s\r\n", file_path_len, file_path_ptr, host_addr);
		if (http_req_len <= 0)
		{
			LOG_E("%s http head request is out of buffer!", __FUNCTION__);
			result = HTTP_OTA_DOWNLOAD_FAIL;
			goto __exit;
		}
		int send_ret = send(sock, http_req_buf, http_req_len, 0);
		if(send_ret <= 0)
		{
			// TODO
		}
		
	}
	else
	{
		/* 发送HTTP GET请求分块下载数据 */
		// TODO
	}
	
	/* 接收并解析响应 */
	http_parser parser;
	http_parser_settings settings;
	http_parser_settings_init(&settings);
	settings.on_message_begin = on_http_message_begin;
	settings.on_url = on_http_url;
	settings.on_status = on_http_status;
	settings.on_header_field = on_http_header_field;
	settings.on_header_value = on_http_header_value;
	settings.on_headers_complete = on_http_headers_complete;
	settings.on_body = on_http_body;
	settings.on_message_complete = on_http_message_complete;
	/* When on_chunk_header is called, the current chunk length is stored
	 * in parser->content_length.
	 */
	settings.on_chunk_header = on_http_chunk_header;
	settings.on_chunk_complete = on_http_chunk_complete;
	
	http_parser_init(&parser, HTTP_RESPONSE);
	while (1)
	{
		char ch = '';
		int recv_ret = recv(sock, &ch, sizeof(ch), 0);
		if(recv_ret <= 0)
		{
			// TODO(出错或服务器断开链接)
		}

		http_parser_execute(&parser, &settings, &ch, recv_ret);
	}
	
__exit:

	if (res)
	{
		freeaddrinfo(res);
	}
	app_mp_free(http_req_buf);
	app_mp_free(http_rsp_buf);
	
	return result;
}

/**---------------------------------------------------------------------------*
 **                         Compiler Flag                                     *
 **---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif
// End of http_ota.c
