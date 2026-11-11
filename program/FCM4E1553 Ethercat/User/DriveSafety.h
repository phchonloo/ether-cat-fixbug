/**
 * @file    DriveSafety.h
 * @brief   驱动保护接口声明
 * @details 处理前台与中断心跳、供电失效以及方向限位相关状态。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-03
 * @note    看门狗喂养条件与直接输出关闭序列保持原实现。
 */
#ifndef DRIVE_SAFETY_H
#define DRIVE_SAFETY_H
#include <stdint.h>
#include "DriveTelemetry.h"
#define DS_VERSION 0x20261111UL
#define DS_REASON_NEGATIVE_LIMIT 11U
#define DS_REASON_POSITIVE_LIMIT 12U
#define DS_REASON_BOTH_LIMITS 13U
#define DS_REASON_LOGIC_SUPPLY 14U
#define DS_REASON_FAST_BUS_LOSS 15U
#define DS_RESET_LOGIC_SUPPLY 0x0100U
#define DS_RESET_BOTH_LIMITS 0x0200U
typedef struct {
    uint32_t version, resetFlags, restartInhibit, rearmCount;
    uint32_t motorTicks, systickTicks, watchdogFeeds, watchdogStarted;
    uint32_t powerLossLatched, busLowMs, limitMask, limitStopping;
    uint32_t limitTrips, limitReason;
} DriveSafetyDiagnostics;
extern volatile DriveSafetyDiagnostics gDriveSafetyDiag;
void DriveSafety_Init(void);
void DriveSafety_StartWatchdog(void);
void DriveSafety_Update(void); /* sole watchdog reload site, at end of foreground */
void DriveSafety_SysTick(void);
void DriveSafety_MotorTick(void);
void DriveSafety_OnValidPdo(uint16_t control); /* successful RxPDO mapping only */
void DriveSafety_OnReset(void);
void DriveSafety_RequireRearm(void);
uint8_t DriveSafety_PowerAllowed(void);
void DriveSafety_ObserveBus(const DriveSafetySample *sample);
uint16_t DriveSafety_ResetBlockMask(void);
void DriveSafety_PowerFail(uint16_t reason);
void DriveSafety_FatalOutputOff(void);
uint8_t DriveSafety_CheckLimit(int32_t requested, int32_t applied);
void DriveSafety_FinishLimit(void);
#endif
