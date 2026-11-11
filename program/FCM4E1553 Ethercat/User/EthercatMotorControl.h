/**
 * @file    EthercatMotorControl.h
 * @brief   EtherCAT运动控制接口声明
 * @details 根据CiA402轴状态与模式目标生成脉冲指令，并包含回零及位置累计实现。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    累计位置来自电气指令计数，不是轴端编码器反馈。
 */
#ifndef USER_ETHERCAT_MOTOR_CONTROL_H
#define USER_ETHERCAT_MOTOR_CONTROL_H
#include "N32G45XSYS.h"
/* Existing motor-control calibration: BaseRpm = rpm * 16. */
#define ECAT_MOTOR_BASE_UNITS_PER_RPM  16L
/* EtherCAT velocity and position modes share this absolute speed limit. */
#define ECAT_MOTOR_MAX_RPM             1000L
#define ECAT_MOTOR_DEFAULT_PROFILE_RPM 100L
/* Default and maximum configurable linear command ramps, in rpm/s. */
#define ECAT_MOTOR_ACCEL_RPM_PER_SEC   3000L
#define ECAT_MOTOR_DECEL_RPM_PER_SEC   3000L
#define ECAT_MOTOR_MAX_RAMP_RPM_PER_SEC 10000L
/* TIM8 calls the motor algorithm at 16 kHz. */
#define ECAT_MOTOR_CONTROL_HZ          16000L
extern volatile s32 gEcatBaseRpmCommand;
extern volatile u8 gEcatMotorEnabled;

typedef struct
{
    s32 countedPosition;
    s32 targetPosition;
    s32 resolution;
    s32 lastPhaseStep;
    s32 requestedPhaseStep;
    uint32_t countedCycles;
    uint32_t skippedCycles;
    uint32_t mismatchCount;
    uint32_t positiveStepCycles;
    uint32_t negativeStepCycles;
    uint32_t blockedAwayCount; /* Legacy diagnostic; no per-PDO hard clamp in new CSP. */
    uint32_t maxStepDelta;
    uint8_t atTarget;
    uint8_t positionLimit;
    uint8_t resolutionChanged;
    uint32_t cspAlgorithmVersion; /* Watch-only: 0x20261028 = cascaded CSP filters. */
} TEcatPulseDiagnostics;
extern volatile TEcatPulseDiagnostics gEcatPulseDiag;

void EthercatMotorControl_Init(void);
void EthercatMotorControl_Update(void);
/* Latch one coherent setpoint after SM RxPDO mapping or on DC Sync0. */
void EthercatMotorControl_OnSyncCycle(void);
/* Emitted phase/ramp standstill only; NOT a measured shaft standstill. */
u8 EthercatMotorControl_IsStopped(void);
/* Final gate for every PWM write, including writes from the vendor library. */
u8 EthercatMotorControl_PowerAllowed(void);
/* ISR-local motion demand, including fractional CSP and pending PP targets. */
u8 EthercatMotorControl_CurrentMotionActive(void);

/* Fixed-rate ramp and feedback helpers called from the 16 kHz motor ISR. */
s32 EthercatMotorControl_ApplyRamp(s32 target_base_rpm);
void EthercatMotorControl_EmergencyStop(void);
/* Called around MainFunt: prepare a bounded step, then count actual PhaseGet. */
s32 EthercatMotorControl_NextPhaseStep(void);
void EthercatMotorControl_BeginPhaseCycle(s32 angle);
void EthercatMotorControl_EndPhaseCycle(s32 angle);
#endif
