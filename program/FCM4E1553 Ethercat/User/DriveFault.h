/**
 * @file    DriveFault.h
 * @brief   驱动故障管理接口声明
 * @details 汇总采样、电流和通信故障，并维护故障锁存与复位条件。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    故障原因、上报编码及复位判定保持原实现。
 */
#ifndef DRIVE_FAULT_H
#define DRIVE_FAULT_H
#include <stdint.h>
/* Bench thresholds selected for the user's 24 V supply, NOT board ratings.
 * Boot self-test is a separate bounded current-only operation. */
#define DF_UNDER_MV 18000U
#define DF_OVER_MV 30000U
#define DF_ENABLE_OVERCURRENT 1U
#define DF_ENABLE_OPEN_PHASE 2U
#define DF_REASON_VFO 1U
#define DF_REASON_OVERCURRENT 2U
#define DF_REASON_PHASE_A 3U
#define DF_REASON_PHASE_B 4U
#define DF_REASON_UNDERVOLTAGE 5U
#define DF_REASON_OVERVOLTAGE 6U
#define DF_REASON_SAMPLING 7U
#define DF_REASON_FOREGROUND 8U
#define DF_REASON_ZERO 9U
#define DF_REASON_SELF_TEST 10U
#define DF_RESET_INVALID_VOLTAGE 0x0001U
#define DF_RESET_VOLTAGE_WINDOW  0x0002U
#define DF_RESET_RECOVERY_TIME   0x0004U
#define DF_RESET_VFO_ACTIVE      0x0008U
#define DF_RESET_VFO_HISTORY     0x0010U
#define DF_RESET_FOREGROUND_OLD  0x0020U
#define DF_RESET_INVALID_PHASES  0x0040U
#define DF_RESET_PHASE_CURRENT   0x0080U
/* Watch phaseBlockMask: global gates in bits 0..6; per-phase gates 7..10. */
#define DF_PHASE_DISABLED       0x0001U
#define DF_PHASE_NOT_ENABLED    0x0002U
#define DF_PHASE_NOT_HOLD       0x0004U
#define DF_PHASE_MOVING         0x0008U
#define DF_PHASE_INVALID_SAMPLE 0x0010U
#define DF_PHASE_REFERENCE_STEP 0x0020U
#define DF_PHASE_SETTLING       0x0040U
#define DF_PHASE_A_LOW_REF      0x0080U
#define DF_PHASE_B_LOW_REF      0x0100U
#define DF_PHASE_A_CURRENT_OK   0x0200U
#define DF_PHASE_B_CURRENT_OK   0x0400U
#define DF_PHASE_FAULT_LATCHED  0x0800U
#define DF_PHASE_TICK_GAP       0x1000U
typedef struct {
    uint16_t currentEnable; /* 0x2020: bit0 overcurrent, bit1 stationary phase test */
    uint16_t overcurrentMa; /* 0x2021: per-phase absolute current, NOT vector/RMS */
} DriveFaultConfig;
typedef struct {
    uint32_t version, nowMs, lastUpdateMs, updateCount;
    uint32_t underMs, overMs, currentMs, phaseAMs, phaseBMs, invalidMs, healthyMs, settleMs;
    uint32_t firstValid, firstCode, firstReason, firstMs;
    uint32_t firstStatus, firstSample, firstBusMv;
    int32_t firstPhaseAMa, firstPhaseBMa;
    int32_t firstReferenceA, firstReferenceB;
    uint32_t faultSequence, resetRequestCount, resetAcceptedCount, lastResetBlockMask;
    uint32_t maxUpdateGapMs;
    uint32_t adcVoltageHealthyMs;
    uint32_t phaseBlockMask, phaseUpdateCount, phaseMaxGapMs, phaseSampleCount;
    uint32_t phaseReferenceAMa, phaseReferenceBMa, phaseInvalidMs;
    int32_t phaseMeasuredAMa, phaseMeasuredBMa;
} DriveFaultDiagnostics;
extern DriveFaultConfig gDriveFaultConfig;
extern volatile DriveFaultDiagnostics gDriveFaultDiag;
extern volatile uint16_t gDriveFaultReason; /* 0x2030, first cause; 0 means none */
extern volatile uint16_t gDriveFaultResetReady; /* 0x2031, live reset permission */
extern volatile uint16_t gDriveFaultResetBlockMask; /* 0x2032, bitwise motor reset inhibitors */
void DriveFault_Init(void);
void DriveFault_Tick(void); /* once per 16 kHz TIM8 interrupt, including faults */
void DriveFault_PhaseTick(void); /* after the actual current reference, TIM8 */
void DriveFault_Update(void); /* after telemetry, in foreground */
void DriveFault_ObserveVoltageSample(uint16_t rawBus, uint8_t valid, uint32_t cycle);
void DriveFault_ObserveAdcError(void);
uint8_t DriveFault_CanReset(void);
uint16_t DriveFault_GetResetBlockMask(void);
void DriveFault_OnReset(void);
#endif
