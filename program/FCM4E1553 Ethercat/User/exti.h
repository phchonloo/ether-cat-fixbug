/**
 * @file    exti.h
 * @brief   EtherCAT外部中断配置接口声明
 * @details 配置ESC中断及同步信号的外部中断线路。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    中断屏蔽、清除及优先级关系保持原实现。
 */
#ifndef USER_EXTI_H
#define USER_EXTI_H
#include "N32G45XSYS.h"
/* This board wires ESC IRQ and SYNC0 only; PB2/SYNC1 is not connected. */
#define ECAT_SYNC1_IRQ_ENABLED 0U
/** @brief Configure PB0/EXTI0 for the active-low ESC IRQ signal. */
void exti_init_irq(void);

/** @brief Configure PB1/EXTI1 for the active-low EtherCAT SYNC0 signal. */
void exti_init_sync0(void);

/** @brief Configure SYNC1 if wired; otherwise mask and clear EXTI2/NVIC. */
void exti_init_sync1(void);
#endif
