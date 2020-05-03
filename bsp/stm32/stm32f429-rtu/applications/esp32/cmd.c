/****************************************************************************
 *
 * File Name
 *  cmd.c
 * Author
 *  
 * Date
 *  2020/05/01
 * Descriptions:
 * 串口命令解析模块接口实现
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

#include "config.h"
#include "cmd.h"
#include "strref.h"
#include "common.h"
#include "util.h"
#include "app.h"

/**---------------------------------------------------------------------------*
 **                            Debugging Flag                                 *
 **---------------------------------------------------------------------------*/
#define LOG_TAG              "cmd"
#define LOG_LVL              LOG_LVL_DBG
#include <rtdbg.h>

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
#define CMD_PACKET_MAX_LEN (512)
// CMD Packet起始字节
#define CMD_PACKET_HEAD_BYTE ((uint8_t)'[')
// CMD Packet结束字节
#define CMD_PACKET_TAIL_BYTE ((uint8_t)']')
#define JSON_DATA_BUF_LEN (APP_MP_BLOCK_SIZE)

/*----------------------------------------------------------------------------*
**                             Data Structures                                *
**----------------------------------------------------------------------------*/
// CMD packet buffer status
typedef enum
{
    CMD_PACKET_EMPTY = 0, // 空状态
    CMD_PACKET_HEAD, // 收到包头状态
    CMD_PACKET_COMPLETED // 收到完整包状态
} CMD_PACKET_BUF_STATUS_E;

// CMD packet buffer
typedef struct
{
    CMD_PACKET_BUF_STATUS_E ePacketStatus;
    uint32_t u32PacketLen;
    uint8_t pu8PacketBuf[CMD_PACKET_MAX_LEN];
} CmdPacketBuf_T;

// Cmd Handler Function Type Define
typedef void (*CMD_HANDLER_FUNC)(const c_str_ref* pctStrRefParam);
// Cmd Handler表项结构体
typedef struct
{
    const char* pcszCmdName; // CMD名
    uint32_t u32CmdNameLen; // CMD名长度
    CMD_HANDLER_FUNC handlerFunc; // CMD处理函数
} CmdHandlerFunc_T;

/*----------------------------------------------------------------------------*
**                             Local Vars                                     *
**----------------------------------------------------------------------------*/
static cmd_resp_func cmd_response_output = NULL;

// CMD packet buffer
static CmdPacketBuf_T s_tCmdPacketBuf = {CMD_PACKET_EMPTY};

/* 
        命令格式
            读取命令：[CMD]
            设置命令：[CMD=Param]
*/
// 命令执行函数(声明)
static void _CMD_HandlerDATANUM(const c_str_ref* pctStrRefParam);
static void _CMD_HandlerDATARD(const c_str_ref* pctStrRefParam);
// 命令和执行函数对应表
const static CmdHandlerFunc_T s_tCmdHandlerTbl[] = {
    { STR_ITEM("DATANUM"), _CMD_HandlerDATANUM }, /* 读取历史数据条数 */
    { STR_ITEM("DATARD"), _CMD_HandlerDATARD }, /* 读取指定历史数据 */
};

/*----------------------------------------------------------------------------*
**                             Extern Function                                *
**----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*
**                             Local Function                                 *
**----------------------------------------------------------------------------*/
/*************************************************
* Function: _CMD_AssemblePacket
* Description: 聚合数据流, 按CMD Packet格式进行分包提取
* Author: 
* Returns: 返回已处理数据长度(字节数)
* Parameter:
* History:
*************************************************/
static uint32_t _CMD_AssemblePacket(const uint8_t* pcu8Data, uint32_t u32DataLen)
{
#define pu8PacketBuf s_tCmdPacketBuf.pu8PacketBuf
#define u32PacketLen s_tCmdPacketBuf.u32PacketLen
#define ePacketStatus s_tCmdPacketBuf.ePacketStatus

    uint32_t i = 0;
    for (i = 0;
        (i < u32DataLen) 
        && (CMD_PACKET_COMPLETED != ePacketStatus)
        && (u32PacketLen < sizeof(pu8PacketBuf)); ++i)
    {
        uint8_t u8Data = pcu8Data[i];

        switch (ePacketStatus)
        {
            case CMD_PACKET_EMPTY:
                if (CMD_PACKET_HEAD_BYTE == u8Data)
                {
                    ePacketStatus = CMD_PACKET_HEAD;
                    pu8PacketBuf[u32PacketLen++] = u8Data;
                }
                break;
            case CMD_PACKET_HEAD:
                if (CMD_PACKET_TAIL_BYTE == u8Data)
                {
                    ePacketStatus = CMD_PACKET_COMPLETED;
                }
                pu8PacketBuf[u32PacketLen++] = u8Data;
                break;
            case CMD_PACKET_COMPLETED:
            default:
                break;
        }
    }

#undef pu8PacketBuf
#undef u32PacketLen
#undef ePacketStatus

    return i;
}

/*************************************************
* Function: _CMD_Response
* Description: 命令处理结果应答
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
static void _CMD_Response(const char* pcszFmt, ...)
{
    if (!cmd_response_output)
    {
        return;
    }
    
    char szCmdRspBuf[CMD_PACKET_MAX_LEN] = "";
    int iCmdRspLen = 0;
    va_list ap;
    va_start(ap, pcszFmt);
    iCmdRspLen = vsnprintf(szCmdRspBuf, sizeof(szCmdRspBuf), pcszFmt, ap);
    va_end(ap);
    if ((iCmdRspLen > 0) && (iCmdRspLen <= (sizeof(szCmdRspBuf) - 2)))
    {
        szCmdRspBuf[iCmdRspLen++] = '\r';
        szCmdRspBuf[iCmdRspLen++] = '\n';
        cmd_response_output((const uint8_t*)szCmdRspBuf, (uint32_t)iCmdRspLen);
    }
}

/*************************************************
* Function: _CMD_GetHandlerFunc
* Description: 取得CMD处理函数
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
static CMD_HANDLER_FUNC _CMD_GetHandlerFunc(const c_str_ref* pctStrRefParam)
{
    if (strref_is_empty(pctStrRefParam))
    {
        return NULL;
    }
    
    CMD_HANDLER_FUNC handlerFunc = NULL;
    /* 查找并处理指令 */
    uint32_t u32TblLen = ARRAY_SIZE(s_tCmdHandlerTbl);
    uint32_t i = 0;
    for (i = 0; i < u32TblLen; ++i)
    { // 查找Cmd对应的处理函数
        /* 严格比较长度和内容 */
        uint32_t u32CmdNameLen = s_tCmdHandlerTbl[i].u32CmdNameLen;
        if ((u32CmdNameLen == pctStrRefParam->len)
            && (0 == memcmp(s_tCmdHandlerTbl[i].pcszCmdName, pctStrRefParam->c_str, u32CmdNameLen)))
        {
            handlerFunc = s_tCmdHandlerTbl[i].handlerFunc;
            break;
        }
    }
    
    return handlerFunc;
}

/*************************************************
* Function: _CMD_PacketProcess
* Description: CMD Packet处理函数
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
static void _CMD_PacketProcess(const uint8_t* pcu8Packet, uint32_t u32PacketLen)
{
    // 参数检查
    if ((NULL == pcu8Packet) || (u32PacketLen < 2))
    {
        return;
    }
    
    // Packet内容(去除包头、包尾)
    c_str_ref tPacketContent = {u32PacketLen - 2, (const char*)(pcu8Packet + 1)};
    c_str_ref tStrRefSplitList[2] = {{0, NULL}, };
    /* 分解命令和参数部分 */
    uint32_t u32SplitListLen = strref_split(&tPacketContent, '=', tStrRefSplitList, ARRAY_SIZE(tStrRefSplitList));
    if (0 == u32SplitListLen)
    { // Packet 格式错误
        _CMD_Response("[ERR]");
        return;
    }
    
    // 找到CMD处理函数
    CMD_HANDLER_FUNC handlerFunc = _CMD_GetHandlerFunc(&(tStrRefSplitList[0]));
    if (!handlerFunc)
    { // 命令不能识别
        _CMD_Response("[ERR]");
        return;
    }
    
    // 处理CMD
    switch (u32SplitListLen)
    {
        case 1: // 没有参数
        {
            handlerFunc(NULL);
            break;
        }
        case 2: // 有参数
        {
            handlerFunc(&(tStrRefSplitList[1]));
            break;
        }
        default: // Packet 格式错误
            _CMD_Response("[ERR]");
            break;
    }
}

/*************************************************
* Function: _CMD_HandlerDATANUM
* Description: DATANUM命令处理函数
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
static void _CMD_HandlerDATANUM(const c_str_ref* pctStrRefParam)
{
    if (NULL == pctStrRefParam)
    { // 读取
        _CMD_Response("[DATANUM=%u]", get_history_data_num());
    }
    else
    { // 设置
        // 只读属性,不允许设置
        _CMD_Response("[ERR]");
    }
}

/*************************************************
* Function: _CMD_HandlerDATARD
* Description: DATARD命令处理函数
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
static void _CMD_HandlerDATARD(const c_str_ref* pctStrRefParam)
{
    if (NULL == pctStrRefParam)
    { // 读取
        _CMD_Response("[ERR]");
    }
    else
    { // 执行
        int n = strref_to_int(pctStrRefParam);
        if (n < 0)
        {
            _CMD_Response("[ERR]");
            return;
        }
        
        char* json_data_buf = (char*)app_mp_alloc();
        RT_ASSERT(json_data_buf != NULL)
    
        /* 读取前n个时刻的一条历史数据(JSON格式)  */
        uint32_t read_len = read_history_data_json(n, json_data_buf, JSON_DATA_BUF_LEN, true);
        
        /* 输出JSON数据 */
        if (read_len > 0)
        { // 读取到数据
            _CMD_Response("[DATARD=%s]", json_data_buf);
        }
        else
        {
            _CMD_Response("[DATARD=]");
        }
    
        app_mp_free(json_data_buf);
        json_data_buf = NULL;
    }
}

/*************************************************
* Function: _CMD_ClearPacketBuf
* Description: 清空CMD Packet Buffer
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
static void _CMD_ClearPacketBuf(void)
{
    //memset(&s_tCmdPacketBuf, 0, sizeof(s_tCmdPacketBuf));
    s_tCmdPacketBuf.ePacketStatus = CMD_PACKET_EMPTY;
    s_tCmdPacketBuf.u32PacketLen = 0;
}

/*************************************************
* Function: _CMD_OnRecvData
* Description: 收到串口命令数据时调用该函数
* Author: 
* Returns: 返回已处理字节数
* Parameter:
* History:
*************************************************/
uint32_t _CMD_OnRecvData(const uint8_t* pcu8Data, uint32_t u32DataLen)
{
    uint32_t u32ProcessedLen = _CMD_AssemblePacket(pcu8Data, u32DataLen);
    if (CMD_PACKET_COMPLETED == s_tCmdPacketBuf.ePacketStatus)
    { // CMD缓冲区收到完整Packet
        /* Process CMD Packet */
        _CMD_PacketProcess(s_tCmdPacketBuf.pu8PacketBuf, s_tCmdPacketBuf.u32PacketLen);
        /* 清空Packet Buffer */
        _CMD_ClearPacketBuf();
        /* 已完成接收 */
    }
    else if (s_tCmdPacketBuf.u32PacketLen >= sizeof(s_tCmdPacketBuf.pu8PacketBuf))
    { // CMD Packet长度超过最大长度, 缓冲区已满
        /* 丢弃缓冲区中的数据 */
        // [TODO]
        /* 清空缓冲区,复位状态 */
        _CMD_ClearPacketBuf();
        /* 输出警告 */
        LOG_W("_CMD_NORMALOnRecvData() warning, CMD packet buffer is full!");
        /* 已完成接收 */
    }
    else
    { // 需要继续在缓冲区累积数据
        /* 未完成接收 */
    }
    
    return u32ProcessedLen;
}

/*----------------------------------------------------------------------------*
**                             Public Function                                *
**----------------------------------------------------------------------------*/
/*************************************************
* Function: cmd_init
* Description: 初始化CMD模块
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
void cmd_init(cmd_resp_func resp_func)
{
    cmd_response_output = resp_func;
    
    // 清空PacketBuffer
    cmd_clear_packet_buf();
}

/*************************************************
* Function: cmd_clear_packet_buf
* Description: 清空Packet Buffer
* Author: 
* Returns:
* Parameter:
* History:
*************************************************/
void cmd_clear_packet_buf(void)
{
    _CMD_ClearPacketBuf();
}

/*************************************************
* Function: cmd_input
* Description: 输入字节流
* Author: 
* Returns: 
* Parameter:
* History:
*************************************************/
void cmd_input(const uint8_t* data, uint32_t data_len)
{
    /* 已处理数据长度 */
    uint32_t u32ProcessedLen = 0;
    /* 循环处理所有接收到的数据 */
    while (u32ProcessedLen < data_len)
    {
        u32ProcessedLen += _CMD_OnRecvData((data + u32ProcessedLen), 
            (data_len - u32ProcessedLen));
    }
}

/**---------------------------------------------------------------------------*
 **                         Compiler Flag                                     *
 **---------------------------------------------------------------------------*/
#ifdef   __cplusplus
}
#endif
// End of cmd.c
