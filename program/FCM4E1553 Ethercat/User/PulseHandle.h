/**
 * @file    PulseHandle.h
 * @brief   脉冲命令处理接口声明
 * @details 处理现有脉冲命令，并初始化与厂商电机库共享的运行状态。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    脉冲单位、方向及共享变量保持原实现。
 */
#ifndef _PulseHandle_H_
#define _PulseHandle_H_
#include "mcuinit.h"
void DataIni(void);
void PulseFunction(void);
void PuIoIntHandle(void);
void DrIoIntHandle(void);

void PulseSet(void);
void WorkSelf(void);

extern s16 Mean_i, Dir_x;
extern s32 xifen, SpeedNow, BaseRpm;
#endif
