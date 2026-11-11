/**
 * @file    MotionPID.h
 * @brief   运动PID兼容声明接口声明
 * @details 保留旧运动控制接口及其共享声明。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    现有声明与厂商库的归属关系不在本阶段调整。
 */
#ifndef _MotionPID_H_
#define _MotionPID_H_
#include "mcuinit.h"
void shuzu_initial(void);
void PhaseGet(s16 inputrpm);
void CurrentCal(void);
void MotorWork(s32 inputIa, s32 inputIb);
void MotorFunc(void);
long LimitAm(long indata, long am);
s32 PassLow(s32 ts, s32 indata[], s32 outdata[]);
void MainFunt(void);

void ad_inital(void);


extern s32 CountFlag;
extern s32 Iangle, xifen;
extern long Am;

extern u8 ready_ad, EnaFlag;

extern s32 TestSpeed, SpeedStar, WorkSpeedMax;
#endif
