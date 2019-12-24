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
	/* 采样率(索引)[0=10HZ 1=20HZ 2=22.5HZ 3=45HZ 4=87.5HZ 5=90HZ 6=165HZ 7=175HZ 8=300HZ 9=330HZ 10=500HZ 11=600HZ 12=1000HZ]*/
	uint32_t freq_index;
	/* lock */
	struct rt_mutex lock;
};

/* 自动归零完成回调函数类型定义 */
typedef void (*ATUO_ZERO_CPL_FUNC)(void);

/* 传感器设备名 */
#define RSCDRRM020NDSE3_DEVICE_NAME "rscm020"

/* rt_device_control cmd code */
#define RSCDRRM020NDSE3_START (0x01) // 启动
#define RSCDRRM020NDSE3_STOP (0x02) // 停止
#define RSCDRRM020NDSE3_AUTO_ZERO (0x03) // 自动归零
#define RSCDRRM020NDSE3_SET_FREQ (0x04) // 设置采样率
#define RSCDRRM020NDSE3_GET_FREQ (0x05) // 读取采样率

#ifdef __cplusplus
}
#endif

#endif // __RSCDRRM020NDSE3_H__
