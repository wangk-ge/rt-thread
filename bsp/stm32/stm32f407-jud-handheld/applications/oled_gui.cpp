/****************************************************************************
 *
 * File Name
 *  oled_gui.cpp
 * Author
 *  wangk
 * Date
 *  2020/09/29
 * Descriptions:
 * OLED屏幕界面功能实现
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

#include "app.h"
#include "U8g2lib.h"

/**---------------------------------------------------------------------------*
 **                            Debugging Flag                                 *
 **---------------------------------------------------------------------------*/
//#define OLED_GUI_DEBUG
#ifdef OLED_GUI_DEBUG
    #define OLED_GUI_TRACE rt_kprintf
#else
    #define OLED_GUI_TRACE(...)
#endif /* OLED_GUI_DEBUG */

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
#define OLED_POWER_PIN         GET_PIN(A, 2) // PA2

#define OLED_SPI_PIN_RES       GET_PIN(A, 4)  // PA4
#define OLED_SPI_PIN_DC        GET_PIN(A, 6)  // PA6
#define OLED_SPI_PIN_CS        GET_PIN(A, 3)  // PA3

// In u8x8.h #define U8X8_USE_PINS 
#define U8G2_PIN_UP            GET_PIN(E, 2)      // PE2(KEY3)
#define U8G2_PIN_DOWN          GET_PIN(E, 3)      // PE3(KEY2)
#define U8G2_PIN_LEFT          U8X8_PIN_NONE
#define U8G2_PIN_RIGHT         GET_PIN(E, 4)      // PE4(KEY1)
#define U8G2_PIN_SELECT        GET_PIN(E, 5)      // PE5(KEY0)
#define U8G2_PIN_HOME          U8X8_PIN_NONE

#define OLED_GUI_THREAD_PRIORITY        25
#define OLED_GUI_THREAD_STACK_SIZE      512
#define OLED_GUI_THREAD_TIMESLICE       5

/*----------------------------------------------------------------------------*
**                             Data Structures                                *
**----------------------------------------------------------------------------*/
typedef struct
{
    uint32_t width;
    uint32_t height;
    const uint8_t* bits;
} icon_info_t;

/*----------------------------------------------------------------------------*
**                             Local Vars                                     *
**----------------------------------------------------------------------------*/
static U8G2_SH1107_PIMORONI_128X128_F_4W_HW_SPI u8g2(U8G2_R0, 
    /* cs=*/ OLED_SPI_PIN_CS, /* dc=*/ OLED_SPI_PIN_DC, /* reset=*/ OLED_SPI_PIN_RES);

#define DISCON_ICON_WIDTH 22
#define DISCON_ICON_HEIGHT 22
static const uint8_t discon_icon_bits[] = {
   0xff, 0xff, 0xff, 0xff, 0xff, 0xe7, 0xff, 0xff, 0xe3, 0xff, 0x0f, 0xf0, 
   0xff, 0x07, 0xf8, 0xff, 0xe3, 0xf8, 0xff, 0xf1, 0xf9, 0xff, 0xf1, 0xf9, 
   0xff, 0xe1, 0xf9, 0x3f, 0xc0, 0xf8, 0x1f, 0x08, 0xfc, 0x0f, 0x08, 0xfe, 
   0xc7, 0x20, 0xff, 0xe7, 0xe3, 0xff, 0xe7, 0xe7, 0xff, 0xe7, 0xe3, 0xff, 
   0xc7, 0xf1, 0xff, 0x07, 0xf8, 0xff, 0x03, 0xfc, 0xff, 0xf1, 0xff, 0xff, 
   0xf9, 0xff, 0xff, 0xff, 0xff, 0xff, 
};

#define BLE_ICON_WIDTH 24
#define BLE_ICON_HEIGHT 24
static const uint8_t ble_icon_bits[] = {
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x8f, 0xff, 0xff, 0x0f, 0xff, 
   0x7f, 0x0e, 0xfe, 0x7f, 0x0c, 0xfc, 0x7f, 0x48, 0xf8, 0xff, 0xc0, 0xf8, 
   0xff, 0x41, 0xf8, 0xff, 0x03, 0xfc, 0xff, 0x07, 0xfe, 0xff, 0x0f, 0xff, 
   0xff, 0x07, 0xfe, 0xff, 0x03, 0xfc, 0xff, 0x41, 0xf8, 0xff, 0xc0, 0xf8, 
   0x7f, 0x48, 0xf8, 0x7f, 0x0c, 0xfc, 0x7f, 0x0e, 0xfe, 0xff, 0x0f, 0xff, 
   0xff, 0x8f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
};

#define USB_ICON_WIDTH 24
#define USB_ICON_HEIGHT 24
static const uint8_t usb_icon_bits[] = {
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
   0xff, 0xff, 0xff, 0xff, 0x0f, 0xff, 0xff, 0x01, 0xff, 0xff, 0x00, 0xff, 
   0xff, 0x08, 0xff, 0x71, 0xfc, 0xff, 0x60, 0xfc, 0xcf, 0x00, 0x00, 0x00, 
   0x00, 0x00, 0x00, 0xc0, 0xf1, 0x8f, 0xe0, 0xf3, 0xcf, 0xff, 0x23, 0xf8, 
   0xff, 0x07, 0xf8, 0xff, 0x07, 0xf8, 0xff, 0x0f, 0xf8, 0xff, 0x3f, 0xf8, 
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
};

#define ENV_ICON_WIDTH 36
#define ENV_ICON_HEIGHT 36
static const uint8_t env_icon_bits[] = {
   0xff, 0x3f, 0xe0, 0xff, 0x0f, 0xff, 0x1f, 0xc0, 0xff, 0x0f, 0xff, 0x0f,
   0x80, 0xff, 0x0f, 0xff, 0x87, 0x0f, 0xff, 0x0f, 0xff, 0xc7, 0x1f, 0xfe,
   0x0f, 0xff, 0xe3, 0x3f, 0xfe, 0x0f, 0xff, 0xe3, 0x3f, 0xc0, 0x0f, 0xff,
   0xe3, 0x3f, 0xc0, 0x0f, 0xff, 0xe3, 0x3f, 0xfe, 0x0f, 0xff, 0xe3, 0x3f,
   0xfe, 0x0f, 0xff, 0xe3, 0x3f, 0xc0, 0x0f, 0xff, 0xe3, 0x3f, 0xc0, 0x0f,
   0xff, 0xe3, 0x38, 0xfe, 0x0f, 0xff, 0xe3, 0x38, 0xfe, 0x0f, 0xff, 0xe3,
   0x38, 0xc0, 0x0f, 0xff, 0xe3, 0x38, 0xc0, 0x0f, 0xff, 0xe3, 0x38, 0xfe,
   0x0f, 0xff, 0xe3, 0x38, 0xfe, 0x0f, 0xff, 0xe3, 0x38, 0xfe, 0x0f, 0xff,
   0xf1, 0x78, 0xfc, 0x0f, 0xff, 0xf0, 0x78, 0xfc, 0x0f, 0xff, 0x78, 0xf0,
   0xf8, 0x0f, 0x7f, 0x3c, 0xe0, 0xf0, 0x0f, 0x7f, 0x1c, 0xc0, 0xf1, 0x0f,
   0x7f, 0x1c, 0xc0, 0xf1, 0x0f, 0x7f, 0x1c, 0xc0, 0xf1, 0x0f, 0x7f, 0x1c,
   0xc0, 0xf1, 0x0f, 0x7f, 0x1c, 0xc0, 0xf1, 0x0f, 0x7f, 0x3c, 0xe0, 0xf0,
   0x0f, 0xff, 0x78, 0xf0, 0xf8, 0x0f, 0xff, 0xf0, 0x7f, 0xfc, 0x0f, 0xff,
   0xe1, 0x3f, 0xfc, 0x0f, 0xff, 0x83, 0x0f, 0xfe, 0x0f, 0xff, 0x07, 0x00,
   0xff, 0x0f, 0xff, 0x0f, 0x80, 0xff, 0x0f, 0xff, 0x3f, 0xe0, 0xff, 0x0f
};

#define INFO_ICON_WIDTH 36
#define INFO_ICON_HEIGHT 36
static const uint8_t info_icon_bits[] = {
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 
   0x80, 0xff, 0xff, 0xff, 0x07, 0x00, 0xfe, 0xff, 0xff, 0x01, 0x00, 0xf8, 
   0xff, 0xff, 0x00, 0x00, 0xf0, 0xff, 0x3f, 0x00, 0x00, 0xe0, 0xff, 0x3f, 
   0x00, 0x06, 0xc0, 0xff, 0x1f, 0x00, 0x0f, 0x80, 0xff, 0x0f, 0x00, 0x0f, 
   0x00, 0xff, 0x0f, 0x00, 0x06, 0x00, 0xff, 0x07, 0x00, 0x00, 0x00, 0xfe, 
   0x07, 0x00, 0x00, 0x00, 0xfe, 0x03, 0x00, 0x00, 0x00, 0xfc, 0x03, 0x00, 
   0x00, 0x00, 0xfc, 0x03, 0x00, 0x00, 0x00, 0xfc, 0x03, 0x00, 0x06, 0x00, 
   0xfc, 0x03, 0x00, 0x0f, 0x00, 0xfc, 0x03, 0x00, 0x0f, 0x00, 0xfc, 0x03, 
   0x00, 0x0f, 0x00, 0xfc, 0x03, 0x00, 0x0f, 0x00, 0xfc, 0x03, 0x00, 0x0f, 
   0x00, 0xfc, 0x03, 0x00, 0x0f, 0x00, 0xfc, 0x07, 0x00, 0x0f, 0x00, 0xfe, 
   0x07, 0x00, 0x0f, 0x00, 0xfe, 0x0f, 0x00, 0x0f, 0x00, 0xff, 0x0f, 0x00, 
   0x0f, 0x00, 0xff, 0x1f, 0x00, 0x0f, 0x80, 0xff, 0x3f, 0x00, 0x06, 0xc0, 
   0xff, 0x3f, 0x00, 0x00, 0xe0, 0xff, 0xff, 0x00, 0x00, 0xf0, 0xff, 0xff, 
   0x01, 0x00, 0xf8, 0xff, 0xff, 0x07, 0x00, 0xfe, 0xff, 0xff, 0x1f, 0x80, 
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
};

/*----------------------------------------------------------------------------*
**                             Extern Function                                *
**----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*
**                             Local Function                                 *
**----------------------------------------------------------------------------*/
/*************************************************
* Function: oled_gui_draw_status_bar
* Description: 画状态栏
* Author: wangk
* Returns: 
* Parameter:
* History:
*************************************************/
static void oled_gui_draw_status_bar(void)
{
    /* 画电池图标 */
    u8g2.drawFrame(8, 8, 24, 12);
    u8g2.drawVLine(33, 12, 5);
    u8g2.setFont(u8g2_font_7x14_tn);
    u8g2.drawStr(12, 8, "85");
    
    /* 画连接状态图标 */
    icon_info_t icon_infos[] = {
        {DISCON_ICON_WIDTH, DISCON_ICON_HEIGHT, discon_icon_bits},
        {BLE_ICON_WIDTH, BLE_ICON_HEIGHT, ble_icon_bits},
        {USB_ICON_WIDTH, USB_ICON_HEIGHT, usb_icon_bits},
    };
    icon_info_t* icon = &(icon_infos[0]);
    u8g2.drawXBMP(114 - (icon->width / 2), 14 - (icon->height / 2), 
        icon->width, icon->height, icon->bits);
}

/*************************************************
* Function: oled_gui_draw_icon_menu
* Description: 画图标菜单
* Author: wangk
* Returns: 
* Parameter:
* History:
*************************************************/
static void oled_gui_draw_icon_menu(void)
{
    /* 画图标 */
    icon_info_t icon_infos[] = {
        {ENV_ICON_WIDTH, ENV_ICON_HEIGHT, env_icon_bits},
        {INFO_ICON_WIDTH, INFO_ICON_HEIGHT, info_icon_bits},
    };
    icon_info_t* icon = &(icon_infos[0]);
    u8g2.drawXBMP((128 / 2) - (icon->width / 2), (128 / 2) - (icon->height / 2), 
        icon->width, icon->height, icon->bits);
    
    /* 画文字 */
    //u8g2.setFont(u8g2_font_7x14_tn);
    //u8g2.drawStr(42, 8, "85");
}

/*************************************************
* Function: oled_gui_thread_entry
* Description: OLED GUI线程入口函数
* Author: wangk
* Returns: 
* Parameter:
* History:
*************************************************/
static void oled_gui_thread_entry(void *parameter)
{
    u8g2.begin(/*Select=*/ U8G2_PIN_SELECT, 
        /*Right/Next=*/ U8G2_PIN_RIGHT, /*Left/Prev=*/ U8G2_PIN_LEFT, 
        /*Up=*/ U8G2_PIN_UP, /*Down=*/ U8G2_PIN_DOWN, 
        /*Home/Cancel=*/ U8G2_PIN_HOME);
    u8g2.setContrast(128);

    while(1)
    {
        u8g2.clearBuffer();
        oled_gui_draw_status_bar();
        oled_gui_draw_icon_menu();
        u8g2.sendBuffer();
        
        rt_thread_mdelay(10);
        
        int8_t event = u8g2.getMenuEvent();
        
        if ( event == U8X8_MSG_GPIO_MENU_NEXT )
        {
            
        }
        
        if ( event == U8X8_MSG_GPIO_MENU_PREV )
        {
            
        }
        
        if ( event == U8X8_MSG_GPIO_MENU_SELECT )
        {
            
        }
    }
}
    
/*----------------------------------------------------------------------------*
**                             Public Function                                *
**----------------------------------------------------------------------------*/
/*************************************************
* Function: oled_gui_init
* Description: 初始化OLED GUI模块
* Author: wangk
* Returns: 
* Parameter:
* History:
*************************************************/
void oled_gui_init(void)
{
    OLED_GUI_TRACE("oled_gui_init()\n");
    // TODO
}

/*************************************************
* Function: oled_gui_start
* Description: 启动OLED GUI模块
* Author: wangk
* Returns: 
* Parameter:
* History:
*************************************************/
void oled_gui_start(void)
{
    OLED_GUI_TRACE("oled_gui_start()\n");
    
    /* 开启OLED电源 */
    rt_pin_mode(OLED_POWER_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(OLED_POWER_PIN, PIN_HIGH);
    
    rt_thread_t oled_gui_thread = rt_thread_create("oled_gui",
        oled_gui_thread_entry, RT_NULL,
        OLED_GUI_THREAD_STACK_SIZE,
        OLED_GUI_THREAD_PRIORITY, 
        OLED_GUI_THREAD_TIMESLICE);

    if (oled_gui_thread == RT_NULL)
    {
        OLED_GUI_TRACE("oled_gui_start() failed, call rt_thread_create(oled_gui) error!\n");
    }
    else
    {
        rt_thread_startup(oled_gui_thread);
    }
}

/**---------------------------------------------------------------------------*
 **                         Compiler Flag                                     *
 **---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif
// End of oled_gui.cpp
