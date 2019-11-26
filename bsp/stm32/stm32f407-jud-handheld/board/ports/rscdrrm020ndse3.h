#ifndef __RSCDRRM020NDSE3_H__
#define __RSCDRRM020NDSE3_H__

#include <rtthread.h>
#include <rthw.h>
#include <rtdevice.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum rscdrrm020ndse3_mode
{
	RSCDRRM020NDSE3_PRESSURE = 0, // 压力采集模式
	RSCDRRM020NDSE3_TEMPERATURE // 温度采集模式
};

struct rscdrrm020ndse3_device
{
    /* inherit from rt_device */
    struct rt_device parent;

    /* spi eeprom device */
    struct rt_spi_device* spi_ee_device;
	/* spi adc device */
    struct rt_spi_device* spi_adc_device;
	/* adc config param(read from eeprom) */
	uint8_t adc_cfg_param[4];
	/* start(0=stop,1=start) */
	bool start;
	/* 当前采集模式(0=pressure,1=temperature) */
	enum rscdrrm020ndse3_mode mode;
	/* 压力值(已补偿) */
	float pressure_comp;
	/* 自动归零请求标志 */
	bool auto_zero;
	/* lock */
	struct rt_mutex lock;
};

/* 传感器设备名 */
#define RSCDRRM020NDSE3_DEVICE_NAME "rscm020"

/* rt_device_control cmd code */
#define RSCDRRM020NDSE3_START (0x01) // 启动
#define RSCDRRM020NDSE3_STOP (0x02) // 停止
#define RSCDRRM020NDSE3_AUTO_ZERO (0x03) // 自动归零

#ifdef __cplusplus
}
#endif

#endif // __RSCDRRM020NDSE3_H__
