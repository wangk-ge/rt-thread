/*
 * File      : at_socket_tb22.c
 * This file is part of RT-Thread RTOS
 * Copyright (c) 2020, RudyLo <luhuadong@163.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Change Logs:
 * Date           Author            Notes
 * 2020-02-13     luhuadong         first version
 */

#include <stdio.h>
#include <string.h>
#include <rtthread.h>
#include "common.h"
#include "util.h"

#include <at_device_tb22.h>

#define LOG_TAG                        "at.skt.tb22"
#include <at_log.h>

#if defined(AT_DEVICE_USING_TB22) && defined(AT_USING_SOCKET)

#define TB22_MODULE_SEND_MAX_SIZE           1358 // AT+NSOSD最大传输1358字节

#define TB22_SOCK_RECV_PACKET_NUM           24 // Socket接收包个数(用于内存池分配)
#define TB22_SOCK_RECV_PACKET_SIZE          64 // Socket接收包大小(用于内存池分配)
#define TB22_SOCK_RECV_THREAD_STACK_SIZE    2048
#define TB22_SOCK_RECV_THREAD_PRIORITY      (RT_THREAD_PRIORITY_MAX / 3 - 2) // 优先级应该高于at_client线程

/* set real event by current socket and current state */
#define SET_EVENT(socket_index, event)       (((socket_index + 1) << 16) | (event))

#if !defined(MIN)
#define MIN(a,b)    (((a) < (b)) ? (a) : (b))
#endif

/* 半字节转换为HEX字符 */
#define TO_HEX_CHAR(b) (((b) <= 0x09) ? ((b) + (uint8_t)'0') : (((b) - 0x0A) + (uint8_t)'A'))

/* AT socket event type */
#define TB22_EVENT_REQ_RECV_THREAD_QUIT (1L << 0) // 请求Socket接收线程退出
#define TB22_EVENT_RECV_THREAD_QUIT     (1L << 1) // Socket接收线程已退出
#define TB22_EVENT_REQ_RECV_DATA        (1L << 2) // 请求Socket接收线程接收数据
#define TB22_EVENT_SEND_OK              (1L << 3) // 发送成功
#define TB22_EVENT_SEND_FAIL            (1L << 4) // 发送失败
#define TB22_EVENT_DOMAIN_OK            (1L << 5) // DNS解析成功

/* 发送数据缓冲区大小(字节数) */
#define TB22_SEND_BUF_SIZE             (TB22_MODULE_SEND_MAX_SIZE * 2)

/* TB22设备Socket信息 */
struct tb22_sock_t {
    int index;
    int device_socket;
    struct at_device *device;
    uint8_t sequence;
    rt_mp_t recv_buf_mp; // 接收数据内存池
    rt_thread_t recv_thread; // 接收线程
};

/* Socket接收信息 */
struct tb22_sock_recv_info_t {
    struct at_device *device;
    int device_socket;
    rt_size_t data_len;
};

static at_evt_cb_t at_evt_cb_set[] = {
    [AT_SOCKET_EVT_RECV] = NULL,
    [AT_SOCKET_EVT_CLOSED] = NULL,
};

/* 发送数据缓冲区 */
static char at_send_buf[TB22_SEND_BUF_SIZE] = "";

/**
 * update the sequence value for AT+NSOSD
 *
 * @param socket current socket
 *
 */
static void tb22_update_sequence(struct at_socket *socket)
{
    struct tb22_sock_t *tb22_sock = (struct tb22_sock_t*)(socket->user_data);
    
    if (tb22_sock->sequence >= 255)
    {
        tb22_sock->sequence = 1;
    }
    else
    {
        tb22_sock->sequence++;
    }
}

static int tb22_socket_event_send(struct at_device *device, uint32_t event)
{
    return (int) rt_event_send(device->socket_event, event);
}

static int tb22_socket_event_recv(struct at_device *device, uint32_t event, uint32_t timeout, rt_uint8_t option)
{
    int result = RT_EOK;
    rt_uint32_t recved;

    result = rt_event_recv(device->socket_event, event, option | RT_EVENT_FLAG_CLEAR, timeout, &recved);
    if (result != RT_EOK)
    {
        return -RT_ETIMEOUT;
    }

    return recved;
}

/**
 * get at socket object by device socket descriptor
 *
 * @param device at device object
 * @param device_socket device socket descriptor
 *
 * @return  device socket descriptor or RT_NULL
 */
static struct at_socket *at_get_socket_by_device_socket(struct at_device *device, int device_socket)
{
    int i = 0;
    struct at_socket *socket = RT_NULL;
    
    rt_base_t level = rt_hw_interrupt_disable();
    
    for (i = 0; i < device->class->socket_num; ++i)
    {
        struct at_socket *sock = &(device->sockets[i]);
        if (sock->magic == AT_SOCKET_MAGIC)
        {
            struct tb22_sock_t *tb22_sock = (struct tb22_sock_t*)(sock->user_data);
            if (tb22_sock != RT_NULL)
            {
                if((device_socket == tb22_sock->device_socket)
                    && (i == tb22_sock->index))
                {
                    socket = sock;
                    break;
                }
            }
        }
    }
    
    rt_hw_interrupt_enable(level);
    
    return socket;
}

/**
 * socket recv thread entry
 *
 */
static void tb22_sock_recv_thread_entry(void *parameter)
{
    struct tb22_sock_t* tb22_sock = (struct tb22_sock_t*)parameter;
    int device_socket = tb22_sock->device_socket;
    int socket_index = tb22_sock->index;
    struct at_device *device = tb22_sock->device;
    rt_event_t socket_event = device->socket_event;
    
    while (1)
    {
        rt_uint32_t recved_event = 0;
        uint32_t event_set = SET_EVENT(socket_index, TB22_EVENT_REQ_RECV_THREAD_QUIT | TB22_EVENT_REQ_RECV_DATA);
        rt_err_t ret = rt_event_recv(socket_event, event_set, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, RT_WAITING_FOREVER, &recved_event);
        if (ret != RT_EOK)
        {
            LOG_E("%s socket_index(%d) device_socket(%d) rt_event_recv failed(%d), quit!", __FUNCTION__, socket_index, device_socket, ret);
            break;
        }
        
        event_set = SET_EVENT(socket_index, TB22_EVENT_REQ_RECV_THREAD_QUIT);
        if (recved_event & event_set)
        {
            LOG_D("%s socket_index(%d) device_socket(%d) rt_event_recv(TB22_EVNET_REQ_RECV_THREAD_QUIT) quit.", __FUNCTION__, socket_index, device_socket);
            break;
        }
        
        event_set = SET_EVENT(socket_index, TB22_EVENT_REQ_RECV_DATA);
        if (recved_event & event_set)
        {
            LOG_D("%s socket_index(%d) device_socket(%d) rt_event_recv(TB22_EVNET_REQ_RECV_DATA)", __FUNCTION__, socket_index, device_socket);
            
            /* get at socket object by device socket descriptor */
            struct at_socket *socket = at_get_socket_by_device_socket(device, device_socket);
            if (socket == RT_NULL)
            {
                LOG_E("get at socket object by device socket descriptor(%d) failed.", device_socket);
                continue;
            }
            
            at_response_t resp = tb22_alloc_at_resp(device, 0, rt_tick_from_millisecond(10 * 1000));
            RT_ASSERT(resp);

            /* 收取Socket数据 */
            while (1)
            { // 收取所有数据
                // 每个HEX占两个字节再加上字符串结尾'\0'
                char hex_str[TB22_SOCK_RECV_PACKET_SIZE * 2 + 1] = ""; // 注意TB22_SOCK_RECV_PACKET_SIZE太大将导致栈溢出
                int rem_len = 0; // 剩余数据长度
                int sock = 0, port = 0, bytes_len = 0;
                char ip[32] = "";
                
                if (at_obj_exec_cmd(device->client, resp, "AT+NSORF=%d,%d", device_socket, (sizeof(hex_str) - 1)) != RT_EOK)
                {
                    LOG_E("at_obj_exec_cmd failed!");
                    break;
                }
                
                if (at_resp_parse_line_args(resp, 2, "%d,%[^,],%d,%d,%[^,],%d", &sock, ip, &port, &bytes_len, hex_str, &rem_len) <= 0)
                {
                    LOG_E("at_resp_parse_line_args failed!");
                    break;
                }
                RT_ASSERT(device_socket == sock);
                
                if (bytes_len <= 0)
                {
                    LOG_E("bytes_len(%d) is invalid!", bytes_len);
                    break;
                }
                
                /* 转换数据格式并发送数据到上层 */
                {
                    /* 在内存池分配一块接收缓冲区(固定大小TB22_SOCK_RECV_PACKET_SIZE) */
                    uint32_t recv_buf_len = TB22_SOCK_RECV_PACKET_SIZE;
                    char* recv_buf = (char*)rt_mp_alloc(tb22_sock->recv_buf_mp, RT_WAITING_FOREVER); // 如果暂时没有足够的内存将会阻塞等待
                    RT_ASSERT(recv_buf != RT_NULL);
                    
                    /* HEX格式字符串数据长度(每个HEX占两个字节) */
                    uint32_t hex_str_len = (uint32_t)bytes_len * 2;
                    
                    /* HEX格式字符串转换二进制数据(就地转换) */
                    uint32_t data_len = util_from_hex_str(hex_str, hex_str_len, 
                        (uint8_t*)recv_buf, (uint32_t)recv_buf_len);

                    if (socket != RT_NULL)
                    {
                        /* notice the receive buffer and buffer size */
                        if (at_evt_cb_set[AT_SOCKET_EVT_RECV])
                        {
                            at_evt_cb_set[AT_SOCKET_EVT_RECV](socket, AT_SOCKET_EVT_RECV, (const char*)recv_buf, data_len);
                        }
                    }
                }
                
                if (rem_len <= 0)
                { // 数据已全部收取
                    break;
                }
            }
            
            tb22_free_at_resp(resp);
        }
    }
    
    /* 发送线程已退出通知 */
    tb22_socket_event_send(device, SET_EVENT(socket_index, TB22_EVENT_RECV_THREAD_QUIT));
}

/* 分配TB22 Socket */
static struct tb22_sock_t* tb22_alloc_socket(struct at_device *device, int device_socket, int socket_index)
{
    char name[RT_NAME_MAX] = "";
    struct tb22_sock_t* tb22_sock = 
        (struct tb22_sock_t*)rt_malloc(sizeof(struct tb22_sock_t));
    if (tb22_sock == RT_NULL)
    {
        LOG_E("no memory for tb22_sock create.");
        goto __err;
    }
    
    tb22_sock->index = socket_index;
    tb22_sock->device_socket = device_socket;
    tb22_sock->sequence = 1;
    tb22_sock->device = device;
    rt_snprintf(name, RT_NAME_MAX, "tb22_skt%d", socket_index);
    tb22_sock->recv_buf_mp = rt_mp_create(name, TB22_SOCK_RECV_PACKET_NUM, TB22_SOCK_RECV_PACKET_SIZE);
    if (tb22_sock->recv_buf_mp == RT_NULL)
    {
        LOG_E("no memory for tb22 recv memory pool create.");
        goto __err;
    }
    rt_snprintf(name, RT_NAME_MAX, "tb22_skt%d", socket_index);
    tb22_sock->recv_thread = rt_thread_create(name, tb22_sock_recv_thread_entry, (void*)tb22_sock,
                         TB22_SOCK_RECV_THREAD_STACK_SIZE, TB22_SOCK_RECV_THREAD_PRIORITY, 10);
    if (tb22_sock->recv_thread == RT_NULL)
    {
        LOG_E("no memory for tb22 recv thread create.");
        goto __err;
    }
    
    return tb22_sock;

__err:
    if (tb22_sock)
    {
        if (tb22_sock->recv_buf_mp)
        {
            rt_mp_delete(tb22_sock->recv_buf_mp);
        }
        if (tb22_sock->recv_thread)
        {
            rt_thread_delete(tb22_sock->recv_thread);
        }
        rt_free(tb22_sock);
    }
    return RT_NULL;
}

/* 释放TB22 Socket */
static void tb22_free_socket(struct tb22_sock_t* tb22_sock)
{
    if (tb22_sock->recv_buf_mp)
    {
        rt_mp_delete(tb22_sock->recv_buf_mp);
    }
    
    if (tb22_sock->recv_thread)
    {
        rt_thread_delete(tb22_sock->recv_thread);
    }
    
    rt_free(tb22_sock);
}

/* 启动TB22 Socket接收线程 */
static rt_err_t tb22_start_socket_recv(struct tb22_sock_t* tb22_sock)
{
    return rt_thread_startup(tb22_sock->recv_thread);
}

/* 停止TB22 Socket接收线程 */
static void tb22_stop_socket_recv(struct tb22_sock_t* tb22_sock)
{
    uint32_t event = 0;
    
    /* 请求Socket接收线程退出 */
    tb22_socket_event_send(tb22_sock->device, SET_EVENT(tb22_sock->index, TB22_EVENT_REQ_RECV_THREAD_QUIT));
    
    /* 等待线程退出 */
    event = SET_EVENT(tb22_sock->index, TB22_EVENT_RECV_THREAD_QUIT);
    tb22_socket_event_recv(tb22_sock->device, event, rt_tick_from_millisecond(5 * 1000), RT_EVENT_FLAG_OR);
}

/**
 * close socket by AT commands.
 *
 * @param current socket
 *
 * @return  0: close socket success
 *         -1: send AT commands error
 *         -2: wait socket event timeout
 *         -5: no memory
 */
static int tb22_socket_close(struct at_socket *socket)
{
    int result = RT_EOK;
    struct at_device *device = (struct at_device *) socket->device;
    
    if (socket->type != AT_SOCKET_TCP)
    {
        return -RT_ERROR;
    }
    
    struct tb22_sock_t *tb22_sock = (struct tb22_sock_t*)(socket->user_data);
    int socket_index = tb22_sock->index;
    int device_socket = tb22_sock->device_socket;

    at_response_t resp = tb22_alloc_at_resp(device, 0, rt_tick_from_millisecond(1000));
    RT_ASSERT(resp);
    
    /* 停止接收线程 */
    tb22_stop_socket_recv(tb22_sock);
    
    result = at_obj_exec_cmd(device->client, resp, "AT+NSOCL=%d", device_socket);

    tb22_free_at_resp(resp);
    
    /* 释放TB22 Socket */
    tb22_free_socket(tb22_sock);
    socket->user_data = (void*)socket_index;

    return result;
}

/**
 * create TCP/UDP client or server connect by AT commands.
 *
 * @param socket current socket
 * @param ip server or client IP address
 * @param port server or client port
 * @param type connect socket type(tcp, udp)
 * @param is_client connection is client
 *
 * @return   0: connect success
 *          -1: connect failed, send commands error or type error
 *          -2: wait socket event timeout
 *          -5: no memory
 */
static int tb22_socket_connect(struct at_socket *socket, char *ip, int32_t port,
    enum at_socket_type type, rt_bool_t is_client)
{
#define CONN_RETRY  2

    int i = 0;
    at_response_t resp = RT_NULL;
    int result = RT_EOK;
    int device_socket = 0;
    int socket_index = (int) socket->user_data;
    struct at_device *device = (struct at_device *) socket->device;
    struct tb22_sock_t *tb22_sock = RT_NULL;

    RT_ASSERT(ip);
    RT_ASSERT(port >= 0);

    if ( ! is_client)
    {
        return -RT_ERROR;
    }

    if (type != AT_SOCKET_TCP)
    {
        return -RT_ERROR;
    }
    
    resp = tb22_alloc_at_resp(device, 0, rt_tick_from_millisecond(1000));
    RT_ASSERT(resp);
        
    /* create socket */
    result = at_obj_exec_cmd(device->client, resp, "AT+NSOCR=STREAM,6,0,1");
    if (result < 0)
    {
        result = -RT_ERROR;
        goto __exit;
    }
    
    if (at_resp_parse_line_args(resp, 2, "%d", &device_socket) <= 0)
    {
        result = -RT_ERROR;
        goto __exit;
    }
    
    tb22_sock = tb22_alloc_socket(device, device_socket, socket_index);
    if (tb22_sock == RT_NULL)
    {
        result = -RT_ENOMEM;
        goto __exit;
    }
    socket->user_data = (void*)tb22_sock;
    
    /* 设置连接超时时间(60s) */
    resp->timeout = rt_tick_from_millisecond(60 * 1000);
    
    /* 建立TCP连接 */
    for(i = 0; i < CONN_RETRY; i++)
    {
        result = at_obj_exec_cmd(device->client, resp, "AT+NSOCO=%d,%s,%d", 
                            device_socket, ip, port);
        if (result == RT_EOK)
        {
            break;
        }

        rt_thread_mdelay(1000);
        /* 重试 */
    }

    if (result != RT_EOK)
    {
        LOG_E("%s device socket(%d) connect failed.", device->name, device_socket);
        goto __exit;
    }
    
    /* 启动Socket接收线程 */
    result = tb22_start_socket_recv(tb22_sock);
    
__exit:
    if (resp)
    {
        tb22_free_at_resp(resp);
    }
    
    if (result != RT_EOK)
    {
        if (tb22_sock)
        {
            tb22_free_socket(tb22_sock);
        }
        socket->user_data = (void*)socket_index;
    }

    return result;
}

/**
 * send data to server or client by AT commands.
 *
 * @param socket current socket
 * @param buff send buffer
 * @param bfsz send buffer size
 * @param type connect socket type(tcp, udp)
 *
 * @return >=0: the size of send success
 *          -1: send AT commands error or send data error
 *          -2: waited socket event timeout
 *          -5: no memory
 */
static int tb22_socket_send(struct at_socket *socket, const char *buff, size_t bfsz, enum at_socket_type type)
{
    uint32_t event = 0;
    int result = 0, event_result = 0;
    size_t cur_pkt_size = 0, sent_size = 0;
    struct tb22_sock_t *tb22_sock = (struct tb22_sock_t*)(socket->user_data);
    int socket_index = tb22_sock->index;
    int device_socket = tb22_sock->device_socket;
    struct at_device *device = (struct at_device *) socket->device;
    struct at_device_tb22 *tb22 = (struct at_device_tb22 *) device->user_data;
    rt_mutex_t lock = device->client->lock;
    at_response_t resp = RT_NULL;

    RT_ASSERT(buff);
    
    rt_mutex_take(lock, RT_WAITING_FOREVER);

    /* set current socket for send URC event */
    tb22->user_data = (void *) tb22_sock;

    /* clear socket send event */
    event = SET_EVENT(socket_index, TB22_EVENT_SEND_OK | TB22_EVENT_SEND_FAIL);
    tb22_socket_event_recv(device, event, 0, RT_EVENT_FLAG_OR);

    while (sent_size < bfsz)
    {
        if (bfsz - sent_size < TB22_MODULE_SEND_MAX_SIZE)
        {
            cur_pkt_size = bfsz - sent_size;
        }
        else
        {
            cur_pkt_size = TB22_MODULE_SEND_MAX_SIZE;
        }
        
        /* 数据转换成hex字符串 */
        cur_pkt_size = util_to_hex_str((const uint8_t*)buff + sent_size, cur_pkt_size, 
            at_send_buf, sizeof(at_send_buf));
        
        resp = tb22_alloc_at_resp(device, 0, rt_tick_from_millisecond(10 * 1000));
        RT_ASSERT(resp);
    
        /* send the "AT+NSOSD" commands to AT server. */
        if (at_obj_exec_cmd(device->client, resp, "AT+NSOSD=%d,%d,%s,0,%d", device_socket, (int)cur_pkt_size, at_send_buf, tb22_sock->sequence) < 0)
        {
            result = -RT_ERROR;
            goto __exit;
        }
        
        int sock = 0, len = 0;
        if (at_resp_parse_line_args(resp, 2, "%d,%d,%d", &sock, &len) <= 0)
        {
            result = -RT_ERROR;
            goto __exit;
        }
        RT_ASSERT(device_socket == sock);
        cur_pkt_size = len;
        
        tb22_free_at_resp(resp);
        resp = RT_NULL;
        
        /* 
         * 进行正常数据传输业务时，在业务数据交互过程中，若60s后未收到下行数据，
         * 则判定本次数据业务因超时而失败，再次尝试发送数据；
         * 若3次尝试均超时失败，则进入异常处理流程(由应用层选择是否重试发送)
         */
        /* waiting OK or failed result */
        event = TB22_EVENT_SEND_OK | TB22_EVENT_SEND_FAIL;
        event_result = tb22_socket_event_recv(device, event, rt_tick_from_millisecond(60 * 1000), RT_EVENT_FLAG_OR);
        if (event_result < 0)
        {
            LOG_E("%s device socket(%d) wait sned OK|FAIL timeout.", device->name, device_socket);
            result = -RT_ETIMEOUT;
            goto __exit;
        }
        /* check result */
        if (event_result & TB22_EVENT_SEND_FAIL)
        {
            LOG_E("%s device socket(%d) send failed.", device->name, device_socket);
            result = -RT_ERROR;
            goto __exit;
        }
        
        /* update sequence */
        tb22_update_sequence(socket);

        sent_size += cur_pkt_size;
    }

__exit:
    rt_mutex_release(lock);
    
    if (resp)
    {
        tb22_free_at_resp(resp);
    }

    return (result >= 0) ? sent_size : result;
}

/**
 * domain resolve by AT commands.
 *
 * @param name domain name
 * @param ip parsed IP address, it's length must be 16
 *
 * @return  0: domain resolve success
 *         -1: send AT commands error or response error
 *         -2: wait socket event timeout
 *         -5: no memory
 */
int tb22_domain_resolve(const char *name, char ip[16])
{
    #define RESOLVE_RETRY  3

    int i, result;
    struct at_device *device = RT_NULL;
    struct at_device_tb22 *tb22 = RT_NULL;
    rt_mutex_t lock = RT_NULL;

    RT_ASSERT(name);
    RT_ASSERT(ip);

    device = at_device_get_first_initialized();
    if (device == RT_NULL)
    {
        LOG_E("get first init device failed.");
        return -RT_ERROR;
    }
    
    lock = device->client->lock;

    rt_mutex_take(lock, RT_WAITING_FOREVER);
    
    /* the maximum response time is 60 seconds, but it set to 10 seconds is convenient to use. */
    at_response_t resp = tb22_alloc_at_resp(device, 0, rt_tick_from_millisecond(1000));
    RT_ASSERT(resp);

    /* clear TB22_EVENT_DOMAIN_OK */
    tb22_socket_event_recv(device, TB22_EVENT_DOMAIN_OK, 0, RT_EVENT_FLAG_OR);
    
    tb22 = (struct at_device_tb22 *) device->user_data;
    tb22->socket_data = ip;

    if (at_obj_exec_cmd(device->client, resp, "AT+MDNS=0,%s", name) != RT_EOK)
    {
        result = -RT_ERROR;
        LOG_E("at_obj_exec_cmd failed!");
        goto __exit;
    }
    
    tb22_free_at_resp(resp);
    resp = RT_NULL;

    for(i = 0; i < RESOLVE_RETRY; i++)
    {
        /* waiting result event from AT URC, the device default connection timeout is 30 seconds.*/
        if (tb22_socket_event_recv(device, TB22_EVENT_DOMAIN_OK, rt_tick_from_millisecond(10 * 1000), RT_EVENT_FLAG_OR) < 0)
        {
            result = -RT_ETIMEOUT;
            continue;
        }
        else
        {
            if (rt_strlen(ip) < 8)
            {
                rt_thread_mdelay(100);
                /* resolve failed, maybe receive an URC CRLF */
                result = -RT_ERROR;
                continue;
            }
            else
            {
                result = RT_EOK;
                break;
            }
        }
    }

 __exit:
    tb22->socket_data = RT_NULL;
    if (resp)
    {
        tb22_free_at_resp(resp);
    }
    
    rt_mutex_release(lock);

    return result;

}

/**
 * set AT socket event notice callback
 *
 * @param event notice event
 * @param cb notice callback
 */
static void tb22_socket_set_event_cb(at_socket_evt_t event, at_evt_cb_t cb)
{
    if (event < sizeof(at_evt_cb_set) / sizeof(at_evt_cb_set[1]))
    {
        at_evt_cb_set[event] = cb;
    }
}

static void urc_send_func(struct at_client *client, const char *data, rt_size_t size)
{
    int device_socket = 0, sequence = 0, status = 0;
    int socket_index = 0;
    struct at_device *device = RT_NULL;
    struct at_device_tb22 *tb22 = RT_NULL;
    struct tb22_sock_t *tb22_sock = RT_NULL;
    char *client_name = client->device->parent.name;

    RT_ASSERT(data && size);
    
    LOG_D("urc_send_func()");

    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_CLIENT, client_name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", client_name);
        return;
    }
    
    tb22 = (struct at_device_tb22 *) device->user_data;
    tb22_sock = (struct tb22_sock_t*)(tb22->user_data);
    socket_index = tb22_sock->index;

    sscanf(data, "+NSOSTR:%d,%d,%d", &device_socket, &sequence, &status);
    if (device_socket == tb22_sock->device_socket)
    {
        if (1 == status)
        {
            tb22_socket_event_send(device, SET_EVENT(socket_index, TB22_EVENT_SEND_OK));
        }
        else if (0 == status)
        {
            tb22_socket_event_send(device, SET_EVENT(socket_index, TB22_EVENT_SEND_FAIL));
        }
    }
}

static void urc_close_func(struct at_client *client, const char *data, rt_size_t size)
{
    int device_socket = 0;
    struct at_socket *socket = RT_NULL;
    struct at_device *device = RT_NULL;
    char *client_name = client->device->parent.name;

    RT_ASSERT(data && size);
    
    LOG_D("urc_close_func()");

    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_CLIENT, client_name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", client_name);
        return;
    }

    sscanf(data, "+NSOCLI:%d", &device_socket);
    
    /* get at socket object by device socket descriptor */
    socket = at_get_socket_by_device_socket(device, device_socket);
    if (socket != RT_NULL)
    {
        /* notice the socket is disconnect by remote */
        if (at_evt_cb_set[AT_SOCKET_EVT_CLOSED])
        {
            at_evt_cb_set[AT_SOCKET_EVT_CLOSED](socket, AT_SOCKET_EVT_CLOSED, NULL, 0);
        }
    }
}

static void urc_recv_func(struct at_client *client, const char *data, rt_size_t size)
{
    int device_socket = 0;
    rt_size_t data_len = 0;
    struct at_device *device = RT_NULL;
    struct at_device_tb22 *tb22 = RT_NULL;
    struct tb22_sock_t *tb22_sock = RT_NULL;
    char *client_name = client->device->parent.name;

    RT_ASSERT(data && size);

    LOG_D("urc_recv_func()");
    
    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_CLIENT, client_name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", client_name);
        return;
    }

    tb22 = (struct at_device_tb22 *)device->user_data;
    tb22_sock = (struct tb22_sock_t*)(tb22->user_data);
    
    /* get the current socket and receive buffer size by receive data */
    sscanf(data, "+NSONMI:%d,%d", &device_socket, (int*)&data_len);

    if ((device_socket < 0) 
        || (data_len <= 0))
    {
        LOG_E("invalid device_socket(%d) or data_len(%d).", device_socket, data_len);
        return;
    }
    
    /* 请求Socket接收线程收取数据 */
    tb22_socket_event_send(tb22_sock->device, SET_EVENT(tb22_sock->index, TB22_EVENT_REQ_RECV_DATA));
}

static void urc_dnsqip_func(struct at_client *client, const char *data, rt_size_t size)
{
    char recv_ip[16] = {0};
    struct at_device *device = RT_NULL;
    struct at_device_tb22 *tb22 = RT_NULL;
    char *client_name = client->device->parent.name;

    RT_ASSERT(data && size);
    
    LOG_D("urc_dnsqip_func()");

    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_CLIENT, client_name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", client_name);
        return;
    }
    
    tb22 = (struct at_device_tb22 *) device->user_data;
    if (tb22->socket_data == RT_NULL)
    {
        LOG_D("%s device socket_data no config.", tb22->device_name);
        return;
    }

    sscanf(data, "+MDNS:%s", recv_ip);
    recv_ip[15] = '\0';

    rt_memcpy(tb22->socket_data, recv_ip, sizeof(recv_ip));

    tb22_socket_event_send(device, TB22_EVENT_DOMAIN_OK);
}

static const struct at_urc urc_table[] =
{
    {"+NSOSTR:",    "\r\n",                 urc_send_func},
    {"+NSOCLI",     "\r\n",                 urc_close_func},
    {"+NSONMI",     "\r\n",                 urc_recv_func},
    {"+MDNS:",      "\r\n",                 urc_dnsqip_func},
};

static const struct at_socket_ops tb22_socket_ops =
{
    tb22_socket_connect,
    tb22_socket_close,
    tb22_socket_send,
    tb22_domain_resolve,
    tb22_socket_set_event_cb,
};

int tb22_socket_init(struct at_device *device)
{
    RT_ASSERT(device);

    /* register URC data execution function  */
    at_obj_set_urc_table(device->client, urc_table, sizeof(urc_table) / sizeof(urc_table[0]));

    return RT_EOK;
}

int tb22_socket_class_register(struct at_device_class *class)
{
    RT_ASSERT(class);

    class->socket_num = AT_DEVICE_TB22_SOCKETS_NUM;
    class->socket_ops = &tb22_socket_ops;

    return RT_EOK;
}

#endif /* AT_DEVICE_USING_TB22 && AT_USING_SOCKET */

