/**
 * @file    n32g45x_it.c
 * @brief   内核异常与系统节拍
 * @details 提供Cortex-M4异常入口及SysTick心跳处理。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-03
 * @note    异常输出关闭及死循环等待行为保持原实现。
 */
#include "n32g45x_it.h"
#include "systick.h"
#include "DriveSafety.h"
void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    DriveSafety_FatalOutputOff();
    while (1) {
    }
}

void MemManage_Handler(void)
{
    DriveSafety_FatalOutputOff();
    while (1) {
    }
}

void BusFault_Handler(void)
{
    DriveSafety_FatalOutputOff();
    while (1) {
    }
}

void UsageFault_Handler(void)
{
    DriveSafety_FatalOutputOff();
    while (1) {
    }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}
#if !SYSTEM_SUPPORT_OS
void SysTick_Handler(void)
{
    DriveSafety_SysTick();
    delay_decrement();
}
#endif
