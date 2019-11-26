/****************************************************************************
 *
 * File Name
 *  main.c
 * Author
 *  wangk
 * Date
 *  2019/08/04
 * Descriptions:
 * main接口实现
 *
 ******************************************************************************/
/*----------------------------------------------------------------------------*
**                             Dependencies                                   *
**----------------------------------------------------------------------------*/
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <stdint.h>
#include <stdbool.h>
#if defined(BSP_USING_RSCDRRM020NDSE3)
#include "rscdrrm020ndse3.h"
#elif defined(BSP_USING_AD7730)
#include "ad7730.h"
#endif
#include "sendwave.h"
#include "app.h"
#include "cmd.h"
#include "cyclequeue.h"
#include "at.h"

/**---------------------------------------------------------------------------*
 **                            Debugging Flag                                 *
 **---------------------------------------------------------------------------*/
#define APP_DEBUG
#ifdef APP_DEBUG
    #define APP_TRACE rt_kprintf
#else
    #define APP_TRACE(...)
#endif /* APP_DEBUG */

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
/* EVENT定义 */
#define SENSOR_EVENT_RX_IND 0x00000001
#define VCOM_EVENT_RX_IND 0x00000002
#define VCOM_EVENT_TX_DONE 0x00000004
/* Vcom发送缓冲区长度 */
#define VCOM_SEND_BUF_SIZE 128
/* defined the TS_LED pin: PE1 */
#define TS_LED_PIN GET_PIN(E, 1)
/* defined the BT_POWER pin: PC5 */
#define BT_POWER_PIN GET_PIN(C, 5)
	
/*----------------------------------------------------------------------------*
**                             Data Structures                                *
**----------------------------------------------------------------------------*/
	
/*----------------------------------------------------------------------------*
**                             Local Vars                                     *
**----------------------------------------------------------------------------*/
/* event for application */
static rt_event_t app_event = RT_NULL;

/* SENSOR设备 */
static rt_device_t sensor_dev = RT_NULL;

/* VCOM设备 */
static rt_device_t vcom_dev = RT_NULL;

/* VCOM接收缓冲区 */
static uint8_t vcom_data_buf[128] = {0};

/* Vcom数据发送队列 */
static CycleQueue_T s_tVcomSendQ = {NULL};

/* Vcom发送队列缓冲区 */
static uint8_t s_pu8VcomSendQueBuf[VCOM_SEND_BUF_SIZE * 4] = {0};

/* Vcom发送缓冲区 */
static uint8_t s_pu8VcomSendBuf[VCOM_SEND_BUF_SIZE] = {0};

/* Vcom是否正在发送状态 */
static bool s_bVcomSending = false;

/*----------------------------------------------------------------------------*
**                             Extern Function                                *
**----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*
**                             Local Function                                 *
**----------------------------------------------------------------------------*/
/*************************************************
* Function: sensor_rx_ind
* Description: AD7730数据收取回调函数
* Author: wangk
* Returns: 
* Parameter:
* History:
*************************************************/
static rt_err_t sensor_rx_ind(rt_device_t dev, rt_size_t size)
{
	if (size > 0)
	{
		rt_event_send(app_event, SENSOR_EVENT_RX_IND);
	}
	
	return RT_EOK;
}

/*************************************************
* Function: vcom_rx_ind
* Description: VCOM数据收取回调函数
* Author: wangk
* Returns: 
* Parameter:
* History:
*************************************************/
static rt_err_t vcom_rx_ind(rt_device_t dev, rt_size_t size)
{
	if (size > 0)
	{
		rt_event_send(app_event, VCOM_EVENT_RX_IND);
	}
	
	return RT_EOK;
}

/*************************************************
* Function: vcom_tx_complete
* Description: VCOM数据发送完成回调函数
* Author: wangk
* Returns: 
* Parameter:
* History:
*************************************************/
static rt_err_t vcom_tx_done(rt_device_t dev, void *buffer)
{
	rt_event_send(app_event, VCOM_EVENT_TX_DONE);
	
	return RT_EOK;
}


#if defined(BSP_USING_RSCDRRM020NDSE3)
/*************************************************
* Function: vcom_send_wave
* Description: 通过VCOM发送波形数据帧
* Author: wangk
* Returns: 返回实际发送的字节数
* Parameter:
* History:
*************************************************/
static rt_size_t vcom_send_wave(float val)
{
	//APP_TRACE("vcom_send_wave() val=%f\r\n", val);
	
	char buf[16] = {0};
	char len = ws_point_float(buf, CH1, val);
	return vcom_send_data((const uint8_t*)buf, (uint32_t)len);
}
#elif defined(BSP_USING_AD7730)
/*************************************************
* Function: vcom_send_wave
* Description: 通过VCOM发送波形数据帧
* Author: wangk
* Returns: 返回实际发送的字节数
* Parameter:
* History:
*************************************************/
static rt_size_t vcom_send_wave(int32_t val)
{
	//APP_TRACE("vcom_send_wave() val=%d\r\n", val);
	
	char buf[16] = {0};
	char len = ws_point_int32(buf, CH1, val);
	return vcom_send_data((const uint8_t*)buf, (uint32_t)len);
}
#endif

/*----------------------------------------------------------------------------*
**                             Public Function                                *
**----------------------------------------------------------------------------*/
/*************************************************
* Function: vcom_send_data
* Description: 通过VCOM输出数据
* Author: wangk
* Returns: 返回实际输出的字节数
* Parameter:
* History:
*************************************************/
uint32_t vcom_send_data(const uint8_t* data, uint32_t len)
{
	if (RT_NULL == vcom_dev)
	{
		return 0;
	}
	
	/* 数据插入发送缓冲队列 */
	uint32_t u32SendLen = CycleQueue_Insert(&s_tVcomSendQ, data, len);
	
	if (s_bVcomSending)
	{ // 正在发送状态
		/* 将会自动发送缓冲区中的数据 */
	}
	else
	{ // 已停止发送
		/* 启动发送 */
		uint8_t* pu8SendBuf = s_pu8VcomSendBuf;
		uint32_t u32DataLen = CycleQueue_Delete(&s_tVcomSendQ, pu8SendBuf, VCOM_SEND_BUF_SIZE, NULL);
		if (u32DataLen > 0)
		{
			/* 设置正在发送状态 */
			s_bVcomSending = true;
			/* 请求发送缓冲区中的数据 */
			rt_device_write(vcom_dev, 0, pu8SendBuf, (rt_size_t)u32DataLen);
		}
	}
	
	return u32SendLen;
}

/*************************************************
* Function: adc_calibration
* Description: 执行ADC通道校准
* Author: wangk
* Returns: 
* Parameter:
* History:
*************************************************/
bool adc_calibration(int adc_channel)
{
	APP_TRACE("adc_calibration() adc_channel=%d\r\n", adc_channel);

#if defined(BSP_USING_RSCDRRM020NDSE3)
	if (0 != adc_channel)
	{
		APP_TRACE("adc_calibration() failed, invalid channel(%d)!\r\n", adc_channel);
		return false;
	}
	
	rt_err_t ret = rt_device_control(sensor_dev, RSCDRRM020NDSE3_AUTO_ZERO, RT_NULL);
	if (RT_EOK != ret)
	{
		APP_TRACE("adc_calibration() failed, rt_device_control(RSCDRRM020NDSE3_AUTO_ZERO) error(%d)!\r\n", ret);
		return false;
	}
#elif defined(BSP_USING_AD7730)
	if ((adc_channel < 0)
		|| (adc_channel >= 3))
	{
		APP_TRACE("adc_calibration() failed, invalid channel(%d)!\r\n", adc_channel);
		return false;
	}
	
	rt_err_t ret = rt_device_control(sensor_dev, AD7730_DO_CALIBRATION, (void*)adc_channel);
	if (RT_EOK != ret)
	{
		APP_TRACE("adc_calibration() failed, rt_device_control(AD7730_DO_CALIBRATION) error(%d)!\r\n", ret);
		return false;
	}
#endif
	
	return true;
}

/*************************************************
* Function: adc_start
* Description: 启动ADC采集
* Author: wangk
* Returns: 
* Parameter:
* History:
*************************************************/
bool adc_start(int adc_channel)
{
	APP_TRACE("adc_start() adc_channel=%d\r\n", adc_channel);
	
#if defined(BSP_USING_RSCDRRM020NDSE3)
	if (0 != adc_channel)
	{
		APP_TRACE("adc_start() failed, invalid channel(%d)!\r\n", adc_channel);
		return false;
	}
	
	/* 启动RSCDRRM020NDSE3 */
	rt_err_t ret = rt_device_control(sensor_dev, RSCDRRM020NDSE3_START, RT_NULL);
	if (RT_EOK != ret)
	{
		APP_TRACE("adc_start() failed, rt_device_control(RSCDRRM020NDSE3_START) error(%d)!\r\n", ret);
		return false;
	}
#elif defined(BSP_USING_AD7730)
	if ((adc_channel < 0)
		|| (adc_channel >= 3))
	{
		APP_TRACE("adc_start() failed, invalid channel(%d)!\r\n", adc_channel);
		return false;
	}
	
#if 0
	/* 执行ADC校准 */
	rt_err_t ret = rt_device_control(sensor_dev, AD7730_DO_CALIBRATION, (void*)adc_channel);
	if (RT_EOK != ret)
	{
		APP_TRACE("adc_start() failed, rt_device_control(AD7730_DO_CALIBRATION) error(%d)!\r\n", ret);
		return false;
	}
#else
	rt_err_t ret = RT_EOK;
#endif
	
	/* 启动AD7730连续读取 */
	ret = rt_device_control(sensor_dev, AD7730_START_CONT_READ, (void*)adc_channel);
	if (RT_EOK != ret)
	{
		APP_TRACE("adc_start() failed, rt_device_control(AD7730_START_CONT_READ) error(%d)!\r\n", ret);
		return false;
	}
#endif
	
	/* 开启TS_LED */
	rt_pin_write(TS_LED_PIN, PIN_HIGH);
	
	return true;
}

/*************************************************
* Function: adc_stop
* Description: 停止ADC采集
* Author: wangk
* Returns: 
* Parameter:
* History:
*************************************************/
bool adc_stop(void)
{
	APP_TRACE("adc_stop()\r\n");
	
#if defined(BSP_USING_RSCDRRM020NDSE3)
	/* 停止RSCDRRM020NDSE3 */
	rt_err_t ret = rt_device_control(sensor_dev, RSCDRRM020NDSE3_STOP, RT_NULL);
	if (RT_EOK != ret)
	{
		APP_TRACE("adc_stop() failed, rt_device_control(RSCDRRM020NDSE3_STOP) error(%d)!\r\n", ret);
		return false;
	}
#elif defined(BSP_USING_AD7730)
	/* 停止AD7730连续读取 */
	rt_err_t ret = rt_device_control(sensor_dev, AD7730_STOP_CONT_READ, RT_NULL);
	if (RT_EOK != ret)
	{
		APP_TRACE("adc_stop() failed, rt_device_control(AD7730_STOP_CONT_READ) error(%d)!\r\n", ret);
		return false;
	}
#endif
	
	/* 关闭TS_LED */
	rt_pin_write(TS_LED_PIN, PIN_LOW);
	
	return true;
}

/*************************************************
* Function: main
* Description: main入口函数
* Author: wangk
* Returns: 
* Parameter:
* History:
*************************************************/
int main(void)
{
	int main_ret = 0;
	
	rt_err_t ret = RT_EOK;
	
	/* 创建Vcom发送循环队列 */
	CycleQueue_Create(&s_tVcomSendQ, s_pu8VcomSendQueBuf, sizeof(s_pu8VcomSendQueBuf));
	
	/* 初始化CMD模块 */
	CMD_Init();
	
	/* set TS_LED pin mode to output */
    rt_pin_mode(TS_LED_PIN, PIN_MODE_OUTPUT);
	rt_pin_write(TS_LED_PIN, PIN_LOW);
	
	/* set BT_POWER pin mode to output */
    rt_pin_mode(BT_POWER_PIN, PIN_MODE_OUTPUT);
	rt_pin_write(BT_POWER_PIN, PIN_LOW);
	
	/* 创建EVENT */
	app_event = rt_event_create("app_event", RT_IPC_FLAG_FIFO);
	if (RT_NULL == app_event)
	{
		APP_TRACE("create app event failed!\r\n");
		main_ret = -RT_ERROR;
		goto _END;
	}
	
#if defined(BSP_USING_RSCDRRM020NDSE3)
	/* 打开AD7730设备 */
	sensor_dev = rt_device_find(RSCDRRM020NDSE3_DEVICE_NAME);
	if (RT_NULL == sensor_dev)
	{
		APP_TRACE("device %s not found!\r\n", RSCDRRM020NDSE3_DEVICE_NAME);
		main_ret = -RT_ERROR;
		goto _END;
	}
#elif defined(BSP_USING_AD7730)
	/* 打开AD7730设备 */
	sensor_dev = rt_device_find(AD7730_DEVICE_NAME);
	if (RT_NULL == sensor_dev)
	{
		APP_TRACE("device %s not found!\r\n", AD7730_DEVICE_NAME);
		main_ret = -RT_ERROR;
		goto _END;
	}
#endif
	
	if (RT_NULL != sensor_dev)
	{
		ret = rt_device_open(sensor_dev, RT_DEVICE_FLAG_RDONLY | RT_DEVICE_FLAG_INT_RX);
		if (RT_EOK != ret)
		{
			APP_TRACE("open device sensor failed(%d)!\r\n", ret);
			main_ret = ret;
			goto _END;
		}
		
		ret = rt_device_set_rx_indicate(sensor_dev, sensor_rx_ind);
		if (RT_EOK != ret)
		{
			APP_TRACE("set vcom rx indicate failed(%d)!\r\n", ret);
			main_ret = ret;
			goto _END;
		}
	}
	
	/* 打开VCOM设备 */
	vcom_dev = rt_device_find("vcom");
	if (RT_NULL == vcom_dev)
	{
		APP_TRACE("vcom device is not found!\r\n");
		main_ret = -RT_ERROR;
		goto _END;
	}
	
	ret = rt_device_open(vcom_dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX | RT_DEVICE_FLAG_DMA_TX);
	if (RT_EOK != ret)
	{
		APP_TRACE("open vcom device failed(%d)!\r\n", ret);
		main_ret = ret;
		goto _END;
	}
	
	ret = rt_device_set_rx_indicate(vcom_dev, vcom_rx_ind);
	if (RT_EOK != ret)
	{
		APP_TRACE("set vcom rx indicate failed(%d)!\r\n", ret);
		main_ret = ret;
		goto _END;
	}
	
	ret = rt_device_set_tx_complete(vcom_dev, vcom_tx_done);
	if (RT_EOK != ret)
	{
		APP_TRACE("main() call rt_device_set_tx_complete failed(%d)!\r\n", ret);
		main_ret = ret;
		goto _END;
	}
	
	/* 开启蓝牙模块电源 */
	rt_pin_write(BT_POWER_PIN, PIN_HIGH);
	
	ret = at_client_init("uart2", 128);
	if (RT_EOK != ret)
	{
		APP_TRACE("main() call at_client_init(uart2) failed(%d)!\r\n", ret);
		main_ret = ret;
		goto _END;
	}

	/* 进入事件循环 */
    while (1)
	{
		
		rt_uint32_t event_recved = 0;
		ret = rt_event_recv(app_event, (SENSOR_EVENT_RX_IND | VCOM_EVENT_RX_IND | VCOM_EVENT_TX_DONE),
						  (RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR),
						  RT_WAITING_FOREVER, &event_recved);
		if (RT_EOK != ret)
		{
			APP_TRACE("recv event failed(%d)!\r\n", ret);
			main_ret = ret;
			break;
		}
		
		if (event_recved & SENSOR_EVENT_RX_IND)
		{ // 收到SENSOR数据
		#if defined(BSP_USING_RSCDRRM020NDSE3)
			float sensor_val = 0;
			rt_size_t read_len = rt_device_read(sensor_dev, 0, &sensor_val, sizeof(sensor_val));
			//APP_TRACE("sensor_val=%f\r\n", sensor_val);
			/* 通过VCOM发送波形数据帧 */
			vcom_send_wave(sensor_val);
		#elif defined(BSP_USING_AD7730)
			int32_t sensor_val = 0;
			rt_size_t read_len = rt_device_read(sensor_dev, 0, &sensor_val, sizeof(sensor_val));
			//APP_TRACE("sensor_val=%d\r\n", sensor_val);
			/* 通过VCOM发送波形数据帧 */
			vcom_send_wave(sensor_val);
		#endif
		}
		
		if (event_recved & VCOM_EVENT_RX_IND)
		{ // 收到VCOM数据
			/* 收取所有数据 */
			while (1)
			{
				rt_size_t read_len = rt_device_read(vcom_dev, 0, vcom_data_buf, sizeof(vcom_data_buf));
				if (read_len > 0)
				{ // 收取到数据
					//APP_TRACE("vcom read_len=%d\r\n", read_len);
					
					/* 数据输入到CMD模块 */
					CMD_OnRecvData(vcom_data_buf, (uint32_t)read_len);
				}
				else
				{ // 数据已收取完毕
					break;
				}
			}
		}
		
		if (event_recved & VCOM_EVENT_TX_DONE)
		{ // VCOM数据已发送完毕
			/* 读取数据到Vcom发送缓冲区 */
			uint8_t* pu8SendBuf = s_pu8VcomSendBuf;
			uint32_t u32DataLen = CycleQueue_Delete(&s_tVcomSendQ, pu8SendBuf, VCOM_SEND_BUF_SIZE, NULL);
			if (u32DataLen > 0)
			{ // 有数据需要发送
				/* 请求发送缓冲区中的数据 */
				rt_device_write(vcom_dev, 0, pu8SendBuf, (rt_size_t)u32DataLen);
			}
			else
			{ // 队列中的数据已发送完毕
				/* 清除正在发送状态 */
				s_bVcomSending = false;
			}
		}
    }
	
_END:
	/* 释放资源 */
	if (RT_NULL != app_event)
	{
		rt_event_delete(app_event);
		app_event = RT_NULL;
	}
	if (RT_NULL != sensor_dev)
	{
		rt_device_close(sensor_dev);
		sensor_dev = RT_NULL;
	}
	if (RT_NULL != vcom_dev)
	{
		rt_device_close(vcom_dev);
		vcom_dev = RT_NULL;
	}
	
    return main_ret;
}

/**---------------------------------------------------------------------------*
 **                         Compiler Flag                                     *
 **---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif
// End of main.c
