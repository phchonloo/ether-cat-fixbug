/**
 * @file    main.h
 * @brief   应用启动与主循环接口声明
 * @details 按现有顺序初始化外设及驱动，并轮询EtherCAT、通信和保护服务。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    PWM启动与看门狗使能时序保持原实现。
 */
#ifndef USER_MAIN_H
#define USER_MAIN_H
#include "N32G45XSYS.h"
void rcu_config(void);
void led_spark(void);
#endif
