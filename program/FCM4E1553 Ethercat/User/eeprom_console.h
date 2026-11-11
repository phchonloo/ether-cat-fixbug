/**
 * @file    eeprom_console.h
 * @brief   EEPROM命令解析接口声明
 * @details 在前台解析串口命令，并调用EEPROM及采样查询接口。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    串口接口的现有声明依赖保持原样。
 */
#ifndef USER_EEPROM_CONSOLE_H
#define USER_EEPROM_CONSOLE_H
#include "N32G45XSYS.h"
/** @brief Clear the USART1 command line buffer. */
void eeprom_console_init(void);

/**
 * @brief Process USART1 commands for the 24C02 from the foreground loop.
 * @note Commands are executed only after CR or LF is received.
 */
void eeprom_console_process(void);
#endif
