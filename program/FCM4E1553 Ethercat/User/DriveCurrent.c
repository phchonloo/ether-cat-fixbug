/**
 * @file    DriveCurrent.c
 * @brief   电流控制适配
 * @details 根据运行与保持电流配置衔接原厂电流计算及PWM输出。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    电流缩放与厂商算法库接口保持原实现。
 */
#include "DriveCurrent.h"
#include "DriveTelemetry.h"
#include "DriveBoot.h"
#include "DriveParameters.h"
#include "EthercatMotorControl.h"
#include "cia402appl.h"
#include "coeappl.h"
#include "MotionPID.h"
/*
 * Adapter for the shipped N32G455.lib, verified in the linked ARM image:
 * PhaseGet produces signed Q12 sine references (4096 peak).
 * CurrentCal overwrites WorkValue[0]/Igive with 20, then uses
 * (reference * 20) >> 8 as its ADC-count reference. It also has a fixed
 * 1020-call idle rule. Merely writing WorkValue[0] or Igive cannot work.
 *
 * Armlink's documented $Sub$$/$Super$$ interposition keeps the original
 * current PI, ADC-zero acquisition, phase generator and MotorWork/PWM code.
 * We adapt the reference BEFORE the original PI, and own the idle timing.
 * No binary patching, no fake feedback, no change to the library file.
 * Revalidate the compiled-image tests whenever the library/toolchain changes.
 */
#if !defined(__CC_ARM)
#error DriveCurrent requires the verified ARMCC/armlink library adapter
#endif
extern void $Super$$CurrentCal(void);
extern s32 AlphaRef, BeitaRef, TimeStop, Igive, CurrentLock;
extern s32 EssAlphaIn[2], EssAlphaOut[2], EssBeitaIn[2], EssBeitaOut[2];
extern u8 LockFlag;

DriveCurrentConfig gDriveCurrentConfig = {
    DC_DEFAULT_RUN_MA, DC_DEFAULT_HOLD_PERCENT, DC_DEFAULT_HOLD_DELAY_MS};
volatile DriveCurrentDiagnostics gDriveCurrentDiag;
static uint32_t sRunScaleQ16, sHoldScaleQ16;
static uint32_t sCachedVref, sCachedGain, sCachedRun, sCachedHold;
static uint32_t sTestVref, sTestGain, sTestReference;
static uint8_t sWasActive;

static uint8_t ConfigValid(void)
{
    uint32_t vref = gDriveTelemetryConfig.vrefMv;
    uint32_t gain = gDriveTelemetryConfig.sensitivityUvA;
    int32_t run = gDriveCurrentConfig.runPeakMa;
    if ((run < 0) || (run > DC_MAX_RUN_MA) ||
        ((run != 0) && (run < 100)) ||
        (gDriveCurrentConfig.holdPercent < 10U) ||
        (gDriveCurrentConfig.holdPercent > 100U) ||
        (gDriveCurrentConfig.holdDelayMs < 1U) ||
        (gDriveCurrentConfig.holdDelayMs > 30000U) ||
        (vref < 2800U) || (vref > 3600U) ||
        (gain < 10000U) || (gain > 1000000U))
        return 0U;
    /* At most 1024 ADC counts peak. Preserve headroom around the auto-zero;
     * this also bounds all reference multiplications in the original PI. */
    return ((uint64_t)(uint32_t)run * gain * 8190U <=
               (uint64_t)vref * 3000000U * 1024U)
        ? 1U
        : 0U;
}

uint8_t DriveCurrent_OutputAllowed(void)
{
    return (ConfigValid() && (gDriveCurrentConfig.runPeakMa != 0)) ? 1U : 0U;
}

static void ResetOutput(void)
{
    uint32_t i;
    for (i = 0U; i < 2U; ++i) {
        EssAlphaIn[i] = EssAlphaOut[i] = 0;
        EssBeitaIn[i] = EssBeitaOut[i] = 0;
    }
    AlphaRef = BeitaRef = Igive = CurrentLock = TimeStop = 0;
    LockFlag = 0U;
    sWasActive = 0U;
    gDriveCurrentDiag.state = DC_STATE_OFF;
    gDriveCurrentDiag.stationaryTicks = 0U;
    gDriveCurrentDiag.referencePeakMa = 0U;
    gDriveCurrentDiag.phaseAReferenceCounts = 0;
    gDriveCurrentDiag.phaseBReferenceCounts = 0;
}

void DriveCurrent_Init(void)
{
    gDriveCurrentConfig.runPeakMa = DC_DEFAULT_RUN_MA;
    gDriveCurrentConfig.holdPercent = DC_DEFAULT_HOLD_PERCENT;
    gDriveCurrentConfig.holdDelayMs = DC_DEFAULT_HOLD_DELAY_MS;
    memset((void *)&gDriveCurrentDiag, 0, sizeof(gDriveCurrentDiag));
    gDriveCurrentDiag.version = 0x20261020UL;
    sCachedVref = sCachedGain = sCachedRun = sCachedHold = 0U;
    sRunScaleQ16 = sHoldScaleQ16 = 0U;
    sTestVref = sTestGain = sTestReference = 0U;
    ResetOutput();
}

void DriveCurrent_BeginCycle(void)
{
    if (gDriveBootState == DB_TEST)
        return; /* dedicated, bounded PI path owns this cycle */
    gDriveCurrentDiag.requestedPeakMa =
        (gDriveCurrentConfig.runPeakMa > 0) ? (uint32_t)gDriveCurrentConfig.runPeakMa : 0U;
    gDriveCurrentDiag.invalidConfig = ConfigValid() ? 0U : 1U;
    if (!EthercatMotorControl_PowerAllowed() || (ready_ad == 0U) ||
        (EnaFlag != 0U))
        ResetOutput();
}

void DriveCurrent_SelfTestCycle(void)
{
    uint32_t vref, gain;
    if (!DriveBoot_PowerAllowed()) {
        ResetOutput();
        UpPwm(0, 0, 0, 0);
        return;
    }
    if (!sWasActive) {
        ResetOutput();
        sWasActive = 1U;
    }
    /* Original PI applies *20/256 to this input. 707 mA per phase, no PhaseGet
     * or pulse generation, independent of run/hold settings. */
    vref = gDriveTelemetryConfig.vrefMv;
    gain = gDriveTelemetryConfig.sensitivityUvA;
    /* Configuration is write-locked during boot. Cache the conversion so a
     * 64-bit division is not repeated on every 16 kHz current-loop tick. */
    if (vref != sTestVref || gain != sTestGain) {
        sTestReference = (uint32_t)((uint64_t)707U * gain * 8190U * 256U /
            ((uint64_t)vref * 3000000U * 20U));
        sTestVref = vref;
        sTestGain = gain;
    }
    AlphaRef = BeitaRef = (int32_t)sTestReference;
    TimeStop = 0;
    LockFlag = 0U;
    $Super$$CurrentCal();
    gDriveCurrentDiag.state = DC_STATE_HOLD;
    gDriveCurrentDiag.requestedPeakMa = gDriveCurrentDiag.referencePeakMa = DB_TEST_PEAK_MA;
    gDriveCurrentDiag.phaseAReferenceCounts = AlphaRef;
    gDriveCurrentDiag.phaseBReferenceCounts = BeitaRef;
    MotorWork(EssAlphaOut[0], EssBeitaOut[0]);
}

static void UpdateScale(void)
{
    uint32_t vref = gDriveTelemetryConfig.vrefMv;
    uint32_t gain = gDriveTelemetryConfig.sensitivityUvA;
    uint32_t run = (uint32_t)gDriveCurrentConfig.runPeakMa;
    uint32_t hold = gDriveCurrentConfig.holdPercent;
    if ((vref != sCachedVref) || (gain != sCachedGain) ||
        (run != sCachedRun) || (hold != sCachedHold)) {
        /* ADC counts per peak mA: gain(uV/A) * 2/3 * 4095 /
         * (Vref(mV) * 1000000). Original Q12 * 20 / 256 has peak 320.
         * The expensive division runs ONLY after configuration changes.
         * 0x6075 and telemetry display zeros are deliberately not inputs:
         * the original PI still uses its power-off acquired current_base1/2. */
        sRunScaleQ16 = (uint32_t)((uint64_t)run * gain * 8190U * 65536U /
            ((uint64_t)vref * 3000000U * 320U));
        sHoldScaleQ16 = sRunScaleQ16 * hold / 100U;
        sCachedVref = vref;
        sCachedGain = gain;
        sCachedRun = run;
        sCachedHold = hold;
    }
}

void $Sub$$CurrentCal(void)
{
    uint32_t scale, delayTicks, referenceMa;
    if (gDriveCurrentDiag.adapterCalls != UINT32_MAX)
        gDriveCurrentDiag.adapterCalls++;
    /* TIM8_UP_IRQHandler already ran BeginCycle before MainFunt. Check power
     * again in case PhaseGet blocked it during this same cycle, and retain
     * BeginCycle's immediate PI reset when that happens. */
    if (!EthercatMotorControl_PowerAllowed() || !ready_ad || EnaFlag) {
        ResetOutput();
        return;
    }
    UpdateScale();
    if (!sWasActive) {
        /* Erase accumulated PI state from a previous enable. Preserve the
         * freshly generated PhaseGet references for this first live cycle. */
        EssAlphaIn[0] = EssAlphaIn[1] = EssAlphaOut[0] = EssAlphaOut[1] = 0;
        EssBeitaIn[0] = EssBeitaIn[1] = EssBeitaOut[0] = EssBeitaOut[1] = 0;
        sWasActive = 1U;
    }
    delayTicks = gDriveCurrentConfig.holdDelayMs *
        (ECAT_MOTOR_CONTROL_HZ / 1000U);
    if (EthercatMotorControl_CurrentMotionActive()) {
        gDriveCurrentDiag.stationaryTicks = 0U;
        gDriveCurrentDiag.state = DC_STATE_RUN;
    } else {
        if (gDriveCurrentDiag.stationaryTicks < delayTicks)
            gDriveCurrentDiag.stationaryTicks++;
        gDriveCurrentDiag.state =
            (gDriveCurrentDiag.stationaryTicks >= delayTicks) ? DC_STATE_HOLD : DC_STATE_WAIT;
    }
    referenceMa = (uint32_t)gDriveCurrentConfig.runPeakMa;
    scale = sRunScaleQ16;
    if (gDriveCurrentDiag.state == DC_STATE_HOLD) {
        referenceMa = referenceMa * gDriveCurrentConfig.holdPercent / 100U;
        scale = sHoldScaleQ16;
    }
    /* Verified Q12 input and capped scale keep products within signed 32 bits. */
    if ((AlphaRef < -4096) || (AlphaRef > 4096) ||
        (BeitaRef < -4096) || (BeitaRef > 4096)) {
        ResetOutput();
        gDriveCurrentDiag.invalidConfig = 1U;
        return;
    }
    AlphaRef = AlphaRef * (int32_t)scale / 65536L;
    BeitaRef = BeitaRef * (int32_t)scale / 65536L;
    /* Prevent the library's hard-coded ~63.75 ms idle reduction from applying
     * a SECOND reduction, a gain change, or a static error deadband. */
    TimeStop = 0;
    LockFlag = 0U;
    $Super$$CurrentCal();
    gDriveCurrentDiag.referencePeakMa = referenceMa;
    gDriveCurrentDiag.phaseAReferenceCounts = AlphaRef;
    gDriveCurrentDiag.phaseBReferenceCounts = BeitaRef;
}

UINT8 WriteDriveCurrent(UINT16 index, UINT8 sub, UINT32 size,
    UINT16 MBXMEM *data, UINT8 complete)
{
    uint16_t value;
    if (!DriveParameters_CanWrite())
        return ABORTIDX_IN_THIS_STATE_DATA_CANNOT_BE_READ_OR_STORED;
    if (complete)
        return ABORTIDX_COMPLETE_ACCESS_NOT_SUPPORTED;
    if (sub != 0U)
        return ABORTIDX_SUBINDEX_NOT_EXISTING;
    if (size != 2U)
        return ABORTIDX_PARAM_LENGTH_ERROR;
    if (((nAlStatus & STATE_MASK) != STATE_PREOP) || gEcatMotorEnabled || DriveBoot_Busy() ||
        !EthercatMotorControl_IsStopped())
        return ABORTIDX_IN_THIS_STATE_DATA_CANNOT_BE_READ_OR_STORED;
    value = SWAPWORD(data[0]);
    switch (index) {
        case 0x20F1:
            if ((value > DC_MAX_RUN_MA) || ((value != 0U) && (value < 100U)))
                return ABORTIDX_VALUE_EXCEEDED;
            gDriveCurrentConfig.runPeakMa = (int16_t)value;
            break;
        case 0x20D5:
            if ((value < 10U) || (value > 100U))
                return ABORTIDX_VALUE_EXCEEDED;
            gDriveCurrentConfig.holdPercent = value;
            break;
        case 0x20F5:
            if ((value < 1U) || (value > 30000U))
                return ABORTIDX_VALUE_EXCEEDED;
            gDriveCurrentConfig.holdDelayMs = value;
            break;
        default:
            return ABORTIDX_UNSUPPORTED_ACCESS;
    }
    /* No power enable, no motion, no telemetry fabrication, no flash write. */
    return 0U;
}
