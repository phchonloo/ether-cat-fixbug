/**
 * @file    DriveBoot.c
 * @brief   启动零点校准
 * @details 维护启动校准状态，与遥测、电流及故障模块交换校准结果。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    启动校准与正常电机输出之间的约束保持原实现。
 */
#include "DriveBoot.h"
#include "DriveCurrent.h"
#include "DriveFault.h"
#include "EthercatMotorControl.h"
#include "Errorhadle.h"
#include "cia402appl.h"
#include "qspi.h"
extern s32 current_base1, current_base2;
extern TCiA402Axis LocalAxes[MAX_AXES];
volatile uint16_t gDriveBootState;
volatile DriveBootDiagnostics gDriveBootDiag;
static uint32_t sSumA, sSumB, sSequence, sTestStart, sLastCycle;
static uint8_t sExciteOnce, sInCycle, sSampleSafe;

static uint32_t AbsCurrent(int32_t x)
{
    return (x < 0) ? (uint32_t)(-(int64_t)x) : (uint32_t)x;
}

static void Fail(uint8_t code, uint16_t reason)
{
    sInCycle = sSampleSafe = 0U;
    gDriveBootState = DB_FAULT;
    gDriveBootDiag.result = reason;
    ErrorHandle_ReportFaultReason(code, reason);
    UpPwm(0, 0, 0, 0);
}

void DriveBoot_Init(void)
{
    memset((void *)&gDriveBootDiag, 0, sizeof(gDriveBootDiag));
    gDriveBootDiag.version = 0x01080001UL;
    gDriveBootState = DB_WAIT;
    sSumA = sSumB = sSequence = sTestStart = sLastCycle = 0U;
    sInCycle = sSampleSafe = 0U;
    sExciteOnce = 0U; /* Release 15: startup never energizes the motor. */
}

uint8_t DriveBoot_Busy(void)
{
    return gDriveBootState >= DB_WAIT && gDriveBootState <= DB_TEST;
}

uint8_t DriveBoot_BlocksNormal(void)
{
    return gDriveBootState != DB_OFF && !gDriveBootDiag.normalReleased;
}

uint8_t DriveBoot_PowerAllowed(void)
{
    return sInCycle && gDriveBootState == DB_TEST && sSampleSafe &&
        SystemCoreClock >= 1000U &&
        (uint32_t)(DWT->CYCCNT - sTestStart) < SystemCoreClock &&
        (uint32_t)(DWT->CYCCNT - sLastCycle) < SystemCoreClock / 200U &&
        !ErrNumFlag && !gEscPdiDiag.faultLatched && IoVfoGet() &&
        ready_ad && !EnaFlag && DriveCurrent_OutputAllowed() &&
        !(LocalAxes[0].Objects.objControlWord & 0x0100U);
}

/* The permission is scoped to this dedicated current-only call. A normal
 * MainFunt/foreground/PDO path cannot borrow boot permission for movement. */
uint8_t DriveBoot_RunCycle(void)
{
    if (gDriveBootState != DB_TEST)
        return 0U;
    sInCycle = 1U;
    if (DriveBoot_PowerAllowed())
        DriveCurrent_SelfTestCycle();
    else
        UpPwm(0, 0, 0, 0);
    sInCycle = 0U;
    return 1U;
}

void DriveBoot_Tick(const DriveSafetySample *sample, uint32_t elapsed)
{
    uint32_t a, b;
    uint8_t fresh, voltageGood;
    if (gDriveBootState == DB_OFF)
        return;
    if (ErrNumFlag || gEscPdiDiag.faultLatched) {
        sSampleSafe = 0U;
        gDriveBootState = DB_FAULT;
        gDriveBootDiag.result = gDriveFaultReason;
        gDriveBootDiag.normalReleased = 0U;
        return;
    }
    if (gDriveBootState == DB_PASS || gDriveBootState == DB_RESET) {
        /* Retire a queued enable; a post-test disabled controlword is required. */
        if (!(LocalAxes[0].Objects.objControlWord & 8U))
            gDriveBootDiag.normalReleased = 1U;
        return;
    }
    if (gDriveBootState == DB_FAULT)
        return;
    gDriveBootDiag.elapsedMs += elapsed;
    fresh = sample->sequence && sample->sequence != sSequence && elapsed <= 5U;
    sSequence = sample->sequence;
    voltageGood = (sample->status & DT_VALID_VOLTAGE) &&
        sample->busMv >= 19000U && sample->busMv <= 29000U;
    if (gDriveBootState != DB_WAIT && (sample->status & DT_VALID_VOLTAGE)) {
        if (sample->busMv < DF_UNDER_MV) {
            Fail(13U, DF_REASON_UNDERVOLTAGE);
            return;
        }
        if (sample->busMv > DF_OVER_MV) {
            Fail(14U, DF_REASON_OVERVOLTAGE);
            return;
        }
    }
    if ((gDriveBootState == DB_WAIT || gDriveBootState == DB_ZERO) &&
        gDriveBootDiag.elapsedMs >= 3000U) {
        Fail(15U, DF_REASON_ZERO);
        return;
    }
    if (gDriveBootState == DB_WAIT) {
        if (gDriveBootDiag.elapsedMs >= 500U && voltageGood && fresh &&
            (sample->status & DT_VALID_PHASES) &&
            ready_ad && !EnaFlag && IoVfoGet() && ErrI_sum == 16) {
            gDriveBootState = DB_ZERO;
            gDriveBootDiag.zeroMinA = gDriveBootDiag.zeroMinB = 4095U;
        }
        return;
    }
    if (gDriveBootState == DB_ZERO) {
        /* A missed DMA frame can coincide with the TIM8 tick. Require
         * 256 fresh samples, bounded by the startup timeout above. */
        if (!fresh)
            return;
        if (!voltageGood || !(sample->status & DT_VALID_PHASES) ||
            sample->rawA < 1800U || sample->rawA > 2300U ||
            sample->rawB < 1800U || sample->rawB > 2300U ||
            !ready_ad || EnaFlag || !IoVfoGet()) {
            Fail(15U, DF_REASON_ZERO);
            return;
        }
        if (sample->rawA < gDriveBootDiag.zeroMinA)
            gDriveBootDiag.zeroMinA = sample->rawA;
        if (sample->rawA > gDriveBootDiag.zeroMaxA)
            gDriveBootDiag.zeroMaxA = sample->rawA;
        if (sample->rawB < gDriveBootDiag.zeroMinB)
            gDriveBootDiag.zeroMinB = sample->rawB;
        if (sample->rawB > gDriveBootDiag.zeroMaxB)
            gDriveBootDiag.zeroMaxB = sample->rawB;
        if (gDriveBootDiag.zeroMaxA - gDriveBootDiag.zeroMinA > 16U ||
            gDriveBootDiag.zeroMaxB - gDriveBootDiag.zeroMinB > 16U) {
            Fail(15U, DF_REASON_ZERO);
            return;
        }
        sSumA += sample->rawA;
        sSumB += sample->rawB;
        if (++gDriveBootDiag.zeroSamples < 256U)
            return;
        gDriveBootDiag.zeroAUv = (uint32_t)((uint64_t)sSumA * gDriveTelemetryConfig.vrefMv * 1000U / (256U * 4095U));
        gDriveBootDiag.zeroBUv = (uint32_t)((uint64_t)sSumB * gDriveTelemetryConfig.vrefMv * 1000U / (256U * 4095U));
        gDriveTelemetryConfig.zeroAUv = gDriveBootDiag.zeroAUv;
        gDriveTelemetryConfig.zeroBUv = gDriveBootDiag.zeroBUv;
        current_base1 = (sSumA + 128U) / 256U;
        current_base2 = (sSumB + 128U) / 256U;
        gDriveBootDiag.zeroValid = 1U;
        sLastCycle = sample->cycle;
        sTestStart = DWT->CYCCNT;
        sSampleSafe = 1U;
        gDriveBootState = sExciteOnce ? DB_TEST : DB_RESET;
        sExciteOnce = 0U;
        return;
    }
    /* No waveform/position generator is called during the test. Both signed
     * phase references are fixed positive; shaft alignment can still occur. */
    gDriveBootDiag.testMs += elapsed;
    if (!fresh || !voltageGood || (sample->status & 3U) != 3U ||
        !ready_ad || EnaFlag || (LocalAxes[0].Objects.objControlWord & 0x0100U)) {
        Fail(15U, DF_REASON_SELF_TEST);
        return;
    }
    a = AbsCurrent(sample->aMa);
    b = AbsCurrent(sample->bMa);
    gDriveBootDiag.measuredAMa = sample->aMa;
    gDriveBootDiag.measuredBMa = sample->bMa;
    sLastCycle = sample->cycle;
    sSampleSafe = 1U;
    if (a > 1500U || b > 1500U) {
        Fail(10U, DF_REASON_OVERCURRENT);
        return;
    }
    if (gDriveBootDiag.testMs >= 300U) {
        gDriveBootDiag.missingAMs = a < 140U ? gDriveBootDiag.missingAMs + elapsed : 0U;
        gDriveBootDiag.missingBMs = b < 140U ? gDriveBootDiag.missingBMs + elapsed : 0U;
        gDriveBootDiag.goodMs = (a >= 350U && a <= 1060U && b >= 350U && b <= 1060U) ? gDriveBootDiag.goodMs + elapsed : 0U;
        if (gDriveBootDiag.missingAMs >= 200U) {
            Fail(11U, DF_REASON_PHASE_A);
            return;
        }
        if (gDriveBootDiag.missingBMs >= 200U) {
            Fail(11U, DF_REASON_PHASE_B);
            return;
        }
        if (gDriveBootDiag.goodMs >= 100U) {
            gDriveBootState = DB_PASS;
            sSampleSafe = 0U;
            UpPwm(0, 0, 0, 0);
            return;
        }
    }
    if (gDriveBootDiag.testMs >= DB_TEST_LIMIT_MS ||
        (uint32_t)(DWT->CYCCNT - sTestStart) >= SystemCoreClock)
        Fail(15U, DF_REASON_SELF_TEST);
}

void DriveBoot_OnReset(void)
{
    if (gDriveBootState == DB_OFF)
        return;
    sInCycle = sSampleSafe = sExciteOnce = 0U;
    gDriveBootDiag.normalReleased = 0U;
    /* Original library re-enters ADC initialization when ready_ad was cleared
     * by a fault. Requalify and replace both PI baselines with PWM inhibited. */
    gDriveBootDiag.zeroValid = 0U;
    gDriveBootDiag.elapsedMs = gDriveBootDiag.zeroSamples = 0U;
    gDriveBootDiag.zeroMaxA = gDriveBootDiag.zeroMaxB = 0U;
    sSumA = sSumB = sSequence = 0U;
    gDriveBootState = DB_WAIT; /* zero only: reset never re-energizes */
}
