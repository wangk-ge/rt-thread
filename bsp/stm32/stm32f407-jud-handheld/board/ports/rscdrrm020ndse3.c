/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-06-30     wangk        add rscdrrm020ndse3 driver
 */

#include <rtthread.h>
#include <stdbool.h>
#include "drv_spi.h"
#include "rscdrrm020ndse3.h"
#include "Pressure_Comp.h"

#if defined(BSP_USING_RSCDRRM020NDSE3)

#define RSCDRRM020NDSE3_DEBUG

//#define USE_FILTER

#ifdef RSCDRRM020NDSE3_DEBUG
    #define RSCDRRM020NDSE3_TRACE	rt_kprintf
#else
    #define RSCDRRM020NDSE3_TRACE(...)
#endif /* RSCDRRM020NDSE3_DEBUG */

/* SPI������ */
#define RSCDRRM020NDSE3_SPI_BUS_NAME "spi2"
/* EEPROM SPI�豸�� */
#define RSCDRRM020NDSE3_SPI_EE_DEVICE_NAME "spi20"
/* ADC SPI�豸�� */
#define RSCDRRM020NDSE3_SPI_ADC_DEVICE_NAME "spi21"
/* defined the PWER_EN pin: PE7 */
#define RSCDRRM020NDSE3_PWER_EN_PIN		GET_PIN(E, 7)
/* defined the RDY pin: PB12 */
#define RSCDRRM020NDSE3_RDY_PIN			GET_PIN(B, 12)
/* defined the CS_ADC pin: PC2 */
#define RSCDRRM020NDSE3_CS_AD_PIN	 	GET_PIN(C, 2)
/* defined the CS_EE pin: PC3 */
#define RSCDRRM020NDSE3_CS_EE_PIN	 	GET_PIN(C, 3)
/* ������EEPROM��С(�ֽ���) */
#define RSCDRRM020NDSE3_EEPROM_SIZE		(452)

/* ADC�ɼ�ģʽ */
#define RSCDRRM020NDSE3_ADC_NORMAL 0x00
#define RSCDRRM020NDSE3_ADC_FAST 0x10

/* ADC�ɼ�Ƶ��(Normalģʽ��) */
#define RSCDRRM020NDSE3_ADC_20HZ 0x00
#define RSCDRRM020NDSE3_ADC_45HZ 0x20
#define RSCDRRM020NDSE3_ADC_90HZ 0x40
#define RSCDRRM020NDSE3_ADC_175HZ 0x60
#define RSCDRRM020NDSE3_ADC_330HZ 0x80
#define RSCDRRM020NDSE3_ADC_600HZ 0xA0
#define RSCDRRM020NDSE3_ADC_1000HZ 0xC0
/* ADC�ɼ�Ƶ��(FASTģʽ�·���) */

/* ADC�ɼ����� */
#define RSCDRRM020NDSE3_ADC_TEMPERATURE 0x06 // �¶�
#define RSCDRRM020NDSE3_ADC_PRESSURE 0x04 // ѹ��

/* ʹ�õ�Ƶ�ʺ�ģʽ */
#ifdef USE_FILTER
#define RSCDRRM020NDSE3_ADC_FREQ RSCDRRM020NDSE3_ADC_1000HZ 
#define RSCDRRM020NDSE3_ADC_MODE RSCDRRM020NDSE3_ADC_NORMAL
#else
#define RSCDRRM020NDSE3_ADC_FREQ RSCDRRM020NDSE3_ADC_600HZ
#define RSCDRRM020NDSE3_ADC_MODE RSCDRRM020NDSE3_ADC_FAST
#endif

/* EVENT���� */
#define RSCDRRM020NDSE3_EVENT_DATA_READY 0x00000001

#ifdef USE_FILTER
/* �˲����ɼ����� */
#define RSCDRRM020NDSE3_FILTER_N (4)
#endif

#define rscdrrm020ndse3_lock(dev)      rt_mutex_take(&((struct rscdrrm020ndse3_device*)dev)->lock, RT_WAITING_FOREVER);
#define rscdrrm020ndse3_unlock(dev)    rt_mutex_release(&((struct rscdrrm020ndse3_device*)dev)->lock);

/* �������豸���� */
static struct rscdrrm020ndse3_device rscdrrm020ndse3_dev;

/* �������豸�߳� */
static struct rt_thread* rscdrrm020ndse3_thread = RT_NULL;

static rt_event_t rscdrrm020ndse3_event = RT_NULL;

#ifdef USE_FILTER
/* �˲�����ر��� */
static uint32_t rscdrrm020ndse3_filter_min_val = 0xFFFFFFFF; // �����е���Сֵ
static uint32_t rscdrrm020ndse3_filter_max_val = 0; // �����е����ֵ
static uint32_t rscdrrm020ndse3_filter_sum = 0; // �����ۼӺ�(���ڼ����ֵ)
static uint32_t rscdrrm020ndse3_filter_sample_cnt = 0; // ��ǰ�Ѳɼ���������
#endif

/* �Զ�������ɻص����� */
static ATUO_ZERO_CPL_FUNC s_pfnAutoZeroCompleted = NULL;

/* ������������Դ */
static void rscdrrm020ndse3_power_on(rt_device_t dev)
{
	RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_power_on()\r\n");
	
	/* ������������Դ */
	rt_pin_write(RSCDRRM020NDSE3_PWER_EN_PIN, PIN_HIGH);
	
	/* �ȴ�������������� */
	rt_thread_mdelay(1);
}

/* �رմ�������Դ */
static void rscdrrm020ndse3_power_off(rt_device_t dev)
{
	RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_power_off()\r\n");
	
	/* �رմ�������Դ */
	rt_pin_write(RSCDRRM020NDSE3_PWER_EN_PIN, PIN_LOW);
}

/* ��ȡEEPROM�е�ָ����ַ��ָ�����ȵ����� */
static rt_err_t rscdrrm020ndse3_spi_read_eeprom(uint16_t address, uint8_t* recv_buf, rt_size_t buf_len)
{
	struct rt_spi_device* spi_device = rscdrrm020ndse3_dev.spi_ee_device;
	
	uint8_t send_buf[2] = {0};
	/* 
		To read from memory, the host sends an EAD_EEPROM instruction[0000 X011] followed by an 8-bit address. 
		The ��X�� bit in the read instruction is the ninth (MSB) address bit.
	*/
	if (address & 0x0100)
	{ // ninth (MSB) address bit is 1
		send_buf[0] = 0x0B; // EAD_EEPROM
	}
	else
	{ // ninth (MSB) address bit is 0
		send_buf[0] = 0x03; // EAD_EEPROM
	}
	send_buf[1] = (uint8_t)(address & 0x00FF); // ADDRESS

    return rt_spi_send_then_recv(spi_device, send_buf, sizeof(send_buf), recv_buf, buf_len);
}

/* ����ADC���� */
static rt_err_t rscdrrm020ndse3_spi_send_adc_cmd(uint8_t cmd)
{
	struct rt_spi_device* spi_device = rscdrrm020ndse3_dev.spi_adc_device;
	
    rt_size_t ret = rt_spi_send(spi_device, &cmd, 1);
	if (1 != ret)
	{
		return rt_get_errno();
	}
	
	return RT_EOK;
}

/* д��ADC�Ĵ��� */
static rt_err_t rscdrrm020ndse3_spi_write_adc_reg(uint8_t reg, const uint8_t* send_data, rt_size_t data_len)
{
	struct rt_spi_device* spi_device = rscdrrm020ndse3_dev.spi_adc_device;
	
	/*
		To program a configuration register, the host sends a WREG command [0100 RRNN], 
		where ��RR�� is the register number and ��NN�� is the number of bytes to be written �C1.
	*/
	uint8_t send_buf[1] = {0};
	send_buf[0] = 0x40 | ((reg & 0x03) << 2) | ((data_len - 1) & 0x03);

    return rt_spi_send_then_send(spi_device, send_buf, sizeof(send_buf), send_data, data_len);
}

/* ��ȡADC�Ĵ��� */
static rt_err_t rscdrrm020ndse3_spi_read_adc_reg(uint8_t reg, uint8_t* recv_buf, rt_size_t buf_len)
{
	struct rt_spi_device* spi_device = rscdrrm020ndse3_dev.spi_adc_device;
	
	/*
		RREG (0010 rrnn), 
		where ��RR�� is the register number and ��NN�� is the number of bytes to be read �C1.
	*/
	uint8_t send_buf[1] = {0};
	send_buf[0] = 0x20 | ((reg & 0x03) << 2) | ((buf_len - 1) & 0x03);

    return rt_spi_send_then_recv(spi_device, send_buf, sizeof(send_buf), recv_buf, buf_len);
}

static rt_err_t rscdrrm020ndse3_spi_adc_transfer(const uint8_t* send_data, uint8_t* recv_buf, rt_size_t length)
{
	struct rt_spi_device* spi_device = rscdrrm020ndse3_dev.spi_adc_device;
	
	rt_size_t ret = rt_spi_transfer(spi_device, send_data, recv_buf, length);
	if (length != ret)
	{
		return rt_get_errno();
	}

	return RT_EOK;
}

/* rscdrrm020ndse3 rdy interupt service */
static void rscdrrm020ndse3_rdy_isr(void *args)
{
	struct rscdrrm020ndse3_device* rscdrrm020ndse3 = (struct rscdrrm020ndse3_device*)args;
	
	/* ״̬��� */
	if(!rscdrrm020ndse3->start)
	{
		return;
	}
	
	/* ֪ͨ�������̴߳������ݶ�ȡ */
	rt_event_send(rscdrrm020ndse3_event, RSCDRRM020NDSE3_EVENT_DATA_READY);
}

/* �������������ݲɼ� */
static rt_err_t rscdrrm020ndse3_start(rt_device_t dev)
{
	struct rscdrrm020ndse3_device* rscdrrm020ndse3 = (struct rscdrrm020ndse3_device*)dev;
	RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_start()\r\n");
	
	rscdrrm020ndse3_lock(dev);
	
	/* ״̬��� */
	if (rscdrrm020ndse3->start)
	{ // ������
		rscdrrm020ndse3_unlock(dev);
		return RT_EOK;
	}
	
	rscdrrm020ndse3_unlock(dev);
	
	/*
		The ADC reset command RESET [0000 0110] resets the ADC to the default values.
	*/
	rt_err_t ret = rscdrrm020ndse3_spi_send_adc_cmd(0x06);
	if (RT_EOK != ret)
	{
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_open() call rscdrrm020ndse3_spi_send_adc_cmd(0x06) failed, error(%d)!\r\n", ret);
		goto _EXIT;
	}
	
	/*
		Resets the device to the default values. 
		Wait at least (50 ��s + 32 �� t(CLK)) after the RESET command is sent before sending any other command.
	*/
	//while (PIN_LOW != rt_pin_read(RSCDRRM020NDSE3_RDY_PIN))
	{
		rt_thread_mdelay(1);
	}
	
	/*
		����ADC����
		Initialize all four configuration registers to the default values in the EEPROM��s Relative addresses 61, 63, 65 and 67 
		by sending a WREG command to address 0
	*/
	ret = rscdrrm020ndse3_spi_write_adc_reg(0x00, rscdrrm020ndse3->adc_cfg_param, sizeof(rscdrrm020ndse3->adc_cfg_param));
	if (RT_EOK != ret)
    {
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_open() call rscdrrm020ndse3_spi_write_adc_reg(0, adc_cfg_param) failed, error(%d)!\r\n", ret);
		goto _EXIT;
    }
	
	/* ��ȡ���üĴ���������֤ */
	{ // �������뾯��
		uint8_t cfg_regs[4] = {0};
		ret = rscdrrm020ndse3_spi_read_adc_reg(0x00, cfg_regs, sizeof(cfg_regs));
		if (RT_EOK != ret)
		{
			RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_open() call rscdrrm020ndse3_spi_read_adc_reg(0x00) failed, error(%d)!\r\n", ret);
			goto _EXIT;
		}
	}
	
	/*
		������ADCΪ�¶Ȳɼ�ģʽ�����ò�����
		Configure the sensor to temperature mode and the desired data rate by setting configuration register 1 by sending a WREG
		command to address 1, [0100 0100] followed by the single configuration byte. Bit 1 (TS) of the configuration register should
		be set to 1.
	*/
	{ // �������뾯��
		uint8_t mode = RSCDRRM020NDSE3_ADC_TEMPERATURE | RSCDRRM020NDSE3_ADC_FREQ | RSCDRRM020NDSE3_ADC_MODE;
		ret = rscdrrm020ndse3_spi_write_adc_reg(0x01, &mode, 1);
		if (RT_EOK != ret)
		{
			RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_open() call rscdrrm020ndse3_spi_write_adc_reg(0x01) failed, error(%d)!\r\n", ret);
			goto _EXIT;
		}
	}
	
	/*
		Send 08h command to start data conversion on ADC.
	*/
	ret = rscdrrm020ndse3_spi_send_adc_cmd(0x08);
	if (RT_EOK != ret)
	{
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_open() call rscdrrm020ndse3_spi_send_adc_cmd(0x08) failed, error(%d)!\r\n", ret);
		goto _EXIT;
	}
	
	/*
		the START/SYNC command must be issued one time to start converting continuously. 
		Sending the START/SYNC command while converting in continuous conversion mode resets the
		digital filter and restarts continuous conversions
	*/

	/* ʹ�ܴ���������READY�жϼ�� */
	ret = rt_pin_irq_enable(RSCDRRM020NDSE3_RDY_PIN, PIN_IRQ_ENABLE);
	if (RT_EOK != ret)
	{
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_open() call rt_pin_irq_enable(RSCDRRM020NDSE3_RDY_PIN) failed, error(%d)!\r\n", ret);
		goto _EXIT;
	}
	
	rscdrrm020ndse3_lock(dev);
	
	/* ������ */
	rscdrrm020ndse3->pressure_comp = 0.0;
	
	/* ��ǰΪ�¶Ȳɼ�ģʽ */
	rscdrrm020ndse3->mode = RSCDRRM020NDSE3_TEMPERATURE;
	
	/* ��������״̬ */
	rscdrrm020ndse3->start = true;
	
	rscdrrm020ndse3_unlock(dev);
	
_EXIT:
	return ret;
}

/* ֹͣ���������ݲɼ� */
static rt_err_t rscdrrm020ndse3_stop(rt_device_t dev)
{
	struct rscdrrm020ndse3_device* rscdrrm020ndse3 = (struct rscdrrm020ndse3_device*)dev;
	RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_stop()\r\n");
	
	rscdrrm020ndse3_lock(dev);
	
	/* ״̬��� */
	if (!rscdrrm020ndse3->start)
	{ // ��ֹͣ
		rscdrrm020ndse3_unlock(dev);
		return RT_EOK;
	}
	
	rscdrrm020ndse3_unlock(dev);
	
	/*
		The ADC reset command RESET [0000 0110] resets the ADC to the default values.
	*/
	rt_err_t ret = rscdrrm020ndse3_spi_send_adc_cmd(0x06);
	if (RT_EOK != ret)
	{
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_stop() call rscdrrm020ndse3_spi_send_adc_cmd(0x06) failed, error(%d)!\r\n", ret);
		goto _EXIT;
	}
	
	/* ֹͣ����������READY�жϼ�� */
	ret = rt_pin_irq_enable(RSCDRRM020NDSE3_RDY_PIN, PIN_IRQ_DISABLE);
	if (RT_EOK != ret)
	{
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_stop() call rt_pin_irq_enable(RSCDRRM020NDSE3_RDY_PIN, PIN_IRQ_DISABLE) failed, error(%d)!\r\n", ret);
		goto _EXIT;
	}
	
	rscdrrm020ndse3_lock(dev);
	
	/* ����ֹͣ״̬ */
	rscdrrm020ndse3->start = false;
	
	/* ������ */
	rscdrrm020ndse3->pressure_comp = 0.0;
	
	rscdrrm020ndse3_unlock(dev);
	
_EXIT:
	return ret;
}

/* �Զ����� */
static rt_err_t rscdrrm020ndse3_auto_zero(rt_device_t dev, ATUO_ZERO_CPL_FUNC pfnAutoZeroCompleted)
{
	struct rscdrrm020ndse3_device* rscdrrm020ndse3 = (struct rscdrrm020ndse3_device*)dev;
	RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_auto_zero()\r\n");
	
	rscdrrm020ndse3_lock(dev);
	
	/* ״̬��� */
	if (!rscdrrm020ndse3->start)
	{ // δ����
		rscdrrm020ndse3_unlock(dev);
		return RT_ERROR;
	}
	
	/* �����Զ����������־ */
	rscdrrm020ndse3->auto_zero = true;
	
	/* ��װ��ɻص����� */
	s_pfnAutoZeroCompleted = pfnAutoZeroCompleted;
	
	rscdrrm020ndse3_unlock(dev);
	
	return RT_EOK;
}

/* �������߳� */
static void rscdrrm020ndse3_thread_entry(void* param)
{
	struct rscdrrm020ndse3_device* rscdrrm020ndse3 = (struct rscdrrm020ndse3_device*)param;
	/* ��ǰ�ɼ���ѹ��ֵ(δ����) */
	uint32_t pressure = 0;
	/* ��ǰ�ɼ����¶�ֵ */
	uint32_t temperature = 0;
	
	/* �����¼�ѭ�� */
    while (1)
	{
		
		rt_uint32_t event_recved = 0;
		rt_err_t ret = rt_event_recv(rscdrrm020ndse3_event, RSCDRRM020NDSE3_EVENT_DATA_READY,
						  (RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR),
						  RT_WAITING_FOREVER, &event_recved);
		if (RT_EOK != ret)
		{
			RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_thread_entry() rt_event_recv(rscdrrm020ndse3_event) failed, error(%d)!\r\n", ret);
			break;
		}
		
		rscdrrm020ndse3_lock(rscdrrm020ndse3);
		
		/* ״̬��� */
		if(!rscdrrm020ndse3->start)
		{
			rscdrrm020ndse3_unlock(rscdrrm020ndse3);
			continue;
		}
		
		rscdrrm020ndse3_unlock(rscdrrm020ndse3);
		
		if (event_recved & RSCDRRM020NDSE3_EVENT_DATA_READY)
		{// �����Ѿ���
			rscdrrm020ndse3_lock(rscdrrm020ndse3);
			if (RSCDRRM020NDSE3_PRESSURE == rscdrrm020ndse3->mode) // pressure
			{
#ifdef USE_FILTER
				if ((rscdrrm020ndse3_filter_sample_cnt + 1) >= RSCDRRM020NDSE3_FILTER_N)
#endif
				{ // �ɼ����������ѴﵽҪ��
					rscdrrm020ndse3->mode = RSCDRRM020NDSE3_TEMPERATURE; // �л�ģʽ
				}
				rscdrrm020ndse3_unlock(rscdrrm020ndse3);
				
				/* ��ȡ������ѹ�����ݲ��л�Ϊ�¶Ȳɼ�ģʽ */
				uint8_t send_buf[4] = {0};
				uint8_t recv_buf[4] = {0};
				send_buf[0] = 0xFF;
				send_buf[1] = 0x40 | ((0x01 & 0x03) << 2) | ((1 - 1) & 0x03); // WREG command [0100 RRNN],
				if (RSCDRRM020NDSE3_TEMPERATURE == rscdrrm020ndse3->mode)
				{ // �´βɼ��¶�
					send_buf[2] = RSCDRRM020NDSE3_ADC_TEMPERATURE | RSCDRRM020NDSE3_ADC_FREQ | RSCDRRM020NDSE3_ADC_MODE; // mode
				}
				else // if (RSCDRRM020NDSE3_PRESSURE == rscdrrm020ndse3->mode)
				{ // �´βɼ�ѹ��
					send_buf[2] = RSCDRRM020NDSE3_ADC_PRESSURE | RSCDRRM020NDSE3_ADC_FREQ | RSCDRRM020NDSE3_ADC_MODE; // mode
				}
				send_buf[3] = 0xFF;
				ret = rscdrrm020ndse3_spi_adc_transfer(send_buf, recv_buf, sizeof(send_buf));
				if (RT_EOK == ret)
				{
					/* 
						����ѹ������
						Pressure data are output starting with MSB, in 24-bit 2��s complement format.
					*/
					pressure = (((uint32_t)recv_buf[0] << 16) & 0x00FF0000) 
						| (((uint32_t)recv_buf[1] << 8) & 0x0000FF00) 
						| ((uint32_t)recv_buf[2] & 0x000000FF);
					
#ifdef USE_FILTER
					/* ͳ����������Сֵ�����ֵ���ۼӺ͡����� */
					if (pressure < rscdrrm020ndse3_filter_min_val)
					{
						rscdrrm020ndse3_filter_min_val = pressure;
					}
					else if (pressure > rscdrrm020ndse3_filter_max_val)
					{
						rscdrrm020ndse3_filter_max_val = pressure;
					}
					rscdrrm020ndse3_filter_sum += pressure;
					rscdrrm020ndse3_filter_sample_cnt++;
					
					if (rscdrrm020ndse3_filter_sample_cnt >= RSCDRRM020NDSE3_FILTER_N)
#endif
					{ // �ɼ����������ѴﵽҪ��
						
#ifdef USE_FILTER
						/* ȥ�����ֵ����Сֵ,�����ֵ */
						uint32_t avg_pressure = (rscdrrm020ndse3_filter_sum - rscdrrm020ndse3_filter_min_val - rscdrrm020ndse3_filter_max_val) / (rscdrrm020ndse3_filter_sample_cnt - 2);
#else
						uint32_t avg_pressure = pressure;
#endif
						
						rscdrrm020ndse3_lock(rscdrrm020ndse3);
						
#ifdef USE_FILTER
						/* ���³�ʼ���˲������� */
						rscdrrm020ndse3_filter_sum = 0;
						rscdrrm020ndse3_filter_sample_cnt = 0;
						rscdrrm020ndse3_filter_min_val = 0xFFFFFFFF;
						rscdrrm020ndse3_filter_max_val = 0;
#endif
						
						/* �����Զ��������� */
						if (rscdrrm020ndse3->auto_zero)
						{
							/* ����Զ����������־ */
							rscdrrm020ndse3->auto_zero = false;
							/* ��ʱ����ص�����ָ�� */
							ATUO_ZERO_CPL_FUNC pfnAutoZeroCompleted = s_pfnAutoZeroCompleted;
							/* ��ջص�����ָ�� */
							s_pfnAutoZeroCompleted = NULL;
							rscdrrm020ndse3_unlock(rscdrrm020ndse3);
							
							/* ���ù������ */
							AutoZero_Pressure(avg_pressure, temperature);
							
							/* ��������˻ص����� */
							if (pfnAutoZeroCompleted)
							{
								/* ���ûص����� */
								pfnAutoZeroCompleted();
							}
						}
						else
						{
							rscdrrm020ndse3_unlock(rscdrrm020ndse3);
						}
						
						/* ִ���¶Ȳ����������� */
						CompReturn_Struct result = Compensate_Pressure(avg_pressure, temperature);
						if (PRESSURE_VALID == result.CompStatus)
						{ // �õ���Ч�������
							/* ���油�����ѹ��ֵ */
							rscdrrm020ndse3->pressure_comp = result.f32PressureOutput * (1000 * 1000 * 10); // �Ŵ�10^7��
							
							/* invoke callback */
							if (rscdrrm020ndse3->parent.rx_indicate != RT_NULL)
							{
								rscdrrm020ndse3->parent.rx_indicate(&rscdrrm020ndse3->parent, 4);
							}
						}
					}
				}
			}
			else //if (RSCDRRM020NDSE3_TEMPERATURE == rscdrrm020ndse3->mode) // temperature
			{
				rscdrrm020ndse3->mode = RSCDRRM020NDSE3_PRESSURE; // �л�ģʽ
				rscdrrm020ndse3_unlock(rscdrrm020ndse3);
				
				/* ��ȡ�������¶����ݲ��л�Ϊѹ���ɼ�ģʽ */
				uint8_t send_buf[4] = {0};
				uint8_t recv_buf[4] = {0};
				send_buf[0] = 0xFF;
				send_buf[1] = 0x40 | ((0x01 & 0x03) << 2) | ((1 - 1) & 0x03); // WREG command [0100 RRNN],
				send_buf[2] = RSCDRRM020NDSE3_ADC_PRESSURE | RSCDRRM020NDSE3_ADC_FREQ | RSCDRRM020NDSE3_ADC_MODE; // mode
				send_buf[3] = 0xFF;
				ret = rscdrrm020ndse3_spi_adc_transfer(send_buf, recv_buf, sizeof(send_buf));
				if (RT_EOK == ret)
				{
					/* 
						�����¶�����
						Temperature data are output starting with MSB. When reading 24 bits, the first 14 bits are used
						to indicate the temperature measurement result. The last 10 bits are random data and must be ignored. Negative temperature
						is represented in 2��s complement format. MSB = 0 indicates positive result, MSB = 1 indicates negative value.
					*/
					temperature = ((((uint32_t)recv_buf[0] << 8) & 0x0000FF00)
						| (((uint32_t)recv_buf[1]) & 0x000000FF)) >> 2;
				}
			}
		}
	}
}

/* initialize the interface */
static rt_err_t rscdrrm020ndse3_init(rt_device_t dev)
{
	struct rscdrrm020ndse3_device* rscdrrm020ndse3 = (struct rscdrrm020ndse3_device*)dev;
	RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_init()\r\n");
	
	/* ����EVENT */
	rscdrrm020ndse3_event = rt_event_create("rscdrrm020ndse3", RT_IPC_FLAG_FIFO);
	if (RT_NULL == rscdrrm020ndse3_event)
	{
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_init() call rt_event_create(rscdrrm020ndse3) failed!\r\n");
        return -RT_ERROR;
	}
	
	/* �����������������߳� */ 
    rscdrrm020ndse3_thread = rt_thread_create("rscdrrm020ndse3", rscdrrm020ndse3_thread_entry, (void*)rscdrrm020ndse3, 1024, 5, 10); 
    if(RT_NULL == rscdrrm020ndse3_thread)
    {
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_init() call rt_thread_create(rscdrrm020ndse3) failed!\r\n");
        return -RT_ERROR;
    }
    rt_thread_startup(rscdrrm020ndse3_thread);
	
	/* ������������Դ */
	rscdrrm020ndse3_power_on(dev);
	
	/* ���仺�������ڴ洢EEPROM�е����в��� */
	uint8_t* eeprom_buf = (uint8_t*)rt_malloc(RSCDRRM020NDSE3_EEPROM_SIZE);
	if (RT_NULL == eeprom_buf)
	{
		/* �رմ�������Դ */
		rscdrrm020ndse3_power_off(dev);
	
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_init() call rt_malloc(RSCDRRM020NDSE3_EEPROM_SIZE) failed!\r\n");
		return -RT_ENOMEM;
	}
	
	/* ��ȡEEPROM�������� */
	rt_err_t ret = rscdrrm020ndse3_spi_read_eeprom(0x00, eeprom_buf, RSCDRRM020NDSE3_EEPROM_SIZE);
	if (RT_EOK != ret)
    {
		/* �ͷ���Դ */
		rt_free(eeprom_buf);
	
		/* �رմ�������Դ */
		rscdrrm020ndse3_power_off(dev);
		
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_init() call rscdrrm020ndse3_spi_read_eeprom(0,%d) failed, error(%d)!\r\n", 
			RSCDRRM020NDSE3_EEPROM_SIZE, ret);
		return ret;
    }
	
	/* ʹ��EEPROM�еĲ�����ʼ������������ */
	CompStatus_Enum comp_ret = Compensate_Pressure_Init(eeprom_buf);
	if (COMPINIT_OK != comp_ret)
	{
		/* �ͷ���Դ */
		rt_free(eeprom_buf);
	
		/* �رմ�������Դ */
		rscdrrm020ndse3_power_off(dev);
		
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_init() call Compensate_Pressure_Init(eeprom_buf) failed, error(%d)!\r\n", comp_ret);
		return -RT_ERROR;
	}
	
	/* 
		����ADC����
		Initialize all four configuration registers to the default values in the EEPROM��s Relative addresses 61, 63, 65 and 67
	*/
	rscdrrm020ndse3->adc_cfg_param[0] = eeprom_buf[61];
	rscdrrm020ndse3->adc_cfg_param[1] = eeprom_buf[63];
	rscdrrm020ndse3->adc_cfg_param[2] = eeprom_buf[65];
	rscdrrm020ndse3->adc_cfg_param[3] = eeprom_buf[67];
	
	/* �ͷ���Դ */
	rt_free(eeprom_buf);
	
	/* �رմ�������Դ */
	rscdrrm020ndse3_power_off(dev);
	
	/* �������״̬ */
	rscdrrm020ndse3->start = false;
	
	/* ������ */
	rscdrrm020ndse3->pressure_comp = 0.0;
	
    return RT_EOK;
}

/* control the interface */
static rt_err_t rscdrrm020ndse3_control(rt_device_t dev, int cmd, void *args)
{
	RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_control() cmd=0x%x\r\n", cmd);
	
	rt_err_t ret = RT_EOK;
	
    switch (cmd)
    {
		case RSCDRRM020NDSE3_START:
		{
			/* �������������ݲɼ� */
			ret = rscdrrm020ndse3_start(dev);
			break;
		}
		case RSCDRRM020NDSE3_STOP:
		{
			/* ֹͣ���������ݲɼ� */
			ret = rscdrrm020ndse3_stop(dev);
			break;
		}
		case RSCDRRM020NDSE3_AUTO_ZERO:
		{
			ATUO_ZERO_CPL_FUNC pfnAutoZeroCompleted = (ATUO_ZERO_CPL_FUNC)args;
			/* �����Զ����� */
			ret = rscdrrm020ndse3_auto_zero(dev, pfnAutoZeroCompleted);
			break;
		}
		default:
		{
			ret = -RT_ERROR;
			RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_control() failed, unsupported cmd(0x%x)!\r\n", cmd);
			break;
		}
    }

    return ret;
}

/* Open the ethernet interface */
static rt_err_t rscdrrm020ndse3_open(rt_device_t dev, uint16_t oflag)
{
	RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_open() oflag=0x%x\r\n", oflag);
	
	struct rscdrrm020ndse3_device* rscdrrm020ndse3 = (struct rscdrrm020ndse3_device*)dev;
	
	/* ������������Դ */
	rscdrrm020ndse3_power_on(dev);
	
	/* ���ô���������READY�жϼ�� */
	rt_err_t ret = rt_pin_attach_irq(RSCDRRM020NDSE3_RDY_PIN, PIN_IRQ_MODE_FALLING, rscdrrm020ndse3_rdy_isr, rscdrrm020ndse3);
	if (RT_EOK != ret)
	{
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_open() call rt_pin_attach_irq(RSCDRRM020NDSE3_RDY_PIN) failed, error(%d)!\r\n", ret);
		return ret;
	}
	
	/* ȷ������������READY�жϼ�⴦��DISABLE״̬ */
	ret = rt_pin_irq_enable(RSCDRRM020NDSE3_RDY_PIN, PIN_IRQ_DISABLE);
	if (RT_EOK != ret)
	{
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_open() call rt_pin_irq_enable(RSCDRRM020NDSE3_RDY_PIN, PIN_IRQ_DISABLE) failed, error(%d)!\r\n", ret);
		return ret;
	}
	
	rscdrrm020ndse3_lock(dev);
	
	/* ��ʼʱΪֹͣ״̬ */
	rscdrrm020ndse3->start = false;
	
	/* ������ */
	rscdrrm020ndse3->pressure_comp = 0.0;
	
	rscdrrm020ndse3_unlock(dev);
	
    return RT_EOK;
}

/* Close the interface */
static rt_err_t rscdrrm020ndse3_close(rt_device_t dev)
{
	RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_close()\r\n");
	
	struct rscdrrm020ndse3_device* rscdrrm020ndse3 = (struct rscdrrm020ndse3_device*)dev;
	
	rscdrrm020ndse3_lock(dev);
	
	/* ����ֹͣ״̬ */
	rscdrrm020ndse3->start = false;
	/* ������ */
	rscdrrm020ndse3->pressure_comp = 0.0;
	
	rscdrrm020ndse3_unlock(dev);
	
	/* ֹͣ����������READY�жϼ�� */
	rt_pin_irq_enable(RSCDRRM020NDSE3_RDY_PIN, PIN_IRQ_DISABLE);
	
	/* ж�ش���������READY�жϼ�� */
	rt_pin_detach_irq(RSCDRRM020NDSE3_RDY_PIN);
	
	/* �رմ�������Դ */
	rscdrrm020ndse3_power_off(dev);
	
    return RT_EOK;
}

/* Read */
static rt_size_t rscdrrm020ndse3_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
	if (size < 4)
	{
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3 read data buffer size must >= 4!\r\n");
		return 0;
	}
	
	struct rscdrrm020ndse3_device* rscdrrm020ndse3 = (struct rscdrrm020ndse3_device*)dev;
		
	rt_memcpy(buffer, &(rscdrrm020ndse3->pressure_comp), 4);
	
	return 4;
}

#ifdef RT_USING_DEVICE_OPS
const static struct rt_device_ops rscdrrm020ndse3_ops = 
{
    rscdrrm020ndse3_init,
    rscdrrm020ndse3_open,
    rscdrrm020ndse3_close,
    rscdrrm020ndse3_read,
    RT_NULL,
    rscdrrm020ndse3_control
};
#endif

int rscdrrm020ndse3_hw_init(void)
{
    RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_hw_init()\r\n");
	
	/* �����ͳ�ʼ��SPI EEPROM�豸(CS_EE pin: PC3) */
	rt_err_t ret = rt_hw_spi_device_attach(RSCDRRM020NDSE3_SPI_BUS_NAME, 
		RSCDRRM020NDSE3_SPI_EE_DEVICE_NAME, GPIOC, GPIO_PIN_3);
    if (RT_EOK != ret)
    {
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_hw_init() call rt_hw_spi_device_attach(%s, %s) failed, error(%d)!\r\n", 
			RSCDRRM020NDSE3_SPI_BUS_NAME, RSCDRRM020NDSE3_SPI_EE_DEVICE_NAME, ret);
		return ret;
    }
		
	struct rt_spi_device* spi_ee_device = (struct rt_spi_device*)rt_device_find(RSCDRRM020NDSE3_SPI_EE_DEVICE_NAME);
    if (RT_NULL == spi_ee_device)
    {
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_hw_init() call rt_device_find(%s) failed!\r\n", 
			RSCDRRM020NDSE3_SPI_EE_DEVICE_NAME);
		return -RT_ERROR;
    }

    /* config eeprom spi device */
    {
		/* EEPROM operates in SPI mode 0 where CPOL = 0 and CPHA = 0 (0,0) and mode 3 where CPOL = 1 and CPHA = 1 (1,1) */
		struct rt_spi_configuration cfg = {0};
		cfg.data_width = 8;
		cfg.mode = RT_SPI_MODE_0 | RT_SPI_MSB; /* SPI Compatible Modes 0 (CPOL = 0, CPHA = 0) */
		cfg.max_hz = 5 * 1000 * 1000; /* SPI Interface with Clock Speeds Up to 5 MHz */
		ret = rt_spi_configure(spi_ee_device, &cfg);
		if (RT_EOK != ret)
		{
			RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_hw_init() call rt_spi_configure(spi_ee_device) failed, error(%d)!\r\n", ret);
			return ret;
		}
    }
		
	/* save eeprom spi device */
	rscdrrm020ndse3_dev.spi_ee_device = spi_ee_device;
	
	/* �����ͳ�ʼ��SPI ADC�豸(CS_ADC pin: PC2) */
	ret = rt_hw_spi_device_attach(RSCDRRM020NDSE3_SPI_BUS_NAME, 
		RSCDRRM020NDSE3_SPI_ADC_DEVICE_NAME, GPIOC, GPIO_PIN_2);
    if (RT_EOK != ret)
    {
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_hw_init() call rt_hw_spi_device_attach(%s, %s) failed, error(%d)!\r\n", 
			RSCDRRM020NDSE3_SPI_BUS_NAME, RSCDRRM020NDSE3_SPI_ADC_DEVICE_NAME, ret);
		return ret;
    }
		
	struct rt_spi_device* spi_adc_device = (struct rt_spi_device*)rt_device_find(RSCDRRM020NDSE3_SPI_ADC_DEVICE_NAME);
    if (RT_NULL == spi_adc_device)
    {
		RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_hw_init() call rt_device_find(%s) failed!\r\n", 
			RSCDRRM020NDSE3_SPI_ADC_DEVICE_NAME);
		return -RT_ERROR;
    }

    /* config adc spi device */
    {
		/* The ADC interface operates in SPI mode 1 where CPOL = 0 and CPHA = 1 */
		struct rt_spi_configuration cfg = {0};
		cfg.data_width = 8;
		cfg.mode = RT_SPI_MODE_1 | RT_SPI_MSB; /* SPI Compatible Modes 1 (CPOL = 0, CPHA = 1) */
		cfg.max_hz = 5 * 1000 * 1000; /* SPI Interface with Clock Speeds Up to 5 MHz */
		ret = rt_spi_configure(spi_adc_device, &cfg);
		if (RT_EOK != ret)
		{
			RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_hw_init() call rt_spi_configure(spi_adc_device) failed, error(%d)!\r\n", ret);
			return ret;
		}
    }
		
	/* save adc spi device */
	rscdrrm020ndse3_dev.spi_adc_device = spi_adc_device;
		
	/* init rt-thread device struct */
    rscdrrm020ndse3_dev.parent.type    = RT_Device_Class_Miscellaneous;
#ifdef RT_USING_DEVICE_OPS
    rscdrrm020ndse3_dev.parent.ops     = &rscdrrm020ndse3_ops;
#else
    rscdrrm020ndse3_dev.parent.init    = rscdrrm020ndse3_init;
    rscdrrm020ndse3_dev.parent.open    = rscdrrm020ndse3_open;
    rscdrrm020ndse3_dev.parent.close   = rscdrrm020ndse3_close;
    rscdrrm020ndse3_dev.parent.read    = rscdrrm020ndse3_read;
    rscdrrm020ndse3_dev.parent.write   = RT_NULL;
    rscdrrm020ndse3_dev.parent.control = rscdrrm020ndse3_control;
#endif
	
	/* set POWER_EN pin mode to output */
	rt_pin_write(RSCDRRM020NDSE3_PWER_EN_PIN, PIN_LOW); // ��ʼ��ʱ�رմ�������Դ
    rt_pin_mode(RSCDRRM020NDSE3_PWER_EN_PIN, PIN_MODE_OUTPUT);
	
	/* set RDY pin mode to input */
    rt_pin_mode(RSCDRRM020NDSE3_RDY_PIN, PIN_MODE_INPUT_PULLUP);
	
	/* ADC�������� */
	rt_memset(rscdrrm020ndse3_dev.adc_cfg_param, 0, sizeof(rscdrrm020ndse3_dev.adc_cfg_param));
	
	/* �������״̬ */
	rscdrrm020ndse3_dev.start = false;
	
	/* ������ */
	rscdrrm020ndse3_dev.pressure_comp = 0.0;
	
	/* init lock */
	rt_mutex_init(&(rscdrrm020ndse3_dev.lock), "rscdrrm020ndse3", RT_IPC_FLAG_FIFO);
	
	/* register a character device */
    ret = rt_device_register(&(rscdrrm020ndse3_dev.parent), RSCDRRM020NDSE3_DEVICE_NAME, 
		RT_DEVICE_FLAG_STANDALONE | RT_DEVICE_FLAG_RDONLY | RT_DEVICE_FLAG_INT_RX);
	if (RT_EOK != ret)
    {
        RSCDRRM020NDSE3_TRACE("rscdrrm020ndse3_hw_init() call rt_device_register(%s) failed, error(%d)!\r\n", 
			RSCDRRM020NDSE3_DEVICE_NAME, ret);
		return ret;
    }

    return RT_EOK;
}
INIT_DEVICE_EXPORT(rscdrrm020ndse3_hw_init);

#endif /* BSP_USING_RSCDRRM020NDSE3 */
