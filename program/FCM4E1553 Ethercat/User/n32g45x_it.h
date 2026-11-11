/**
 * @file    n32g45x_it.h
 * @brief   内核异常与系统节拍接口声明
 * @details 提供Cortex-M4异常入口及SysTick心跳处理。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    异常输出关闭及死循环等待行为保持原实现。
 */
#ifndef N32G45X_IT_H
#define N32G45X_IT_H
#include "N32G45XSYS.h"
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);
#endif
