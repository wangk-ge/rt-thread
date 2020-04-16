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

#include <at_device_tb22.h>

#define LOG_TAG                        "at.skt.tb22"
#include <at_log.h>

#if defined(AT_DEVICE_USING_TB22) && defined(AT_USING_SOCKET)

#define TB22_MODULE_SEND_MAX_SIZE       1358 // AT+NSOSD最大传输1358字节

/* set real event by current socket and current state */
#define SET_EVENT(socket_index, event)       (((socket_index + 1) << 16) | (event))

#if !defined(MIN)
#define MIN(a,b)    (((a) < (b)) ? (a) : (b))
#endif

/* 半字节转换为HEX字符 */
#define TO_HEX_CHAR(b) (((b) <= 0x09) ? ((b) + (uint8_t)'0') : (((b) - 0x0A) + (uint8_t)'A'))

/* AT socket event type */
#define TB22_EVENT_CONN_OK             (1L << 0)
#define TB22_EVENT_SEND_OK             (1L << 1)
#define TB22_EVENT_RECV_OK             (1L << 2)
#define TB22_EVNET_CLOSE_OK            (1L << 3)
#define TB22_EVENT_CONN_FAIL           (1L << 4)
#define TB22_EVENT_SEND_FAIL           (1L << 5)
#define TB22_EVENT_DOMAIN_OK           (1L << 6)

/* 发送数据缓冲区大小(字节数) */
#define TB22_SEND_BUF_SIZE             (TB22_MODULE_SEND_MAX_SIZE * 2)

struct tb22_sock_t {
    int index;
    int device_socket;
};

static at_evt_cb_t at_evt_cb_set[] = {
        [AT_SOCKET_EVT_RECV] = NULL,
        [AT_SOCKET_EVT_CLOSED] = NULL,
};

/* 发送数据缓冲区 */
static char at_send_buf[TB22_SEND_BUF_SIZE] = "";

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
    at_response_t resp = RT_NULL;
    struct tb22_sock_t *tb22_sock = (struct tb22_sock_t*)(socket->user_data);
    int socket_index = tb22_sock->index;
    int device_socket = tb22_sock->device_socket;
    struct at_device *device = (struct at_device *) socket->device;

    resp = at_create_resp(64, 0, rt_tick_from_millisecond(300));
    if (resp == RT_NULL)
    {
        LOG_E("no memory for resp create.");
        return -RT_ENOMEM;
    }
    
    result = at_obj_exec_cmd(device->client, resp, "AT+NSOCL=%d", device_socket);

    at_delete_resp(resp);
    
    rt_free(socket->user_data);
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
    int result = 0;
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

    resp = at_create_resp(128, 0, rt_tick_from_millisecond(300));
    if (resp == RT_NULL)
    {
        LOG_E("no memory for resp create.");
        return -RT_ENOMEM;
    }
        
    /* create socket */
    result = at_obj_exec_cmd(device->client, resp, "AT+NSOCR=STREAM,6,0,1");
    if (result < 0)
    {
        return -RT_ERROR;
    }
    
    if (at_resp_parse_line_args(resp, 1, "%d", &device_socket) <= 0)
    {
        return -RT_ERROR;
    }
    
    tb22_sock = (struct tb22_sock_t*)rt_malloc(sizeof(struct tb22_sock_t));
    if (tb22_sock == RT_NULL)
    {
        return -RT_ENOMEM;
    }
    tb22_sock->index = socket_index;
    tb22_sock->device_socket = device_socket;
    socket->user_data = (void*)tb22_sock;
    
    /* 设置连接超时时间(60s) */
    resp->timeout = rt_tick_from_millisecond(60 * 1000);
    
    /* 建立TCP连接 */
    for(i = 0; i<CONN_RETRY; i++)
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

    if (i == CONN_RETRY)
    {
        LOG_E("%s device socket(%d) connect failed.", device->name, device_socket);
        
        rt_free(socket->user_data);
        socket->user_data = (void*)socket_index;
        
        result = -RT_ERROR;
    }
    
    if (resp)
    {
        at_delete_resp(resp);
    }

    return result;
}

/**
 * convert data to hex string
 *
 * @param data data to convert
 * @param data_len data length (in bytes)
 * @param hex_str_buf buffer to hex string
 * @param buf_len buffer length (in bytes)
 *
 * @return the data length of convert success
 */
static uint32_t tb22_to_hex_str(const uint8_t* data, size_t data_len, 
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

/**
 * convert hex char to byte data(Example: '0'->0x00,'A'->0x0A)
 *
 * @param hex_char hex char
 *
 * @return the byte data
 */
rt_inline uint8_t tb22_hex_char_to_byte(char hex_char)
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

/**
 * convert hex string to byte data(can be convert in place)
 *
 * @param hex_str hex string to convert
 * @param str_len string length (in bytes)
 * @param data_buf buffer to byte data
 * @param buf_len buffer length (in bytes)
 *
 * @return the byte data length of convert success
 */
static uint32_t tb22_from_hex_str(const char* hex_str, size_t str_len, 
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
        uint8_t high_byte = tb22_hex_char_to_byte(hight_char);
        uint8_t low_byte = tb22_hex_char_to_byte(low_char);
        data_buf[i] = (high_byte << 4) | low_byte;
    }
    
    return convert_len;
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
    at_response_t resp = RT_NULL;
    struct tb22_sock_t *tb22_sock = (struct tb22_sock_t*)(socket->user_data);
    int socket_index = tb22_sock->index;
    int device_socket = tb22_sock->device_socket;
    struct at_device *device = (struct at_device *) socket->device;
    struct at_device_tb22 *tb22 = (struct at_device_tb22 *) device->user_data;
    rt_mutex_t lock = device->client->lock;

    RT_ASSERT(buff);

    resp = at_create_resp(128, 2, rt_tick_from_millisecond(300));
    if (resp == RT_NULL)
    {
        LOG_E("no memory for resp create.");
        return -RT_ENOMEM;
    }

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
        cur_pkt_size = tb22_to_hex_str((const uint8_t*)buff + sent_size, cur_pkt_size, at_send_buf, sizeof(at_send_buf));
    
        /* send the "AT+NSOSD" commands to AT server. */
        if (at_obj_exec_cmd(device->client, resp, "AT+NSOSD=%d,%d,%s", device_socket, (int)cur_pkt_size, at_send_buf) < 0)
        {
            result = -RT_ERROR;
            goto __exit;
        }
        
        int sock = 0, len = 0;
        if (at_resp_parse_line_args(resp, 1, "%d,%d,%d", &sock, &len) <= 0)
        {
            result = -RT_ERROR;
            goto __exit;
        }
        RT_ASSERT(device_socket == sock);
        cur_pkt_size = len;
        
        /* waiting OK or failed result */
        event = TB22_EVENT_SEND_OK | TB22_EVENT_SEND_FAIL;
        event_result = tb22_socket_event_recv(device, event, 1 * RT_TICK_PER_SECOND, RT_EVENT_FLAG_OR);
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

        sent_size += cur_pkt_size;
    }

__exit:
    rt_mutex_release(lock);

    if (resp)
    {
        at_delete_resp(resp);
    }

    return result > 0 ? sent_size : result;
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
    at_response_t resp = RT_NULL;
    struct at_device *device = RT_NULL;
    struct at_device_tb22 *tb22 = RT_NULL;

    RT_ASSERT(name);
    RT_ASSERT(ip);

    device = at_device_get_first_initialized();
    if (device == RT_NULL)
    {
        LOG_E("get first init device failed.");
        return -RT_ERROR;
    }

    /* the maximum response time is 60 seconds, but it set to 10 seconds is convenient to use. */
    resp = at_create_resp(128, 0, rt_tick_from_millisecond(300));
    if (!resp)
    {
        LOG_E("no memory for resp create.");
        return -RT_ENOMEM;
    }

    /* clear TB22_EVENT_DOMAIN_OK */
    tb22_socket_event_recv(device, TB22_EVENT_DOMAIN_OK, 0, RT_EVENT_FLAG_OR);
    
    tb22 = (struct at_device_tb22 *) device->user_data;
    tb22->socket_data = ip;

    if (at_obj_exec_cmd(device->client, resp, "AT+QDNS=0,%s", name) != RT_EOK)
    {
        result = -RT_ERROR;
        goto __exit;
    }

    for(i = 0; i < RESOLVE_RETRY; i++)
    {
        /* waiting result event from AT URC, the device default connection timeout is 30 seconds.*/
        if (tb22_socket_event_recv(device, TB22_EVENT_DOMAIN_OK, 10 * RT_TICK_PER_SECOND, RT_EVENT_FLAG_OR) < 0)
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
        at_delete_resp(resp);
    }

    return result;

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
    rt_size_t bfsz = 0;
    char *recv_buf = RT_NULL;
    struct at_socket *socket = RT_NULL;
    struct at_device *device = RT_NULL;
    char *client_name = client->device->parent.name;
    at_response_t resp = RT_NULL;

    RT_ASSERT(data && size);

    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_CLIENT, client_name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", client_name);
        return;
    }

    /* get the current socket and receive buffer size by receive data */
    sscanf(data, "+NSONMI:%d,%d", &device_socket, (int*)&bfsz);

    if (device_socket < 0 || bfsz == 0)
    {
        return;
    }
    
    /* get at socket object by device socket descriptor */
    socket = at_get_socket_by_device_socket(device, device_socket);
    if (socket == RT_NULL)
    {
        LOG_E("get at socket object by device socket descriptor(%d) failed.", device_socket);
        return;
    }

    int rem_len = bfsz; // 剩余数据长度
    while (rem_len > 0)
    { // 收取所有数据
        recv_buf = (char *) rt_calloc(1, rem_len);
        if (recv_buf == RT_NULL)
        {
            LOG_E("no memory for URC receive buffer(%d).", rem_len);
            /* read and clean the coming data */
            at_obj_exec_cmd(device->client, RT_NULL, "AT+NSORF=%d,%d", device_socket, rem_len);
            break;
        }

        resp = at_create_resp((rem_len * 2) + 64, 0, rt_tick_from_millisecond(300));
        if (recv_buf == RT_NULL)
        {
            LOG_E("no memory for resp create.");
            /* read and clean the coming data */
            at_obj_exec_cmd(device->client, RT_NULL, "AT+NSORF=%d,%d", device_socket, rem_len);
            break;
        }
    
        if (at_obj_exec_cmd(device->client, resp, "AT+NSORF=%d,%d", device_socket, rem_len) != RT_EOK)
        {
            /* read and clean the coming data */
            at_obj_exec_cmd(device->client, RT_NULL, "AT+NSORF=%d,%d", device_socket, rem_len);
            break;
        }
        
        int sock = 0, port = 0, len = 0;
        char ip[32] = "";
        if (at_resp_parse_line_args(resp, 1, "%d,%s,%d,%d,%s,%d", &sock, ip, &port, &len, recv_buf, &rem_len) <= 0)
        {
            break;
        }
        RT_ASSERT(device_socket == sock);
        if (len <= 0)
        {
            break;
        }
        
        at_delete_resp(resp);
        resp = RT_NULL;
        
        /* Hex字符串转换二进制数据(就地转换) */
        uint8_t *data_buf = (uint8_t*)recv_buf;
        uint32_t buf_len = (uint32_t)bfsz;
        uint32_t data_len = tb22_from_hex_str(recv_buf, (uint32_t)len, data_buf, buf_len);

        if (socket != RT_NULL)
        {
            /* notice the receive buffer and buffer size */
            if (at_evt_cb_set[AT_SOCKET_EVT_RECV])
            {
                at_evt_cb_set[AT_SOCKET_EVT_RECV](socket, AT_SOCKET_EVT_RECV, (const char*)data_buf, data_len);
            }
        }
    }
    
    if (resp != RT_NULL)
    {
        at_delete_resp(resp);
    }
}

static void urc_dnsqip_func(struct at_client *client, const char *data, rt_size_t size)
{
    char recv_ip[16] = {0};
    struct at_device *device = RT_NULL;
    struct at_device_tb22 *tb22 = RT_NULL;
    char *client_name = client->device->parent.name;

    RT_ASSERT(data && size);

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

    sscanf(data, "+QDNS:%s", recv_ip);
    recv_ip[15] = '\0';

    rt_memcpy(tb22->socket_data, recv_ip, sizeof(recv_ip));

    tb22_socket_event_send(device, TB22_EVENT_DOMAIN_OK);
}

static const struct at_urc urc_table[] =
{
    {"+NSOSTR:",    "\r\n",                 urc_send_func},
    {"+NSOCLI",     "\r\n",                 urc_close_func},
    {"+NSONMI",     "\r\n",                 urc_recv_func},
    {"+QDNS:",      "\r\n",                 urc_dnsqip_func},
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

