/**
 * @file    DriveFault.c
 * @brief   驱动故障管理
 * @details 汇总采样、电流和通信故障，并维护故障锁存与复位条件。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    故障原因、上报编码及复位判定保持原实现。
 */
#include "DriveFault.h"
#include "DriveTelemetry.h"
#include "DriveCurrent.h"
#include "DriveBoot.h"
#include "DriveSafety.h"
#include "DriveParameters.h"
#include "EthercatMotorControl.h"
#include "Errorhadle.h"
#include "cia402appl.h"
#include "coeappl.h"
DriveFaultConfig gDriveFaultConfig = {DF_ENABLE_OPEN_PHASE, 4000U};
volatile DriveFaultDiagnostics gDriveFaultDiag;
volatile uint16_t gDriveFaultReason, gDriveFaultResetReady;
volatile uint16_t gDriveFaultResetBlockMask;
static volatile uint32_t sMs;
static uint32_t sSubTicks, sLedStart;
static uint8_t sInitialized, sLedCode, sLedLevel;
static volatile uint8_t sStartupDone;
static int32_t sReferenceA, sReferenceB;
static uint32_t sPhaseMs, sPhaseSample;
static volatile uint8_t sAdcVoltageSeen, sAdcVoltageGood;
static volatile uint32_t sAdcLastCycle, sAdcHealthyCycles, sAdcVref;
static uint32_t sSafetyMs, sSafetySequence;
static DriveTelemetryConfig sRecoveryConfig;
static DriveFaultConfig sRecoveryFaultConfig;
static void SafetyTick(const DriveSafetySample *v, uint32_t elapsed);

static uint32_t Magnitude(int32_t value)
{
    return (value < 0) ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
}

static void Qualify(volatile uint32_t *timer, uint8_t condition, uint32_t elapsed)
{
    if (!condition)
        *timer = 0U;
    else if (*timer < 1000U)
        *timer += elapsed;
}

void DriveFault_Init(void)
{
    memset((void *)&gDriveFaultDiag, 0, sizeof(gDriveFaultDiag));
    gDriveFaultDiag.version = 0x01080001UL;
    gDriveFaultConfig.currentEnable = DF_ENABLE_OPEN_PHASE;
    gDriveFaultConfig.overcurrentMa = 4000U;
    gDriveFaultReason = gDriveFaultResetReady = 0U;
    gDriveFaultResetBlockMask = DF_RESET_INVALID_VOLTAGE | DF_RESET_RECOVERY_TIME;
    sMs = sSubTicks = sLedStart = 0U;
    sLedCode = 0U;
    sLedLevel = 1U;
    sReferenceA = sReferenceB = 0;
    sPhaseMs = sPhaseSample = 0U;
    gDriveFaultDiag.phaseBlockMask = DF_PHASE_DISABLED;
    sStartupDone = 0U;
    sAdcVoltageSeen = sAdcVoltageGood = 0U;
    sAdcLastCycle = sAdcHealthyCycles = sAdcVref = 0U;
    sSafetyMs = sSafetySequence = 0U;
    sRecoveryConfig = gDriveTelemetryConfig;
    sRecoveryFaultConfig = gDriveFaultConfig;
    sInitialized = 1U;
}

void DriveFault_Tick(void)
{
    uint32_t pulses, phase;
    uint8_t code, level;
    if (++sSubTicks < 16U)
        return; /* fixed TIM8 rate: 16000 Hz */
    sSubTicks = 0U;
    ++sMs;
    gDriveFaultDiag.nowMs = sMs;
    if (sMs >= 500U)
        sStartupDone = 1U;
    /* A stopped foreground must not silently disable the software detectors.
     * This only watches foreground progress; it is not an independent HW watchdog. */
    if (sInitialized && sStartupDone &&
        ((uint32_t)(sMs - gDriveFaultDiag.lastUpdateMs) > 100U))
        ErrorHandle_ReportFaultReason(15U, DF_REASON_FOREGROUND);
    code = ErrNumFlag;
    if (code != sLedCode) {
        sLedCode = code;
        sLedStart = sMs;
    }
    pulses = (code == 10U) ? 1U : (code == 11U) ? 2U
        : (code == 14U)                         ? 3U
        : (code == 13U)                         ? 4U
                                                : 5U;
    phase = (uint32_t)(sMs - sLedStart) % (pulses * 1000U + 2000U);
    /* PA8 red: 500 ms on, 500 ms off, then 2 s off. Never touch Y1. */
    level = ((code != 0U) && (phase < pulses * 1000U) &&
                ((phase % 1000U) < 500U))
        ? 0U
        : 1U;
    if (level != sLedLevel) {
        LedErrSet(level);
        sLedLevel = level;
    }
}

/* Foreground mailbox processing may take longer than 5 ms. Phase qualification
 * belongs to the periodic motor path and reads coherent raw ADC data itself.
 * Check reference/motion changes every motor cycle; convert only once per ms. */
void DriveFault_PhaseTick(void)
{
    DrivePhaseSample sample;
    DriveSafetySample safety;
    uint32_t now, elapsed, mask = 0U, refA, refB;
    int32_t referenceA, referenceB;
    uint8_t valid, fresh;
    if (!sInitialized)
        return;
    now = sMs;
    if (now >= 500U)
        sStartupDone = 1U;
    if (now != sSafetyMs) {
        elapsed = now - sSafetyMs;
        sSafetyMs = now;
        DriveTelemetry_GetSafetySample(&safety);
        SafetyTick(&safety, elapsed);
        DriveBoot_Tick(&safety, elapsed);
    }
    if (ErrNumFlag) {
        gDriveFaultDiag.phaseBlockMask |= DF_PHASE_FAULT_LATCHED;
        return; /* retain the last reference/measurement and qualifying timers */
    }
    referenceA = gDriveCurrentDiag.phaseAReferenceCounts;
    referenceB = gDriveCurrentDiag.phaseBReferenceCounts;
    if (!(gDriveFaultConfig.currentEnable & DF_ENABLE_OPEN_PHASE))
        mask |= DF_PHASE_DISABLED;
    if (!gEcatMotorEnabled)
        mask |= DF_PHASE_NOT_ENABLED;
    if (gDriveCurrentDiag.state != DC_STATE_HOLD)
        mask |= DF_PHASE_NOT_HOLD;
    if (EthercatMotorControl_CurrentMotionActive())
        mask |= DF_PHASE_MOVING;
    if ((referenceA != sReferenceA) || (referenceB != sReferenceB))
        mask |= DF_PHASE_REFERENCE_STEP;
    sReferenceA = referenceA;
    sReferenceB = referenceB;
    if (mask) {
        gDriveFaultDiag.settleMs = 0U;
        gDriveFaultDiag.phaseAMs = gDriveFaultDiag.phaseBMs = 0U;
        gDriveFaultDiag.phaseBlockMask = mask;
    }
    now = sMs;
    elapsed = now - sPhaseMs;
    if (!elapsed)
        return;
    sPhaseMs = now;
    ++gDriveFaultDiag.phaseUpdateCount;
    if (elapsed > gDriveFaultDiag.phaseMaxGapMs)
        gDriveFaultDiag.phaseMaxGapMs = elapsed;
    if (elapsed > 5U) {
        mask |= DF_PHASE_TICK_GAP;
        gDriveFaultDiag.settleMs = 0U;
        gDriveFaultDiag.phaseAMs = gDriveFaultDiag.phaseBMs = 0U;
        gDriveFaultDiag.phaseInvalidMs = 0U;
        elapsed = 1U;
    }
    if (mask & DF_PHASE_DISABLED) {
        gDriveFaultDiag.phaseInvalidMs = 0U;
        gDriveFaultDiag.phaseBlockMask = mask;
        return;
    }
    if (mask & DF_PHASE_MOVING) {
        /* Open-phase qualification is hold-only. SafetyTick above still
         * checks voltage, overcurrent and missing ADC samples while moving.
         * Avoid the phase-current conversions and reference divisions here:
         * they cannot qualify a fault until the axis is stationary. */
        gDriveFaultDiag.phaseInvalidMs = 0U;
        gDriveFaultDiag.phaseBlockMask = mask;
        return;
    }
    valid = DriveTelemetry_GetPhaseSample(&sample);
    fresh = valid && (sample.sequence != sPhaseSample);
    sPhaseSample = sample.sequence;
    gDriveFaultDiag.phaseSampleCount = sample.sequence;
    gDriveFaultDiag.phaseMeasuredAMa = sample.aMa;
    gDriveFaultDiag.phaseMeasuredBMa = sample.bMa;
    if (!fresh)
        mask |= DF_PHASE_INVALID_SAMPLE;
    Qualify(&gDriveFaultDiag.phaseInvalidMs, sStartupDone && !valid, elapsed);
    if (gDriveFaultDiag.phaseInvalidMs >= 100U)
        ErrorHandle_ReportFaultReason(15U, DF_REASON_SAMPLING);
    refA = (uint32_t)((uint64_t)Magnitude(referenceA) *
        gDriveTelemetryConfig.vrefMv * 3000000U /
        ((uint64_t)(gDriveTelemetryConfig.sensitivityUvA ? gDriveTelemetryConfig.sensitivityUvA : 1U) * 8190U));
    refB = (uint32_t)((uint64_t)Magnitude(referenceB) *
        gDriveTelemetryConfig.vrefMv * 3000000U /
        ((uint64_t)(gDriveTelemetryConfig.sensitivityUvA ? gDriveTelemetryConfig.sensitivityUvA : 1U) * 8190U));
    gDriveFaultDiag.phaseReferenceAMa = refA;
    gDriveFaultDiag.phaseReferenceBMa = refB;
    Qualify(&gDriveFaultDiag.settleMs, mask == 0U, elapsed);
    if (gDriveFaultDiag.settleMs < 300U)
        mask |= DF_PHASE_SETTLING;
    if (refA < 500U)
        mask |= DF_PHASE_A_LOW_REF;
    if (refB < 500U)
        mask |= DF_PHASE_B_LOW_REF;
    if (valid && ((uint64_t)Magnitude(sample.aMa) * 5U >= refA))
        mask |= DF_PHASE_A_CURRENT_OK;
    if (valid && ((uint64_t)Magnitude(sample.bMa) * 5U >= refB))
        mask |= DF_PHASE_B_CURRENT_OK;
    Qualify(&gDriveFaultDiag.phaseAMs,
        !(mask & (0x007FU | DF_PHASE_TICK_GAP | DF_PHASE_A_LOW_REF | DF_PHASE_A_CURRENT_OK)), elapsed);
    Qualify(&gDriveFaultDiag.phaseBMs,
        !(mask & (0x007FU | DF_PHASE_TICK_GAP | DF_PHASE_B_LOW_REF | DF_PHASE_B_CURRENT_OK)), elapsed);
    gDriveFaultDiag.phaseBlockMask = mask;
    if (gDriveFaultDiag.phaseAMs >= 200U)
        ErrorHandle_ReportFaultReason(11U, DF_REASON_PHASE_A);
    else if (gDriveFaultDiag.phaseBMs >= 200U)
        ErrorHandle_ReportFaultReason(11U, DF_REASON_PHASE_B);
}

static uint8_t Healthy(const DriveSafetySample *v)
{
    if (!(v->status & DT_VALID_VOLTAGE) ||
        (v->busMv < DF_UNDER_MV + 1000U) ||
        (v->busMv > DF_OVER_MV - 1000U))
        return 0U;
    if (gDriveFaultConfig.currentEnable &&
        (!(v->status & DT_VALID_PHASES) ||
            (Magnitude(v->aMa) >= gDriveFaultConfig.overcurrentMa) ||
            (Magnitude(v->bMa) >= gDriveFaultConfig.overcurrentMa)))
        return 0U;
    return 1U;
}

static void SafetyTick(const DriveSafetySample *v, uint32_t elapsed)
{
    uint8_t fresh = v->sequence && v->sequence != sSafetySequence;
    uint8_t voltageValid, phasesValid;
    sSafetySequence = v->sequence;
    DriveSafety_ObserveBus(v);
    if (elapsed > 5U || !fresh ||
        memcmp(&sRecoveryConfig, &gDriveTelemetryConfig, sizeof(sRecoveryConfig)) ||
        memcmp(&sRecoveryFaultConfig, &gDriveFaultConfig, sizeof(sRecoveryFaultConfig))) {
        gDriveFaultDiag.underMs = gDriveFaultDiag.overMs = 0U;
        gDriveFaultDiag.currentMs = gDriveFaultDiag.healthyMs = 0U;
        elapsed = 1U;
    }
    sRecoveryConfig = gDriveTelemetryConfig;
    sRecoveryFaultConfig = gDriveFaultConfig;
    voltageValid = fresh && (v->status & DT_VALID_VOLTAGE);
    phasesValid = fresh && (v->status & DT_VALID_PHASES);
    Qualify(&gDriveFaultDiag.underMs, voltageValid && sStartupDone && v->busMv < DF_UNDER_MV, elapsed);
    Qualify(&gDriveFaultDiag.overMs, voltageValid && v->busMv > DF_OVER_MV, elapsed);
    Qualify(&gDriveFaultDiag.currentMs, phasesValid && (gDriveFaultConfig.currentEnable & DF_ENABLE_OVERCURRENT) && (Magnitude(v->aMa) > gDriveFaultConfig.overcurrentMa || Magnitude(v->bMa) > gDriveFaultConfig.overcurrentMa), elapsed);
    Qualify(&gDriveFaultDiag.invalidMs, sStartupDone && (!(v->status & DT_VALID_VOLTAGE) || (gDriveFaultConfig.currentEnable && !(v->status & DT_VALID_PHASES))), elapsed);
    Qualify(&gDriveFaultDiag.healthyMs, fresh && Healthy(v), elapsed);
    if (gDriveFaultDiag.overMs >= 10U)
        ErrorHandle_ReportFaultReason(14U, DF_REASON_OVERVOLTAGE);
    else if (gDriveFaultDiag.underMs >= 50U)
        ErrorHandle_ReportFaultReason(13U, DF_REASON_UNDERVOLTAGE);
    else if (gDriveFaultDiag.currentMs >= 10U)
        ErrorHandle_ReportFaultReason(10U, DF_REASON_OVERCURRENT);
    else if (gDriveFaultDiag.invalidMs >= 100U)
        ErrorHandle_ReportFaultReason(15U, DF_REASON_SAMPLING);
}

/* DMA voltage history remains diagnostic; reset now uses the fixed-rate
 * voltage AND phase-current qualifier, with a fresh raw recheck at request. */
void DriveFault_ObserveVoltageSample(uint16_t rawBus, uint8_t valid, uint32_t cycle)
{
    uint32_t mv, elapsed, vref = gDriveTelemetryConfig.vrefMv;
    if (!sInitialized || (SystemCoreClock < 1000U))
        return;
    elapsed = cycle - sAdcLastCycle;
    mv = ((uint32_t)rawBus * vref * 51U + 2047U) / 4095U;
    if (!valid ||
        (mv < DF_UNDER_MV + 1000U) || (mv > DF_OVER_MV - 1000U)) {
        sAdcVoltageGood = 0U;
        sAdcHealthyCycles = 0U;
    } else {
        if (!sAdcVoltageGood || !sAdcVoltageSeen || (sAdcVref != vref) ||
            (elapsed >= SystemCoreClock / 200U))
            sAdcHealthyCycles = 0U;
        else if (sAdcHealthyCycles < SystemCoreClock / 5U) {
            sAdcHealthyCycles += elapsed;
            if (sAdcHealthyCycles > SystemCoreClock / 5U)
                sAdcHealthyCycles = SystemCoreClock / 5U;
        }
        sAdcVoltageGood = 1U;
    }
    sAdcVref = vref;
    sAdcLastCycle = cycle;
    sAdcVoltageSeen = 1U;
    gDriveFaultDiag.adcVoltageHealthyMs = sAdcHealthyCycles / (SystemCoreClock / 1000U);
}

void DriveFault_ObserveAdcError(void)
{
    gDriveFaultDiag.healthyMs = 0U;
    sAdcVoltageGood = 0U;
    sAdcHealthyCycles = 0U;
    gDriveFaultDiag.adcVoltageHealthyMs = 0U;
}

uint8_t DriveFault_CanReset(void)
{
    return (DriveFault_GetResetBlockMask() == 0U) ? 1U : 0U;
}

uint16_t DriveFault_GetResetBlockMask(void)
{
    DriveSafetySample v;
    uint16_t mask = DriveSafety_ResetBlockMask();
    uint8_t recoveryReady;
    recoveryReady = gDriveFaultDiag.healthyMs >= 200U &&
        !memcmp(&sRecoveryConfig, &gDriveTelemetryConfig, sizeof(sRecoveryConfig)) &&
        !memcmp(&sRecoveryFaultConfig, &gDriveFaultConfig, sizeof(sRecoveryFaultConfig)) &&
        (uint32_t)(sMs - sSafetyMs) <= 5U;
    if (sAdcVoltageSeen && (!sAdcVoltageGood || sAdcVref != gDriveTelemetryConfig.vrefMv || sAdcHealthyCycles < SystemCoreClock / 5U || (uint32_t)(DWT->CYCCNT - sAdcLastCycle) >= SystemCoreClock / 200U))
        recoveryReady = 0U;
    if (!sInitialized || !recoveryReady)
        mask |= DF_RESET_RECOVERY_TIME;
    if (!sInitialized || ((uint32_t)(sMs - gDriveFaultDiag.lastUpdateMs) > 100U))
        mask |= DF_RESET_FOREGROUND_OLD;
    DriveTelemetry_GetSafetySample(&v); /* raw ADC age checked again at reset */
    if (!(v.status & DT_VALID_VOLTAGE))
        mask |= DF_RESET_INVALID_VOLTAGE;
    else if ((v.busMv < DF_UNDER_MV + 1000U) ||
        (v.busMv > DF_OVER_MV - 1000U))
        mask |= DF_RESET_VOLTAGE_WINDOW;
    if (IoVfoGet() == 0U)
        mask |= DF_RESET_VFO_ACTIVE;
    if (ErrI_sum != 16)
        mask |= DF_RESET_VFO_HISTORY;
    if (gDriveFaultConfig.currentEnable) {
        if (!(v.status & DT_VALID_PHASES))
            mask |= DF_RESET_INVALID_PHASES;
        else if ((Magnitude(v.aMa) >= gDriveFaultConfig.overcurrentMa) ||
            (Magnitude(v.bMa) >= gDriveFaultConfig.overcurrentMa))
            mask |= DF_RESET_PHASE_CURRENT;
    }
    gDriveFaultResetBlockMask = mask;
    return mask;
}

void DriveFault_OnReset(void)
{
    gDriveFaultReason = 0U;
    gDriveFaultResetReady = 0U;
    gDriveFaultDiag.underMs = gDriveFaultDiag.overMs = 0U;
    gDriveFaultDiag.currentMs = gDriveFaultDiag.phaseAMs = gDriveFaultDiag.phaseBMs = 0U;
    gDriveFaultDiag.invalidMs = 0U;
    gDriveFaultDiag.settleMs = gDriveFaultDiag.phaseInvalidMs = 0U;
    sPhaseMs = sMs;
    sPhaseSample = 0U;
    DriveBoot_OnReset();
    DriveSafety_OnReset();
    /* Keep the first-event snapshot for diagnosis until the next boot. */
}

void DriveFault_Update(void)
{
    DriveTelemetryValues v;
    uint32_t now = sMs;
    int32_t referenceA, referenceB;
    uint32_t irq;
    if (!sInitialized)
        return;
    if (now - gDriveFaultDiag.lastUpdateMs > gDriveFaultDiag.maxUpdateGapMs)
        gDriveFaultDiag.maxUpdateGapMs = now - gDriveFaultDiag.lastUpdateMs;
    gDriveFaultDiag.lastUpdateMs = now;
    ++gDriveFaultDiag.updateCount;
    if (now >= 500U)
        sStartupDone = 1U;
    DriveTelemetry_GetSnapshot(&v);
    /* Snapshot references only for the first-fault record. Phase qualification
     * is owned by DriveFault_PhaseTick; foreground gaps must not reset it. */
    irq = __get_PRIMASK();
    __disable_irq();
    referenceA = gDriveCurrentDiag.phaseAReferenceCounts;
    referenceB = gDriveCurrentDiag.phaseBReferenceCounts;
    __set_PRIMASK(irq);
    gDriveFaultResetReady = (ErrNumFlag != 0U) && DriveFault_CanReset() &&
        (IoVfoGet() != 0U) && (ErrI_sum == 16);
    if (ErrNumFlag && !gDriveFaultDiag.firstValid) {
        /* First foreground observation of a latched cause, NOT a precisely
         * synchronized VFO edge waveform. Publish valid last. */
        gDriveFaultDiag.firstCode = ErrNumFlag;
        gDriveFaultDiag.firstReason = gDriveFaultReason;
        gDriveFaultDiag.firstMs = now;
        gDriveFaultDiag.firstStatus = v.status;
        gDriveFaultDiag.firstSample = v.sampleCount;
        gDriveFaultDiag.firstBusMv = v.busVoltageMv;
        gDriveFaultDiag.firstPhaseAMa = v.phaseAMa;
        gDriveFaultDiag.firstPhaseBMa = v.phaseBMa;
        gDriveFaultDiag.firstReferenceA = referenceA;
        gDriveFaultDiag.firstReferenceB = referenceB;
        __DMB();
        gDriveFaultDiag.firstValid = 1U;
    }
}

UINT8 WriteDriveFault(UINT16 index, UINT8 sub, UINT32 size,
    UINT16 MBXMEM *data, UINT8 complete)
{
    uint16_t value;
    if (!DriveParameters_CanWrite())
        return ABORTIDX_IN_THIS_STATE_DATA_CANNOT_BE_READ_OR_STORED;
    if (complete)
        return ABORTIDX_COMPLETE_ACCESS_NOT_SUPPORTED;
    if (sub)
        return ABORTIDX_SUBINDEX_NOT_EXISTING;
    if (size != 2U)
        return ABORTIDX_PARAM_LENGTH_ERROR;
    if (((nAlStatus & STATE_MASK) != STATE_PREOP) || gEcatMotorEnabled || DriveBoot_Busy() ||
        !EthercatMotorControl_IsStopped())
        return ABORTIDX_IN_THIS_STATE_DATA_CANNOT_BE_READ_OR_STORED;
    value = SWAPWORD(data[0]);
    if (index == 0x2020U) {
        if (value > 3U)
            return ABORTIDX_VALUE_EXCEEDED;
        gDriveFaultConfig.currentEnable = value;
    } else if (index == 0x2021U) {
        if ((value < 500U) || (value > 6000U))
            return ABORTIDX_VALUE_EXCEEDED;
        gDriveFaultConfig.overcurrentMa = value;
    } else
        return ABORTIDX_UNSUPPORTED_ACCESS;
    gDriveFaultDiag.healthyMs = 0U;
    gDriveFaultResetReady = 0U;
    return 0U;
}

UINT8 ReadDriveFaultReset(UINT16 index, UINT8 sub, UINT32 size,
    UINT16 MBXMEM *data, UINT8 complete)
{
    uint16_t mask;
    if (complete)
        return ABORTIDX_COMPLETE_ACCESS_NOT_SUPPORTED;
    if (sub)
        return ABORTIDX_SUBINDEX_NOT_EXISTING;
    if (size != 2U)
        return ABORTIDX_PARAM_LENGTH_ERROR;
    if ((index != 0x2031U) && (index != 0x2032U))
        return ABORTIDX_UNSUPPORTED_ACCESS;
    mask = DriveFault_GetResetBlockMask();
    gDriveFaultResetReady = (ErrNumFlag != 0U) && (mask == 0U);
    data[0] = SWAPWORD((index == 0x2031U) ? gDriveFaultResetReady : mask);
    return 0U;
}
