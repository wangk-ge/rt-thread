 /*
 * File      : at_device_tb22.c
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
 * 2020-02-12     luhuadong         first version
 */

#include <stdio.h>
#include <string.h>
#include <drv_gpio.h>

#include <at_device_tb22.h>

#define LOG_TAG                        "at.dev.tb22"
#include <at_log.h>

#ifdef AT_DEVICE_USING_TB22

#define TB22_WAIT_CONNECT_TIME          5000
#define TB22_THREAD_STACK_SIZE          2048
#define TB22_THREAD_PRIORITY            (RT_THREAD_PRIORITY_MAX/2)
#define TB22_NET_LED_PIN                GET_PIN(D, 14) // 网络连接状态指示LED控制GPIO(低电平点亮)

#define TB22_AT_RESP_NUM           10  // at resp个数(用于内存池分配)
#define TB22_AT_RESP_SIZE          256 // at resp大小(用于内存池分配)

static int tb22_reset(struct at_device *device)
{
    struct at_device_tb22 *tb22 = (struct at_device_tb22 *)device->user_data;

    if (tb22->power_pin != -1)
    {
        rt_pin_mode(tb22->power_pin, PIN_MODE_OUTPUT);
        rt_pin_write(tb22->power_pin, PIN_LOW);
        rt_thread_mdelay(300);
        rt_pin_write(tb22->power_pin, PIN_HIGH);
        tb22->power_status = 1;
    }

    return(RT_EOK);
}

static int tb22_check_link_status(struct at_device *device)
{
    struct at_device_tb22 *tb22 = RT_NULL;
    int result = -RT_ERROR;
    
    tb22 = (struct at_device_tb22 *)device->user_data;
    if ( ! tb22->power_status) // power off
    {
        LOG_E("the power is off.");
        return(-RT_ERROR);
    }
    if (tb22->sleep_status)    // is sleep status
    {
        if (tb22->power_pin != -1)
        {
            rt_pin_write(tb22->power_pin, PIN_LOW);
            rt_thread_mdelay(100);
            rt_pin_write(tb22->power_pin, PIN_HIGH);
            rt_thread_mdelay(200);
        }
    }
	
	at_response_t resp = tb22_alloc_at_resp(device, 0, rt_tick_from_millisecond(1000));
    RT_ASSERT(resp);

    result = -RT_ERROR;
    if (at_obj_exec_cmd(device->client, resp, "AT+CGATT?") == RT_EOK)
    {
        int link_stat = 0;
        if (at_resp_parse_line_args_by_kw(resp, "+CGATT:", "+CGATT:%d", &link_stat) > 0)
        {
            if (link_stat == 1)
            {
                result = RT_EOK;
            }
        }
    }

    tb22_free_at_resp(resp);
	
    return(result);
}

/* =============================  tb22 network interface operations ============================= */
/* set tb22 network interface device status and address information */
static int tb22_netdev_set_info(struct netdev *netdev)
{
#define TB22_INFO_RESP_SIZE        128
#define TB22_INFO_RESP_TIMOUT      rt_tick_from_millisecond(5000)

    int result = RT_EOK;
    ip_addr_t addr;
    at_response_t resp = RT_NULL;
    struct at_device *device = RT_NULL;

    RT_ASSERT(netdev);

    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_NETDEV, netdev->name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", netdev->name);
        return -RT_ERROR;
    }

    /* set network interface device status */
    netdev_low_level_set_status(netdev, RT_TRUE);
    netdev_low_level_set_link_status(netdev, RT_TRUE);
    netdev_low_level_set_dhcp_status(netdev, RT_TRUE);

	resp = tb22_alloc_at_resp(device, 0, TB22_INFO_RESP_TIMOUT);
    RT_ASSERT(resp);

    /* set network interface device hardware address(IMEI) */
    {
        #define TB22_NETDEV_HWADDR_LEN   8
        #define TB22_IMEI_LEN            15

        char imei[TB22_IMEI_LEN] = {0};
        int i = 0, j = 0;

        /* send "AT+CGSN=1" commond to get device IMEI */
        if (at_obj_exec_cmd(device->client, resp, "AT+CGSN=1") != RT_EOK)
        {
            result = -RT_ERROR;
            goto __exit;
        }
        
        if (at_resp_parse_line_args(resp, 2, "+CGSN:%s", imei) <= 0)
        {
            LOG_E("%s device prase \"AT+CGSN=1\" cmd error.", device->name);
            result = -RT_ERROR;
            goto __exit;
        }
        
        LOG_D("%s device IMEI number: %s", device->name, imei);

        netdev->hwaddr_len = TB22_NETDEV_HWADDR_LEN;
        /* get hardware address by IMEI */
        for (i = 0, j = 0; i < TB22_NETDEV_HWADDR_LEN && j < TB22_IMEI_LEN; i++, j+=2)
        {
            if (j != TB22_IMEI_LEN - 1)
            {
                netdev->hwaddr[i] = (imei[j] - '0') * 10 + (imei[j + 1] - '0');
            }
            else
            {
                netdev->hwaddr[i] = (imei[j] - '0');
            }
        }
    }

    /* set network interface device IP address */
    {
        #define IP_ADDR_SIZE_MAX    16
        char ipaddr[IP_ADDR_SIZE_MAX] = {0};
        
        /* send "AT+CGPADDR" commond to get IP address */
        if (at_obj_exec_cmd(device->client, resp, "AT+CGPADDR") != RT_EOK)
        {
            result = -RT_ERROR;
            goto __exit;
        }

        /* parse response data "+CGPADDR: 0,<IP_address>" */
        if (at_resp_parse_line_args_by_kw(resp, "+CGPADDR:", "+CGPADDR:%*d,%s", ipaddr) <= 0)
        {
            LOG_E("%s device \"AT+CGPADDR\" cmd error.", device->name);
            result = -RT_ERROR;
            goto __exit;
        }

        LOG_D("%s device IP address: %s", device->name, ipaddr);

        /* set network interface address information */
        inet_aton(ipaddr, &addr);
        netdev_low_level_set_ipaddr(netdev, &addr);
    }

    /* set network interface device dns server */
#if 0
    {
        #define DNS_ADDR_SIZE_MAX   16
        char dns_server1[DNS_ADDR_SIZE_MAX] = {0}, dns_server2[DNS_ADDR_SIZE_MAX] = {0};
        
        /* send "AT+QIDNSCFG?" commond to get DNS servers address */
        if (at_obj_exec_cmd(device->client, resp, "AT+QIDNSCFG?") != RT_EOK)
        {
            result = -RT_ERROR;
            goto __exit;
        }

        /* parse response data "PrimaryDns:<pri_dns>" 
         *                     "SecondaryDns:<sec_dns>" 
        */
        if (at_resp_parse_line_args_by_kw(resp, "PrimaryDns:", "PrimaryDns: %s", dns_server1) <= 0)
        {
            LOG_E("%s device prase \"AT+QIDNSCFG?\" cmd error.", device->name);
            result = -RT_ERROR;
            goto __exit;
        }
        if (at_resp_parse_line_args_by_kw(resp, "SecondaryDns:", "SecondaryDns: %s", dns_server2) <= 0)
        {
            LOG_E("%s device prase \"AT+QIDNSCFG?\" cmd error.", device->name);
            result = -RT_ERROR;
            goto __exit;
        }

        LOG_D("%s device primary DNS server address: %s", device->name, dns_server1);
        LOG_D("%s device secondary DNS server address: %s", device->name, dns_server2);

        inet_aton(dns_server1, &addr);
        netdev_low_level_set_dns_server(netdev, 0, &addr);

        inet_aton(dns_server2, &addr);
        netdev_low_level_set_dns_server(netdev, 1, &addr);
    }
#endif

__exit:
    if (resp)
    {
        tb22_free_at_resp(resp);
    }

    return result;
}

static void tb22_check_link_status_entry(void *parameter)
{
#define TB22_LINK_DELAY_TIME    (60 * RT_TICK_PER_SECOND)

    rt_bool_t is_link_up;
    struct at_device *device = RT_NULL;
    struct netdev *netdev = (struct netdev *) parameter;

    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_NETDEV, netdev->name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", netdev->name);
        return;
    }
    
    while (1)
    {
        is_link_up = (tb22_check_link_status(device) == RT_EOK);

        netdev_low_level_set_link_status(netdev, is_link_up);
        
        /* 控制连接指示灯(低电平点亮) */
        rt_pin_write(TB22_NET_LED_PIN, is_link_up ? PIN_LOW : PIN_HIGH);

        rt_thread_delay(TB22_LINK_DELAY_TIME);
    }
}

static int tb22_netdev_check_link_status(struct netdev *netdev)
{
#define TB22_LINK_THREAD_STACK_SIZE     (1024 + 512)
#define TB22_LINK_THREAD_PRIORITY       (RT_THREAD_PRIORITY_MAX - 2)

    rt_thread_t tid;
    char tname[RT_NAME_MAX] = {0};

    RT_ASSERT(netdev);

    rt_snprintf(tname, RT_NAME_MAX, "%s", netdev->name);

    /* create tb22 link status polling thread  */
    tid = rt_thread_create(tname, tb22_check_link_status_entry, (void *)netdev,
                           TB22_LINK_THREAD_STACK_SIZE, TB22_LINK_THREAD_PRIORITY, 20);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }

    return RT_EOK;
}

static int tb22_net_init(struct at_device *device);

static int tb22_netdev_set_up(struct netdev *netdev)
{
    struct at_device *device = RT_NULL;

    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_NETDEV, netdev->name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", netdev->name);
        return -RT_ERROR;
    }

    if (device->is_init == RT_FALSE)
    {
        tb22_net_init(device);
        device->is_init = RT_TRUE;

        netdev_low_level_set_status(netdev, RT_TRUE);
        LOG_D("network interface device(%s) set up status.", netdev->name);
    }

    return RT_EOK;
}

static int tb22_netdev_set_down(struct netdev *netdev)
{
    struct at_device *device = RT_NULL;

    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_NETDEV, netdev->name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", netdev->name);
        return -RT_ERROR;
    }

    if (device->is_init == RT_TRUE)
    {
        //tb22_power_off(device);
        device->is_init = RT_FALSE;

        netdev_low_level_set_status(netdev, RT_FALSE);
        LOG_D("network interface device(%s) set down status.", netdev->name);
    }

    return RT_EOK;
}

static int tb22_netdev_set_dns_server(struct netdev *netdev, uint8_t dns_num, ip_addr_t *dns_server)
{
#define TB22_DNS_RESP_LEN      64
#define TB22_DNS_RESP_TIMEOUT  rt_tick_from_millisecond(300)

    int result = RT_EOK;
    at_response_t resp = RT_NULL;
    struct at_device *device = RT_NULL;

    RT_ASSERT(netdev);
    RT_ASSERT(dns_server);

    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_NETDEV, netdev->name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", netdev->name);
        return -RT_ERROR;
    }
	
	resp = tb22_alloc_at_resp(device, 0, TB22_DNS_RESP_TIMEOUT);
    RT_ASSERT(resp);

    /* send "AT+QIDNSCFG=<pri_dns>[,<sec_dns>]" commond to set dns servers */
    if (at_obj_exec_cmd(device->client, resp, "AT+QIDNSCFG=%d,%s", 
        dns_num, inet_ntoa(*dns_server)) != RT_EOK)
    {
        result = -RT_ERROR;
        goto __exit;
    }

    netdev_low_level_set_dns_server(netdev, dns_num, dns_server);

__exit:
    if (resp)
    {
        tb22_free_at_resp(resp);
    }

    return result;
}

#ifdef AT_USING_SOCKET
    int tb22_domain_resolve(const char *name, char ip[16]);
#endif
#ifdef RT_USING_FINSH
#ifdef NETDEV_USING_PING
static int tb22_netdev_ping(struct netdev *netdev, const char *host,
        size_t data_len, uint32_t timeout, struct netdev_ping_resp *ping_resp)
{
#define TB22_PING_RESP_SIZE       128
#define TB22_PING_IP_SIZE         16
#define TB22_PING_TIMEOUT         (10 * RT_TICK_PER_SECOND)

    rt_err_t result = RT_EOK;
    int response = -1, ping_time, ttl;
    char ip_addr[TB22_PING_IP_SIZE] = {0};
    at_response_t resp = RT_NULL;
    struct at_device *device = RT_NULL;

    RT_ASSERT(netdev);
    RT_ASSERT(host);
    RT_ASSERT(ping_resp);

    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_NETDEV, netdev->name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", netdev->name);
        return -RT_ERROR;
    }

	resp = tb22_alloc_at_resp(device, 4, TB22_PING_TIMEOUT);
    RT_ASSERT(resp);

    /* DNS resolve */
    struct in_addr inp;
    if (inet_aton(host, &inp) > 0)
    {
        rt_strncpy(ip_addr, host, TB22_PING_IP_SIZE);
    }
#ifdef AT_USING_SOCKET
    else
    {
        tb22_domain_resolve(host, ip_addr);
    }
#endif
    
    if (at_obj_exec_cmd(device->client, resp, "AT+NPING=%s,%d,%d", 
                        ip_addr, data_len, timeout*1000/RT_TICK_PER_SECOND) < 0)
    {
        result = -RT_ERROR;
        goto __exit;
    }

    if (at_resp_parse_line_args_by_kw(resp, "+NPINGERR:", "+NPINGERR:%d", &response) > 0)
    {
        switch (response)
        {
        case 1:
            result = -RT_ETIMEOUT;
            break;
        case 2:
        default:
            result = -RT_ERROR;
            break;
        }
    }
    else if (at_resp_parse_line_args_by_kw(resp, "+NPING:", "+NPING:%[^,],%d,%d",
                                            ip_addr, &ttl, &ping_time) > 0 )
    {
        inet_aton(ip_addr, &(ping_resp->ip_addr));
        ping_resp->data_len = data_len;
        ping_resp->ticks = rt_tick_from_millisecond(ping_time);
        ping_resp->ttl = ttl;
        result = RT_EOK;
    }
    else
    {
        result = -RT_ERROR;
        goto __exit;
    }

__exit:
    if (resp)
    {
        tb22_free_at_resp(resp);
    }

    return result;
}
#endif /* NETDEV_USING_PING */
#endif

const struct netdev_ops tb22_netdev_ops =
{
    tb22_netdev_set_up,
    tb22_netdev_set_down,

    RT_NULL,
    tb22_netdev_set_dns_server,
    RT_NULL,

#ifdef RT_USING_FINSH
#ifdef NETDEV_USING_PING
    tb22_netdev_ping,
#endif
    RT_NULL,
#endif
    
    RT_NULL,
};

static struct netdev *tb22_netdev_add(const char *netdev_name)
{
#define ETHERNET_MTU        1500
#define HWADDR_LEN          8
    struct netdev *netdev = RT_NULL;

    netdev = (struct netdev *)rt_calloc(1, sizeof(struct netdev));
    if (netdev == RT_NULL)
    {
        LOG_E("no memory for netdev create.");
        return RT_NULL;
    }

    netdev->mtu = ETHERNET_MTU;
    netdev->ops = &tb22_netdev_ops;
    netdev->hwaddr_len = HWADDR_LEN;

#ifdef SAL_USING_AT
    extern int sal_at_netdev_set_pf_info(struct netdev *netdev);
    /* set the network interface socket/netdb operations */
    sal_at_netdev_set_pf_info(netdev);
#endif

    netdev_register(netdev, netdev_name, RT_NULL);

    return netdev;
}

/* =============================  tb22 device operations ============================= */

/* initialize for tb22 */
static void tb22_init_thread_entry(void *parameter)
{
#define INIT_RETRY                     5
#define CPIN_RETRY                     5
#define CSQ_RETRY                      20
#define CGREG_RETRY                    300 // 手册建议开机入网超时时间不小于300s
#define IPADDR_RETRY                   10
#define AT_DEFAULT_TIMEOUT             5000

    int i;
    int retry_num = INIT_RETRY;
    rt_err_t result = RT_EOK;
    at_response_t resp = RT_NULL;
    struct at_device *device = (struct at_device *) parameter;
    struct at_client *client = device->client;

    LOG_D("start init %s device.", device->name);
	
	resp = tb22_alloc_at_resp(device, 0, rt_tick_from_millisecond(AT_DEFAULT_TIMEOUT));
	RT_ASSERT(resp);

    while (retry_num--)
    {
        /* reset the tb22 device */
        tb22_reset(device);
        rt_thread_mdelay(1000);

        /* wait tb22 startup finish, send AT every 500ms, if receive OK, SYNC success*/
        if (at_client_obj_wait_connect(client, TB22_WAIT_CONNECT_TIME))
        {
            result = -RT_ETIMEOUT;
            goto __exit;
        }
        
		
		
        /* disable echo */
        if (at_obj_exec_cmd(device->client, resp, "ATE0") != RT_EOK)
        {
            result = -RT_ERROR;
            LOG_E("ATE0");
            goto __exit;
        }
        
        /* set min function */
        if (at_obj_exec_cmd(device->client, resp, "AT+CFUN=0") != RT_EOK)
        {
            result = -RT_ERROR;
            LOG_E(">> AT+CFUN=0");
            goto __exit;
        }

#if 0
        /* disable auto register(禁用IoT平台的注册功能) */
        if (at_obj_exec_cmd(device->client, resp, "AT+QREGSWT=2") != RT_EOK)
        {
            result = -RT_ERROR;
            LOG_E(">> AT+QREGSWT=2");
            goto __exit;
        }
#endif
        
        /* disable auto connect */
        if (at_obj_exec_cmd(device->client, resp, "AT+NCONFIG=AUTOCONNECT,FALSE") != RT_EOK)
        {
            result = -RT_ERROR;
            LOG_E(">> AT+NCONFIG=AUTOCONNECT,FALSE");
            goto __exit;
        }
        
        /* ??? */
        if (at_obj_exec_cmd(device->client, resp, "AT+NCONFIG=PCO_IE_TYPE,PCO") != RT_EOK)
        {
            result = -RT_ERROR;
            LOG_E(">> AT+NCONFIG=PCO_IE_TYPE,PCO");
            goto __exit;
        }
        
#if 0
        /* 输出配置信息 */
        /* AT+NCONFIG? */
        at_obj_exec_cmd(device->client, resp, "AT+NCONFIG?");
#endif
        
        /* reboot */
        at_obj_exec_cmd(device->client, resp, "AT+NRB");
        rt_thread_mdelay(5000);

        while (at_obj_exec_cmd(device->client, resp, "AT") != RT_EOK)
        {
            rt_thread_mdelay(1000);
        }
        
        /* set min function */
        if (at_obj_exec_cmd(device->client, resp, "AT+CFUN=0") != RT_EOK)
        {
            result = -RT_ERROR;
            LOG_E(">> AT+CFUN=0");
            goto __exit;
        }
        
        /* search band 8 */
        if (at_obj_exec_cmd(device->client, resp, "AT+NBAND=8") != RT_EOK)
        {
            result = -RT_ERROR;
            LOG_E(">> AT+NBAND=8");
            goto __exit;
        }
        
        /* 初次驻网不清除先验频点 */
        if ((retry_num + 1) < INIT_RETRY)
        {
            /* 清除先验频点 */
            if (at_obj_exec_cmd(device->client, resp, "AT+NCSEARFCN") != RT_EOK)
            {
                result = -RT_ERROR;
                LOG_E(">> AT+NCSEARFCN");
                goto __exit;
            }
        }
        
        /* check IMEI */
        if (at_obj_exec_cmd(device->client, resp, "AT+CGSN=1") != RT_EOK)
        {
            result = -RT_ERROR;
            LOG_E(">> AT+CGSN=1");
            goto __exit;
        }
        
        /* set max function */
        if (at_obj_exec_cmd(device->client, resp, "AT+CFUN=1") != RT_EOK)
        {
            result = -RT_ERROR;
            LOG_E(">> AT+CFUN=1");
            goto __exit;
        }

#if 0
        /* auto report recv from tcp */
        if (at_obj_exec_cmd(device->client, resp, "AT+NSONMI=2") != RT_EOK)
        {
            result = -RT_ERROR;
            LOG_E(">> AT+NSONMI=2");
            goto __exit;
        }
#endif

        /* disable eDRX mode  */
        if (at_obj_exec_cmd(device->client, resp, "AT+CEDRXS=0,5") != RT_EOK)
        {
            result = -RT_ERROR;
            LOG_E(">> AT+CEDRXS=0,5");
            goto __exit;
        }
        
        /* disable PSM mode  */
        if (at_obj_exec_cmd(device->client, resp, "AT+CPSMS=0") != RT_EOK)
        {
            result = -RT_ERROR;
            LOG_E(">> AT+CPSMS=0");
            goto __exit;
        }

        /* check IMSI */
        if (at_obj_exec_cmd(device->client, resp, "AT+CIMI") != RT_EOK)
        {
            result = -RT_ERROR;
            LOG_E(">> AT+CIMI");
            goto __exit;
        }

        /* attach */
        if (at_obj_exec_cmd(device->client, resp, "AT+CGATT=1") != RT_EOK)
        {
            result = -RT_ERROR;
            LOG_E(">> AT+CGATT=1");
            goto __exit;
        }
        
        /* Get the baudrate */
        if (at_obj_exec_cmd(device->client, resp, "AT+NATSPEED?") != RT_EOK)
        {
            result = -RT_ERROR;
            LOG_E(">> AT+NATSPEED?");
            goto __exit;
        }
        at_resp_parse_line_args_by_kw(resp, "+NATSPEED:", "+NATSPEED:%d", &i);
        LOG_D("%s device baudrate %d", device->name, i);
        
        /* get module version */
        if (at_obj_exec_cmd(device->client, resp, "ATI") != RT_EOK)
        {
            result = -RT_ERROR;
            LOG_E(">> ATI");
            goto __exit;
        }
        for (i = 0; i < (int) resp->line_counts - 1; i++)
        {
            LOG_D("%s", at_resp_get_line(resp, i + 1));
        }
        
        /* check SIM card */
        for (i = 0; i < CPIN_RETRY; i++)
        {
            rt_thread_mdelay(1000);
            if (at_obj_exec_cmd(device->client, resp, "AT+CPIN?") == RT_EOK)
            {
                if (at_resp_get_line_by_kw(resp, "READY") != RT_NULL)
                    break;
            }
        }
        if (i == CPIN_RETRY)
        {
            LOG_E("%s device SIM card detection failed.", device->name);
            result = -RT_ERROR;
            goto __exit;
        }

        /* check signal strength */
        for (i = 0; i < CSQ_RETRY; i++)
        {
            rt_thread_mdelay(1000);
            if (at_obj_exec_cmd(device->client, resp, "AT+CSQ") == RT_EOK)
            {
                int signal_strength = 0, err_rate = 0;
                
                if (at_resp_parse_line_args_by_kw(resp, "+CSQ:", "+CSQ:%d,%d", &signal_strength, &err_rate) > 0)
                {
                    if ((signal_strength != 99) && (signal_strength != 0))
                    {
                        LOG_D("%s device signal strength: %d, channel bit error rate: %d",
                                device->name, signal_strength, err_rate);
                        break;
                    }
                }
            }
        }
        if (i == CSQ_RETRY)
        {
            LOG_E("%s device signal strength check failed", device->name);
            result = -RT_ERROR;
            goto __exit;
        }
                
        /* check the GPRS network is registered */
        for (i = 0; i < CGREG_RETRY; i++)
        {
            rt_thread_mdelay(1000);
            if (at_obj_exec_cmd(device->client, resp, "AT+CGATT?") == RT_EOK)
            {
                int link_stat = 0;
                
                if (at_resp_parse_line_args_by_kw(resp, "+CGATT:", "+CGATT:%d", &link_stat) > 0)
                {
                    if (link_stat == 1)
                    {
                        LOG_D("%s device GPRS is registered", device->name);
                        break;
                    }
                }
            }
        }
        if (i == CGREG_RETRY)
        {
            LOG_E("%s device GPRS is register failed", device->name);
            result = -RT_ERROR;
            goto __exit;
        }
        
#if 0
        /* 输出状态信息 */
        /* AT+NUESTATS */
        at_obj_exec_cmd(device->client, resp, "AT+NUESTATS=ALL");
#endif
        
        /* check the GPRS network IP address */
        for (i = 0; i < IPADDR_RETRY; i++)
        {
            rt_thread_mdelay(1000);
            if (at_obj_exec_cmd(device->client, resp, "AT+CGPADDR") == RT_EOK)
            {
                #define IP_ADDR_SIZE_MAX    16
                char ipaddr[IP_ADDR_SIZE_MAX] = {0};
                
                /* parse response data "+CGPADDR: 0,<IP_address>" */
                if (at_resp_parse_line_args_by_kw(resp, "+CGPADDR:", "+CGPADDR:%*d,%s", ipaddr) > 0)
                {
                    LOG_D("%s device IP address: %s", device->name, ipaddr);
                    break;
                }
            }
        }
        if (i == IPADDR_RETRY)
        {
            LOG_E("%s device GPRS is get IP address failed", device->name);
            result = -RT_ERROR;
            goto __exit;
        }
        
        /* get date time  */
        if (at_obj_exec_cmd(device->client, resp, "AT+CCLK?") == RT_EOK)
        {
            int yy = 0, MM = 0, dd = 0;
            int hh = 0, mm = 0, ss = 0, zz = 0;
            /* parse response data "+CCLK:yy/MM/dd,hh:mm:ss±zz" */
            if (at_resp_parse_line_args_by_kw(resp, "+CCLK:", "+CCLK:%02d/%02d/%02d,%02d:%02d:%02d%d", &yy, &MM, &dd, &hh, &mm, &ss, &zz) > 0)
            {
                LOG_I("%s device date time: %02d/%02d/%02d,%02d:%02d:%02d%+02d", device->name, yy, MM, dd, hh, mm, ss, zz);
                
#ifdef RT_USING_RTC
                
                /* convert to local timezone */
                struct tm tm_time = {
                    .tm_sec = ss, /* seconds after the minute, 0 to 60 */
                    .tm_min = mm, /* minutes after the hour, 0 to 59 */
                    .tm_hour = hh, /* hours since midnight, 0 to 23 */
                    .tm_mday = dd, /* day of the month, 1 to 31 */
                    .tm_mon = (MM - 1), /* months since January, 0 to 11 */
                    .tm_year = ((2000 + yy) - 1900) /* years since 1900 */
                };
                time_t time_utc = mktime(&tm_time);
                time_utc = (time_t)((int)time_utc + ((zz / 4) * 3600));
                struct tm *local_time = gmtime(&time_utc);
                
                /* set system date time */
                rt_err_t ret = set_date((1900 + local_time->tm_year), (local_time->tm_mon + 1), local_time->tm_mday);
                if (ret != RT_EOK)
                {
                    LOG_W("%s device set_date failed", device->name);
                }
                else
                {
                    ret = set_time(local_time->tm_hour, local_time->tm_min, local_time->tm_sec);
                    if (ret != RT_EOK)
                    {
                        LOG_W("%s device set_time failed", device->name);
                    }
                }
#endif
            }
        }
        
        /* initialize successfully  */
        result = RT_EOK;
        break;

    __exit:
        if (result != RT_EOK)
        {
            rt_thread_mdelay(1000);

            LOG_I("%s device initialize retry...", device->name);
        }
    }

    if (resp)
    {
        tb22_free_at_resp(resp);
    }

    if (result == RT_EOK)
    {
        /* set network interface device status and address information */
        tb22_netdev_set_info(device->netdev);
        /* check and create link staus sync thread  */
        if (rt_thread_find(device->netdev->name) == RT_NULL)
        {
            tb22_netdev_check_link_status(device->netdev);
        }

        LOG_I("%s device network initialize success.", device->name);
    }
    else
    {
        LOG_E("%s device network initialize failed(%d).", device->name, result);
    }
}

static int tb22_get_csq(struct at_device *device, int* signal_strength, int *err_rate)
{
    int ret = RT_EOK;
    
    at_response_t resp = tb22_alloc_at_resp(device, 0, rt_tick_from_millisecond(AT_DEFAULT_TIMEOUT));
	RT_ASSERT(resp);
    
    ret = at_obj_exec_cmd(device->client, resp, "AT+CSQ");
    if (ret != RT_EOK)
    {
        LOG_E("at_obj_exec_cmd error(%d).", ret);
        goto __exit;
    }
    
    if (at_resp_parse_line_args_by_kw(resp, "+CSQ:", "+CSQ:%d,%d", signal_strength, err_rate) <= 0)
    {
        LOG_E("at_resp_parse_line_args_by_kw error.");
        ret = -RT_ERROR;
        goto __exit;
    }
    
    LOG_I("%s device csq %d,%d.", device->name, *signal_strength, *err_rate);
    
__exit:
    if (resp)
    {
        tb22_free_at_resp(resp);
    }
    
    return ret;
}

/* tb22 device network initialize */
static int tb22_net_init(struct at_device *device)
{
#ifdef AT_DEVICE_TB22_INIT_ASYN
    rt_thread_t tid;

    tid = rt_thread_create("tb22_net", tb22_init_thread_entry, (void *)device,
                           TB22_THREAD_STACK_SIZE, TB22_THREAD_PRIORITY, 20);
    if (tid)
    {
        rt_thread_startup(tid);
    }
    else
    {
        LOG_E("create %s device init thread failed.", device->name);
        return -RT_ERROR;
    }
#else
    tb22_init_thread_entry(device);
#endif /* AT_DEVICE_TB22_INIT_ASYN */

    return RT_EOK;
}

static int tb22_init(struct at_device *device)
{
	int ret = RT_EOK;
    struct at_device_tb22 *tb22 = (struct at_device_tb22 *)device->user_data;
	
	tb22->at_resp_mp = rt_mp_create("tb22at_resp", TB22_AT_RESP_NUM, TB22_AT_RESP_SIZE);
    if (tb22->at_resp_mp == RT_NULL)
    {
        LOG_E("no memory for tb22 at resp memory pool create.");
        ret = -RT_ENOMEM;
		goto __exit;
    }

    /* initialize AT client */
    at_client_init(tb22->client_name, tb22->recv_bufsz);

    device->client = at_client_get(tb22->client_name);
    if (device->client == RT_NULL)
    {
		rt_mp_delete(tb22->at_resp_mp);
		tb22->at_resp_mp = RT_NULL;
        LOG_E("get AT client(%s) failed.", tb22->client_name);
        ret = -RT_ERROR;
		goto __exit;
    }

    /* register URC data execution function  */
#ifdef AT_USING_SOCKET
    tb22_socket_init(device);
#endif

    /* add tb22 device to the netdev list */
    device->netdev = tb22_netdev_add(tb22->device_name);
    if (device->netdev == RT_NULL)
    {
		rt_mp_delete(tb22->at_resp_mp);
		tb22->at_resp_mp = RT_NULL;
        LOG_E("add netdev(%s) failed.", tb22->device_name);
        ret = -RT_ERROR;
		goto __exit;
    }

    /* initialize tb22 pin configuration */
    if (tb22->power_pin != -1)
    {
        rt_pin_mode(tb22->power_pin, PIN_MODE_OUTPUT);
        rt_pin_write(tb22->power_pin, PIN_LOW);
    }
    rt_pin_mode(TB22_NET_LED_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(TB22_NET_LED_PIN, PIN_HIGH);

    /* initialize tb22 device network */
    ret = tb22_netdev_set_up(device->netdev);
	if (ret != RT_EOK)
	{
		rt_mp_delete(tb22->at_resp_mp);
		tb22->at_resp_mp = RT_NULL;
		LOG_E("tb22_netdev_set_up failed.");
	}
	
__exit:
	if (ret != RT_EOK)
	{
		if (tb22->at_resp_mp)
		{
			rt_mp_delete(tb22->at_resp_mp);
			tb22->at_resp_mp = RT_NULL;
		}
	}
	
	return ret;
}

static int tb22_deinit(struct at_device *device)
{
    RT_ASSERT(device);
 
	struct at_device_tb22 *tb22 = (struct at_device_tb22 *)device->user_data;
	
    /* 关闭连接指示灯 */
    rt_pin_write(TB22_NET_LED_PIN, PIN_HIGH);
	
	if (tb22->at_resp_mp)
	{
		rt_mp_delete(tb22->at_resp_mp);
		tb22->at_resp_mp = RT_NULL;
	}
    
    return tb22_netdev_set_down(device->netdev);
}

static int tb22_control(struct at_device *device, int cmd, void *arg)
{
    int result = -RT_ERROR;

    RT_ASSERT(device);

    switch (cmd)
    {
    case AT_DEVICE_CTRL_RESET:
        result = tb22_reset(device);
        break;
    case AT_DEVICE_CTRL_GET_SIGNAL:
    {
        int signal_strength = 0;
        int err_rate = 0;
        result = tb22_get_csq(device, &signal_strength, &err_rate);
        if ((result == RT_EOK)
            && (arg != NULL))
        {
            *((int*)arg) = signal_strength;
        }
        break;
    }
    case AT_DEVICE_CTRL_SLEEP:
    case AT_DEVICE_CTRL_WAKEUP:
    case AT_DEVICE_CTRL_POWER_ON:
    case AT_DEVICE_CTRL_POWER_OFF:
    case AT_DEVICE_CTRL_LOW_POWER:
    case AT_DEVICE_CTRL_NET_CONN:
    case AT_DEVICE_CTRL_NET_DISCONN:
    case AT_DEVICE_CTRL_SET_WIFI_INFO:
    case AT_DEVICE_CTRL_GET_GPS:
    case AT_DEVICE_CTRL_GET_VER:
        LOG_W("not support the control command(%d).", cmd);
        break;
    default:
        LOG_E("input error control command(%d).", cmd);
        break;
    }

    return result;
}

const struct at_device_ops tb22_device_ops =
{
    tb22_init,
    tb22_deinit,
    tb22_control,
};

/* 从内存池分配at_response_t对象 */
at_response_t tb22_alloc_at_resp(struct at_device *device, rt_size_t line_num, rt_int32_t timeout)
{
	RT_ASSERT(device);
	
	struct at_device_tb22 *tb22 = (struct at_device_tb22 *)device->user_data;
	RT_ASSERT(tb22->at_resp_mp);
	
	uint8_t *buf = (uint8_t*)rt_mp_alloc(tb22->at_resp_mp, RT_WAITING_FOREVER);
	RT_ASSERT(buf);
	
	at_response_t resp = (at_response_t)buf;
	resp->buf = (char *)(buf + sizeof(struct at_response));
    resp->buf_size = TB22_AT_RESP_SIZE - sizeof(struct at_response);
    resp->line_num = line_num;
    resp->line_counts = 0;
    resp->timeout = timeout;
    
    LOG_D("%s at_resp_mp=0x%08x, resp=0x%08x", __FUNCTION__, tb22->at_resp_mp, resp);
	
	return resp;
}

/* 释放at_response_t对象 */
void tb22_free_at_resp(at_response_t resp)
{
    LOG_D("%s resp=0x%08x", __FUNCTION__, resp);
    
	if (resp != RT_NULL)
	{
		rt_mp_free(resp);
	}
}

static int tb22_device_class_register(void)
{
    struct at_device_class *class = RT_NULL;

    class = (struct at_device_class *) rt_calloc(1, sizeof(struct at_device_class));
    if (class == RT_NULL)
    {
        LOG_E("no memory for device class create.");
        return -RT_ENOMEM;
    }

    /* fill tb22 device class object */
#ifdef AT_USING_SOCKET
    tb22_socket_class_register(class);
#endif
    class->device_ops = &tb22_device_ops;

    return at_device_class_register(class, AT_DEVICE_CLASS_TB22);
}
INIT_DEVICE_EXPORT(tb22_device_class_register);

#endif /* AT_DEVICE_USING_TB22 */
