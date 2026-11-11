/**
 * @file    Errorhadle.h
 * @brief   错误锁存与指示接口声明
 * @details 衔接原厂错误码、驱动故障原因、复位入口及指示灯控制。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    保留现有文件名及工程引用。
 */
#ifndef _ERRORHADLE_H_
#define _ERRORHADLE_H_
#include "mcuinit.h"
void ErrorHandle(void);
void ErrorHandle_ReportFault(u8 vendor_code);
void ErrorHandle_ReportFaultReason(u8 vendor_code, u16 reason);
u8 ErrorHandle_TryReset(void);
void LedError(u8 ErrNum);

/* The first detected fault is latched until a safe CiA 402 fault reset. */
extern volatile u8 ErrNumFlag;
extern u16 u16VendorErrorCode;
extern u16 u16ErrorRegister;
extern s8 ErrI_sum;
#endif
