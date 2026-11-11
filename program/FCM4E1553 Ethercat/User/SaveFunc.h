/**
 * @file    SaveFunc.h
 * @brief   旧参数表与启动指示接口声明
 * @details 保留厂商运动参数表和启动指示灯入口。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    未使用局部变量及旧接口保持原样。
 */
#ifndef _SaveFunc_H_
#define _SaveFunc_H_
#include "mcuinit.h"
void fmc_erase_pages(void);
void fmc_program(void);
void IniSaveFunc(void);

extern s32 sxifen;
extern u32 WorkValue[30];
extern u8 WolkMode;
#endif
