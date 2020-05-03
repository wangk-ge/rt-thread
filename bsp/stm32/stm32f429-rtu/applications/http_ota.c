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
#include <sys/time.h>
#include <netdb.h>
#include <sys/socket.h>

#include "util.h"
#include "strref.h"
#include "common.h"
#include "http_parser.h"
#include "app.h"
#include "http_ota.h"

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
#define HTTP_OTA_SOCKET_TIMEO (60) // socket收发超时时间(s)
#define HTTP_OTA_RECONNECT_DELAY (5) // 重连延迟时间(s)

/*----------------------------------------------------------------------------*
**                             Data Structures                                *
**----------------------------------------------------------------------------*/
/* HTTP OTA状态 */
typedef enum
{
	HTTP_OTA_INIT = 0,
	HTTP_OTA_GET_SIZE, // 请求获取文件大小
	HTTP_OTA_GET_SIZE_COMPLETE, // 已取得文件大小
	HTTP_OTA_GET_DATA, // 请求下载一包数据
	HTTP_OTA_GET_DATA_COMPLETE, // 已完收到一包数据
	HTTP_OTA_DOWNLOAD_COMPLETE, // 已完成下载
	HTTP_OTA_STATUS_ERROR, // 服务器返回错误状态码
} http_ota_state;

typedef struct
{
	http_ota_state state; // 状态
	size_t file_size; // 文件大小
    char *http_buf; // HTTP缓存(用于发送/接收)
    size_t http_buf_len; // HTTP缓存长度(字节数)
    size_t http_len; // 已缓存HTTP长度(字节数)
	char *data_buf; // 数据缓存
	size_t data_buf_len; // 数据缓存长度(字节数)
	size_t data_len; // 已缓存数据长度(字节数)
} http_ota_context;

/*----------------------------------------------------------------------------*
**                             Local Vars                                     *
**----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*
**                             Local Function                                 *
**----------------------------------------------------------------------------*/
static int on_http_message_begin(http_parser *parser)
{
	http_ota_context *ota_context = (http_ota_context *)parser->data;
	switch (ota_context->state)
	{
		case HTTP_OTA_GET_SIZE:
			ota_context->file_size = 0;
			break;
		case HTTP_OTA_GET_DATA:
			//memset(ota_context->data_buf, 0, ota_context->data_buf_len);
			ota_context->data_len = 0;
			break;
		default:
			break;
	}
    
    return 0;
}

static int on_http_headers_complete(http_parser *parser)
{
    int ret = 0;
	http_ota_context *ota_context = (http_ota_context *)parser->data;
	switch (ota_context->state)
	{
		case HTTP_OTA_GET_SIZE:
			if (parser->status_code == 200)
			{
				ota_context->file_size = (size_t)(parser->content_length);
				ota_context->state = HTTP_OTA_GET_SIZE_COMPLETE;
			}
			else
			{
				LOG_E("%s status(%d) code(%d) error!", __FUNCTION__, ota_context->state, parser->status_code);
				ota_context->state = HTTP_OTA_STATUS_ERROR;
			}
            /* 输出响应头 */
            LOG_I("%s Response Header: %.*s", __FUNCTION__, ota_context->http_len, ota_context->http_buf);
            /* 清空缓冲区 */
            //memset(ota_context->data_buf, 0, ota_context->data_buf_len);
			ota_context->data_len = 0;
            ret = 1; // this message has no body
			break;
		case HTTP_OTA_GET_DATA:
			if ((parser->status_code != 200) 
				&& (parser->status_code != 206))
			{
				LOG_E("%s(%d) status code(%d) error!", __FUNCTION__, ota_context->state, parser->status_code);
				ota_context->state = HTTP_OTA_STATUS_ERROR;
			}
            /* 输出响应头 */
            LOG_I("%s Response Header: %.*s", __FUNCTION__, ota_context->http_len, ota_context->http_buf);
            /* 清空缓冲区 */
            //memset(ota_context->data_buf, 0, ota_context->data_buf_len);
			ota_context->data_len = 0;
			break;
		default:
			break;
	}
    
    return ret;
}

static int on_http_body(http_parser *parser, const char *at, size_t length)
{
	http_ota_context *ota_context = (http_ota_context *)parser->data;
	switch (ota_context->state)
	{
		case HTTP_OTA_GET_DATA:
		{
			/* 在Buffer中缓存数据 */
			size_t cache_size = ota_context->data_buf_len - ota_context->data_len; // 剩余Cache大小
			if (cache_size >= length)
			{
				memcpy(ota_context->data_buf + ota_context->data_len, at, length);
				ota_context->data_len += length;
			}
			else
			{ // 缓冲区不足
				/* 输出警告,如果出现该警告则需要调大Cache缓冲区 */
				LOG_E("%s(%d) warning, data out of cache buffer size(%u)!", __FUNCTION__, ota_context->state, ota_context->data_len);
                ota_context->state = HTTP_OTA_STATUS_ERROR;
			}
			break;
		}
		default:
			break;
	}
    
    return 0;
}

static int on_http_message_complete(http_parser *parser)
{
	http_ota_context *ota_context = (http_ota_context *)parser->data;
	switch (ota_context->state)
	{
		case HTTP_OTA_GET_DATA:
			ota_context->state = HTTP_OTA_GET_DATA_COMPLETE;
			break;
		default:
			break;
	}
    
    return 0;
}

/*----------------------------------------------------------------------------*
**                             Public Function                                *
**----------------------------------------------------------------------------*/
/* 下载固件并执行MD5校验 */
http_ota_result http_ota_fw_download(const char *url, size_t firmware_size, 
	uint8_t *orign_md5, uint32_t retry_count)
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
	struct addrinfo *addr_res = NULL;
	size_t download_len = 0; // 已下载长度(字节数)
	tiny_md5_context md5_ctx;
    unsigned char md5[16] = {0};
	const struct fal_partition *dl_part = RT_NULL;
	http_parser parser;
	http_parser_settings parser_settings;
	http_ota_context ota_context = {
		.state = HTTP_OTA_INIT, // 状态
		.file_size = 0, // 文件大小
        .http_buf = NULL, // HTTP缓存
		.http_buf_len = 0, // HTTP缓存长度(字节数)
		.http_len = 0, // 已缓存HTTP长度(字节数)
		.data_buf = NULL, // 数据缓存
		.data_buf_len = 0, // 数据缓存长度(字节数)
		.data_len = 0 // 已缓存数据长度(字节数)
	};
	
	LOG_D("%s url(%s) firmware_size(%d) retry_count(%d)", __FUNCTION__, url, firmware_size, retry_count);
    
    retry_count++; // 第一次区得文件大小之后需要断线重连
	
    /* 分配缓存 */
	ota_context.http_buf = app_mp_alloc();
    ota_context.http_buf_len = APP_MP_BLOCK_SIZE;
	ota_context.data_buf = app_mp_alloc();
	ota_context.data_buf_len = APP_MP_BLOCK_SIZE;
    
    /* 配置http_parser */
	http_parser_settings_init(&parser_settings);
	parser_settings.on_message_begin = on_http_message_begin;
	parser_settings.on_headers_complete = on_http_headers_complete;
	parser_settings.on_body = on_http_body;
	parser_settings.on_message_complete = on_http_message_complete;
	parser.data = &ota_context;
	
	/* 解析URL */
	http_parser_url_init(&http_url);
	iret = http_parser_parse_url(url, rt_strlen(url), 0, &http_url);
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
	
	/* 初始化MD5计算器 */
	tiny_md5_starts(&md5_ctx);
	
__retry:
	/* 执行DNS解析 */
	iret = getaddrinfo(host_addr, port_str, NULL, &addr_res);
	if (iret != 0)
	{
		LOG_W("%s getaddrinfo(%s:%s) failed(%d), retry!", __FUNCTION__, host_addr, port_str, iret);
		goto __close_connect_and_retry;
	}
	
	/* 创建Socket */
	if ((sock = socket(addr_res->ai_family, SOCK_STREAM, 0)) == -1)
	{
		LOG_W("%s create socket error, retry!", __FUNCTION__);
		goto __close_connect_and_retry;
	}

	/* 连接到HTTP服务器 */
	if (connect(sock, addr_res->ai_addr, addr_res->ai_addrlen) == -1)
	{
		LOG_E("%s connect err, retry!", __FUNCTION__);
		goto __close_connect_and_retry;
	}
	
	/* 释放addrinfo */
	if (addr_res)
	{
        freeaddrinfo(addr_res);
        addr_res = NULL;
    }

	/* set send/recv timeout option */
	timeout.tv_sec = HTTP_OTA_SOCKET_TIMEO;
	timeout.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (void *)&timeout, sizeof(timeout));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (void *)&timeout, sizeof(timeout));
	
	/* 初始化HTTP Parser */
	http_parser_init(&parser, HTTP_RESPONSE);
	
	if (ota_context.file_size == 0)
	{
		ota_context.state = HTTP_OTA_GET_SIZE; // 请求获取文件大小
        /* 清空缓冲区 */
        //memset(ota_context.data_buf, 0, ota_context.data_buf_len);
        ota_context.data_len = 0;
	}
	else
	{
		ota_context.state = HTTP_OTA_GET_DATA; // 请求下载文件数据
        /* 清空缓冲区 */
        //memset(ota_context.data_buf, 0, ota_context.data_buf_len);
        ota_context.data_len = 0;
	}
	
	while (1)
	{
		/* 发送HTTP请求 */
		switch (ota_context.state)
		{
			case HTTP_OTA_GET_SIZE: /* 取得文件大小 */
			{
				/* 发送HTTP HEAD请求 */
				rt_int32_t http_req_len = rt_snprintf(ota_context.http_buf, ota_context.http_buf_len, 
					"HEAD %.*s HTTP/1.1\r\nHost: %s\r\n\r\n", // User-Agent: RT-Thread HTTP Agent\r\nConnection: keep-alive\r\n
                    file_path_len, file_path_ptr, host_addr);
				if (http_req_len <= 0)
				{
					LOG_E("%s http HEAD request is out of buffer size(%u)!", __FUNCTION__, ota_context.http_buf_len);
					result = HTTP_OTA_DOWNLOAD_FAIL;
					goto __exit;
				}
                ota_context.http_len = http_req_len;
                
                LOG_I("%s Send: %.*s", __FUNCTION__, ota_context.http_len, ota_context.http_buf);
                
				int send_ret = send(sock, ota_context.http_buf, http_req_len, 0);
				if(send_ret != http_req_len)
				{
					LOG_W("%s socket send error(%d), reconnect!", __FUNCTION__, send_ret);
					goto __close_connect_and_retry;
				}
				break;
			}
			case HTTP_OTA_GET_DATA:
			{
				/* 发送HTTP GET请求分块下载数据 */
                size_t block_size = MIN((ota_context.file_size - download_len), ota_context.data_buf_len);
				rt_int32_t http_req_len = rt_snprintf(ota_context.http_buf, ota_context.http_buf_len, 
					"GET %.*s HTTP/1.1\r\nRange: bytes=%u-%u\r\nHost: %s\r\n\r\n", // User-Agent: RT-Thread HTTP Agent\r\nConnection: keep-alive\r\n
                    file_path_len, file_path_ptr, download_len, (download_len + block_size - 1), host_addr);
				if (http_req_len <= 0)
				{
					LOG_E("%s http GET request is out of buffer size(%u)!", __FUNCTION__, APP_MP_BLOCK_SIZE);
					result = HTTP_OTA_DOWNLOAD_FAIL;
					goto __exit;
				}
                ota_context.http_len = http_req_len;
                
                LOG_I("%s Send: %.*s", __FUNCTION__, ota_context.http_len, ota_context.http_buf);
                
				int send_ret = send(sock, ota_context.http_buf, http_req_len, 0);
				if (send_ret != http_req_len)
				{
					LOG_W("%s socket send error(%d), reconnect!", __FUNCTION__, send_ret);
					goto __close_connect_and_retry;
				}
				break;
			}
			default:
			{
				LOG_E("%s invalid sate(%d)!", __FUNCTION__, ota_context.state);
				result = HTTP_OTA_DOWNLOAD_FAIL;
				goto __exit;
				//break;
			}
		}
		
		/* 接收并解析HTTP响应 */
		while (1)
		{
			bool stop_recv = false;
			int recv_ret = recv(sock, ota_context.http_buf, ota_context.http_buf_len, 0);
			if(recv_ret < 0)
			{
                if(!(errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN))
                {
                    LOG_W("%s socket recv error(%d), reconnect!", __FUNCTION__, recv_ret);
                    goto __close_connect_and_retry;
                }
			}
            else if(recv_ret == 0) // 服务器断开了连接
            {
                LOG_W("%s connection close by server, reconnect!", __FUNCTION__, recv_ret);
                goto __close_connect_and_retry;
            }
            ota_context.http_len = recv_ret;
            
			if (http_parser_execute(&parser, &parser_settings, ota_context.http_buf, ota_context.http_len) != ota_context.http_len)
			{
				LOG_W("%s http_parser_execute error, reconnect!", __FUNCTION__);
				goto __close_connect_and_retry;
			}
			
			switch (ota_context.state)
			{
				case HTTP_OTA_GET_SIZE_COMPLETE:
				{
                    LOG_I("%s File size=%d.", __FUNCTION__, ota_context.file_size);
                    
					/* 检查文件长度 */
					if (firmware_size != ota_context.file_size)
					{
						LOG_W("%s firmware_size(%d) file_size(%d) not equal!", __FUNCTION__, firmware_size, ota_context.file_size);
					}
					
					/* 擦除download分区 */
					/* Get download partition information and erase download partition data */
					if ((dl_part = fal_partition_find("download")) == RT_NULL)
					{
						LOG_E("%s Firmware download failed! Partition (download) find error!", __FUNCTION__);
						result = HTTP_OTA_DOWNLOAD_FAIL;
						goto __exit;
					}
					LOG_I("%s Start erase flash (%s) partition.", __FUNCTION__, dl_part->name);

					if (fal_partition_erase(dl_part, 0, ota_context.file_size) < 0)
					{
						LOG_E("%s Firmware download failed! Partition (%s) erase error!", __FUNCTION__, dl_part->name);
						result = HTTP_OTA_DOWNLOAD_FAIL;
						goto __exit;
					}
					LOG_I("%s Erase flash (%s) partition success.", __FUNCTION__, dl_part->name);

					//ota_context.state = HTTP_OTA_GET_DATA; // 请求下载文件数据
					//stop_recv = true; // 停止接收
                    /* 重新连接,并使用分块下载方式 */
                    goto __close_connect_and_retry;
					//break;
				}
				case HTTP_OTA_GET_DATA_COMPLETE:
				{
					if (ota_context.data_len > 0)
					{
						uint8_t progress = 0; // 下载进度(%)
						
						/* Write the data to the corresponding partition address */
						if (fal_partition_write(dl_part, download_len, (const uint8_t*)(ota_context.data_buf), ota_context.data_len) < 0)
						{
							LOG_E("%s Firmware download failed! Partition (%s) write data error!", __FUNCTION__, dl_part->name);
							result = HTTP_OTA_DOWNLOAD_FAIL;
							goto __exit;
						}
						download_len += ota_context.data_len;
						
						tiny_md5_update(&md5_ctx, (unsigned char*)(ota_context.data_buf), ota_context.data_len);

						/* 输出当前下载进度 */
						progress = download_len * 100 / ota_context.file_size;
						LOG_I("%s Download: %d%%", __FUNCTION__, progress);
					}
					else
					{
						LOG_W("%s HTTP_OTA_GET_DATA_COMPLETE data_len(%d)!", __FUNCTION__, ota_context.data_len);
					}
					
                    /* 清空缓冲区 */
                    //memset(ota_context.data_buf, 0, ota_context.data_buf_len);
                    ota_context.data_len = 0;
                    
					if (download_len == ota_context.file_size)
					{ // 下载完毕
						ota_context.state = HTTP_OTA_DOWNLOAD_COMPLETE; // 下载完成
					}
					else
					{ // 继续下载
						ota_context.state = HTTP_OTA_GET_DATA; // 继续请求下载文件数据
					}
					
					stop_recv = true; // 停止接收
					break;
				}
				case HTTP_OTA_STATUS_ERROR:
				{
					LOG_W("%s HTTP_OTA_STATUS_ERROR, reconnect!", __FUNCTION__);
					goto __close_connect_and_retry;
					//break;
				}
				default:
                {
					break;
                }
			}
			
			if (stop_recv)
			{ // 停止接收
				break;
			}
		}
		
		if (ota_context.state == HTTP_OTA_DOWNLOAD_COMPLETE)
		{ // 下载完毕
			break;
		}
	}
	
	RT_ASSERT(ota_context.state == HTTP_OTA_DOWNLOAD_COMPLETE);
	
	/* 计算MD5 */
	tiny_md5_finish(&md5_ctx, md5);
    
    LOG_I("%s Download firmware to flash success. md5: %x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x", __FUNCTION__,
        md5[0], md5[1], md5[2], md5[3], md5[4], md5[5], md5[6], md5[7], md5[8], md5[9], md5[10], md5[11], 
		md5[12], md5[13], md5[14], md5[15]);
    
	/* 校验MD5 */
    if ((orign_md5 != NULL)
        && memcmp(md5, orign_md5, sizeof(md5)) != 0)
    {
        LOG_E("%s Exit: md5 check failed!", __FUNCTION__);
        result = HTTP_OTA_VERIFY_FAIL;
		goto __exit;
    }
	
	/* 下载并校验成功 */
	result = HTTP_OTA_DOWNLOAD_AND_VERIFY_SUCCESS;
	
	goto __exit;

__close_connect_and_retry:
	LOG_D("%s __close_connect_and_retry", __FUNCTION__);
	
	/* 是否已达到指定重试次数 */
	if (retry_count <= 0)
	{
		LOG_E("%s retry count reached!", __FUNCTION__);
		result = HTTP_OTA_DOWNLOAD_FAIL;
		goto __exit;
	}
	
	/* 重试次数递减 */
	--retry_count;
	
	/* 释放addrinfo */
	if (addr_res)
	{
      freeaddrinfo(addr_res);
      addr_res = NULL;
    }
	
	/* 关闭连接 */
	if (sock >= 0)
	{
      closesocket(sock);
      sock = -1;
    }
	
	/* 延迟指定时间后再次尝试 */
    rt_thread_delay(rt_tick_from_millisecond(HTTP_OTA_RECONNECT_DELAY * 1000));
	goto __retry;
	
__exit:
	LOG_D("%s __exit", __FUNCTION__);

	/* 释放addrinfo */
	if (addr_res)
	{
      freeaddrinfo(addr_res);
      addr_res = NULL;
    }
	
	/* 关闭连接 */
	if (sock >= 0)
	{
      closesocket(sock);
      sock = -1;
    }
	
	/* 释放缓冲区 */
	app_mp_free(ota_context.http_buf);
	app_mp_free(ota_context.data_buf);
	
	return result;
}

/* 重启系统进行升级 */
void http_ota_reboot(void)
{
    LOG_I("%s System now will restart...", __FUNCTION__);

    rt_thread_delay(rt_tick_from_millisecond(5));

    /* Reset the device, Start new firmware */
    extern void rt_hw_cpu_reset(void);
    rt_hw_cpu_reset();
}

/**---------------------------------------------------------------------------*
 **                         Compiler Flag                                     *
 **---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif
// End of http_ota.c
