#ifndef RT_CONFIG_H__
#define RT_CONFIG_H__

/* Automatically generated file; DO NOT EDIT. */
/* RT-Thread Configuration */

/* RT-Thread Kernel */

#define RT_NAME_MAX 16
#define RT_ALIGN_SIZE 4
#define RT_THREAD_PRIORITY_32
#define RT_THREAD_PRIORITY_MAX 32
#define RT_TICK_PER_SECOND 1000
#define RT_USING_OVERFLOW_CHECK
#define RT_USING_HOOK
#define RT_USING_IDLE_HOOK
#define RT_IDLE_HOOK_LIST_SIZE 4
#define IDLE_THREAD_STACK_SIZE 1024
#define RT_DEBUG

/* Inter-Thread communication */

#define RT_USING_SEMAPHORE
#define RT_USING_MUTEX
#define RT_USING_EVENT
#define RT_USING_MAILBOX
#define RT_USING_MESSAGEQUEUE

/* Memory Management */

#define RT_USING_MEMPOOL
#define RT_USING_MEMHEAP
#define RT_USING_MEMHEAP_AS_HEAP
#define RT_USING_HEAP

/* Kernel Device Object */

#define RT_USING_DEVICE
#define RT_USING_CONSOLE
#define RT_CONSOLEBUF_SIZE 128
#define RT_CONSOLE_DEVICE_NAME "uart1"
#define RT_VER_NUM 0x40003
#define ARCH_ARM
#define RT_USING_CPU_FFS
#define ARCH_ARM_CORTEX_M
#define ARCH_ARM_CORTEX_M4

/* RT-Thread Components */

#define RT_USING_COMPONENTS_INIT
#define RT_USING_USER_MAIN
#define RT_MAIN_THREAD_STACK_SIZE 2048
#define RT_MAIN_THREAD_PRIORITY 10

/* C++ features */


/* Command shell */


/* Device virtual file system */

#define RT_USING_DFS
#define DFS_USING_WORKDIR
#define DFS_FILESYSTEMS_MAX 2
#define DFS_FILESYSTEM_TYPES_MAX 2
#define DFS_FD_MAX 16
#define RT_USING_DFS_DEVFS

/* Device Drivers */

#define RT_USING_DEVICE_IPC
#define RT_PIPE_BUFSZ 512
#define RT_USING_SYSTEM_WORKQUEUE
#define RT_SYSTEM_WORKQUEUE_STACKSIZE 2048
#define RT_SYSTEM_WORKQUEUE_PRIORITY 23
#define RT_USING_SERIAL
#define RT_SERIAL_USING_DMA
#define RT_SERIAL_RB_BUFSZ 512
#define RT_USING_PIN
#define RT_USING_RTC
#define RT_USING_SPI
#define RT_USING_SFUD
#define RT_SFUD_USING_SFDP
#define RT_SFUD_USING_FLASH_INFO_TABLE
#define RT_SFUD_SPI_MAX_HZ 50000000

/* Using USB */


/* POSIX layer and C standard library */

#define RT_USING_LIBC
#define RT_USING_POSIX
#define RT_USING_POSIX_TERMIOS

/* Network */

/* Socket abstraction layer */

#define RT_USING_SAL

/* protocol stack implement */

#define SAL_USING_AT
#define SAL_USING_POSIX

/* Network interface device */

#define RT_USING_NETDEV
#define NETDEV_USING_IFCONFIG
#define NETDEV_USING_PING
#define NETDEV_USING_NETSTAT
#define NETDEV_USING_AUTO_DEFAULT
#define NETDEV_IPV4 1
#define NETDEV_IPV6 0

/* light weight TCP/IP stack */


/* AT commands */

#define RT_USING_AT
#define AT_DEBUG
#define AT_USING_SERVER
#define AT_SERVER_DEVICE "uart1"
#define AT_SERVER_RECV_BUFF_LEN 256
#define AT_CMD_END_MARK_CRLF
#define AT_USING_CLIENT
#define AT_CLIENT_NUM_MAX 1
#define AT_USING_SOCKET
#define AT_PRINT_RAW_CMD
#define AT_CMD_MAX_LEN 2048
#define AT_SW_VERSION_NUM 0x10300

/* VBUS(Virtual Software BUS) */


/* Utilities */

#define RT_USING_ULOG
#define ULOG_OUTPUT_LVL_D
#define ULOG_OUTPUT_LVL 7
#define ULOG_USING_ISR_LOG
#define ULOG_ASSERT_ENABLE
#define ULOG_LINE_BUF_SIZE 2048

/* log format */

#define ULOG_OUTPUT_FLOAT
#define ULOG_OUTPUT_TIME
#define ULOG_TIME_USING_TIMESTAMP
#define ULOG_OUTPUT_LEVEL
#define ULOG_OUTPUT_TAG
#define ULOG_OUTPUT_THREAD_NAME
#define ULOG_BACKEND_USING_CONSOLE
#define ULOG_USING_FILTER

/* RT-Thread online packages */

/* IoT - internet of things */

#define MQTT_DEBUG
#define PKG_USING_WEBCLIENT
#define WEBCLIENT_DEBUG
#define WEBCLIENT_USING_FILE_DOWMLOAD
#define WEBCLIENT_NOT_USE_TLS
#define PKG_USING_WEBCLIENT_LATEST_VERSION
#define PKG_WEBCLIENT_VER_NUM 0x99999
#define PKG_USING_MYMQTT
#define PKG_USING_MYMQTT_LATEST_VERSION
#define MQTT_MAX_MESSAGE_HANDLERS 5
#define PKG_USING_JSMN
#define PKG_USING_JSMN_LATEST_VERSION

/* jsmn Options */

#define PKG_USING_LIBMODBUS
#define PKG_USING_LIBMODBUS_RTU
#define HAVE_DECL_TIOCSRS485 1
#define HAVE_DECL_TIOCM_RTS 1
#define PKG_USING_LIBMODBUS_LATEST_VERSION
#define PKG_LIBMODBUS_VER_NUM 0x99999

/* Wi-Fi */

/* Marvell WiFi */


/* Wiced WiFi */

#define PKG_USING_AT_DEVICE
#define PKG_USING_AT_DEVICE_LATEST_VERSION
#define PKG_AT_DEVICE_VER_NUM 0x99999

/* IoT Cloud */

#define PKG_USING_OTA_DOWNLOADER
#define OTA_DOWNLOADER_DEBUG
#define PKG_USING_HTTP_OTA
#define PKG_HTTP_OTA_URL "http://xxx/xxx/rtthread.rbl"
#define PKG_USING_OTA_DOWNLOADER_LATEST_VERSION

/* security packages */

#define PKG_USING_TINYCRYPT
#define PKG_USING_TINYCRYPT_LATEST_VERSION
#define TINY_CRYPT_MD5

/* language packages */


/* multimedia packages */


/* tools packages */

#define PKG_USING_EASYFLASH
#define PKG_EASYFLASH_ENV
#define PKG_EASYFLASH_ENV_AUTO_UPDATE
#define PKG_EASYFLASH_ENV_VER_NUM 0
#define PKG_EASYFLASH_ERASE_GRAN 4096
#define PKG_EASYFLASH_WRITE_GRAN_1BIT
#define PKG_EASYFLASH_WRITE_GRAN 1
#define PKG_EASYFLASH_START_ADDR 0
#define PKG_EASYFLASH_DEBUG
#define PKG_USING_EASYFLASH_LATEST_VERSION
#define PKG_EASYFLASH_VER_NUM 0x99999

/* system packages */

#define PKG_USING_FAL
#define FAL_DEBUG_CONFIG
#define FAL_DEBUG 1
#define FAL_PART_HAS_TABLE_CFG
#define FAL_USING_SFUD_PORT
#define FAL_USING_NOR_FLASH_DEV_NAME "W25Q128"
#define PKG_USING_FAL_LATEST_VERSION
#define PKG_FAL_VER_NUM 0x99999

/* peripheral libraries and drivers */


/* miscellaneous packages */


/* samples: kernel and components samples */

#define SOC_FAMILY_STM32
#define SOC_SERIES_STM32F4

/* Hardware Drivers Config */

#define SOC_STM32F429VG

/* Onboard Peripheral Drivers */

#define BSP_USING_RS232
#define BSP_USING_SPI_FLASH
#define AT_DEVICE_USING_TB22
#define AT_DEVICE_TB22_INIT_ASYN
#define TB22_POWER_PIN 33
#define TB22_STATUS_PIN -1
#define TB22_DEVICE_NAME "tb22"
#define TB22_CLIENT_NAME "uart3"
#define TB22_RECV_BUFF_LEN 2048
#define KAWAII_MQTT_USE_SAL

/* On-chip Peripheral Drivers */

#define BSP_USING_GPIO
#define BSP_USING_UART
#define BSP_USING_UART1
#define BSP_UART1_RX_USING_DMA
#define BSP_USING_UART2
#define BSP_UART2_RX_USING_DMA
#define BSP_USING_UART3
#define BSP_UART3_RX_USING_DMA
#define BSP_USING_UART5
#define BSP_UART5_RX_USING_DMA
#define BSP_USING_UART6
#define BSP_UART6_RX_USING_DMA
#define BSP_USING_UART7
#define BSP_UART7_RX_USING_DMA
#define BSP_USING_UART8
#define BSP_UART8_RX_USING_DMA
#define BSP_USING_ON_CHIP_FLASH
#define BSP_USING_SPI
#define BSP_USING_SPI1
#define BSP_SPI1_TX_USING_DMA
#define BSP_SPI1_RX_USING_DMA
#define BSP_USING_ONCHIP_RTC
#define BSP_RTC_USING_LSE

/* Board extended module Drivers */


#endif
