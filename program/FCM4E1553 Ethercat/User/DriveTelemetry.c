/**
 * @file    DriveTelemetry.c
 * @brief   驱动遥测
 * @details 从ADC采样帧生成安全采样数据及对象字典读取快照。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    快照一致性、采样有效性及单位换算保持原实现。
 */
#include "DriveTelemetry.h"
#include "DriveParameters.h"
#include "DriveFault.h"
#include "DriveBoot.h"
#include "AdcInit.h"
#include "EthercatMotorControl.h"
#include "cia402appl.h"
#include "coeappl.h"
extern TCiA402Axis LocalAxes[MAX_AXES];

DriveTelemetryValues gDriveTelemetry;
volatile DriveTelemetryDiagnostics gDriveTelemetryDiag;
DriveTelemetryConfig gDriveTelemetryConfig = {
    DT_DEFAULT_VREF_MV, DT_DEFAULT_SENSITIVITY_UVA,
    DT_DEFAULT_ZERO_UV, DT_DEFAULT_ZERO_UV, DT_DEFAULT_RATED_CURRENT_MA};

typedef struct {
    uint32_t sequence, cycle;
    uint16_t a, b, bus, rails;
} RawFrame;
typedef struct {
    DriveTelemetryValues values;
    uint32_t cycle;
} ConvertedFrame;
typedef struct {
    uint32_t sampleCycle, readCycle, ageCycles;
} SnapshotTiming;
static volatile RawFrame sRaw[2];
static volatile uint32_t sRawIndex, sRawCount, sAdcError;
static ConvertedFrame sConverted[2];
static volatile uint32_t sConvertedIndex;
static uint32_t sLastUpdate;
static volatile uint32_t sExpiredSequence;

static void InvalidMeasurements(DriveTelemetryValues *v)
{
    v->status &= ~(DT_VALID_VOLTAGE | DT_VALID_PHASES | DT_VALID_CURRENT |
        DT_VALID_TORQUE | DT_VALID_BUS_CURRENT);
    v->phaseAMa = v->phaseBMa = v->busCurrentMa = DT_INVALID_I32;
    v->currentMa = v->currentPermille = DT_INVALID_I16;
    v->busVoltageMv = DT_INVALID_U32;
    v->busVoltageDecivolt = 0xFFFFU;
}

void DriveTelemetry_Init(void)
{
    memset((void *)sRaw, 0, sizeof(sRaw));
    memset(sConverted, 0, sizeof(sConverted));
    memset((void *)&gDriveTelemetryDiag, 0, sizeof(gDriveTelemetryDiag));
    gDriveTelemetryDiag.version = 0x20261019UL;
    sRawIndex = sRawCount = sAdcError = sConvertedIndex = sLastUpdate = 0U;
    sExpiredSequence = 0U;
    gDriveTelemetryConfig.vrefMv = DT_DEFAULT_VREF_MV;
    /* MT9221 Rev 2.0, p9: +/-10 A, 200 mV/A, Vzero=VCC/2 at 5 V.
     * User-authorized substitute profile only; verify the fitted sensor,
     * supply, VDDA and both zero offsets before trusting physical accuracy. */
    gDriveTelemetryConfig.sensitivityUvA = DT_DEFAULT_SENSITIVITY_UVA;
    gDriveTelemetryConfig.zeroAUv = gDriveTelemetryConfig.zeroBUv = DT_DEFAULT_ZERO_UV;
    /* Normalization only: this default does not set PWM or motor current.
     * A nonzero reference alone must never validate an unpublished sample. */
    gDriveTelemetryConfig.ratedCurrentMa = DT_DEFAULT_RATED_CURRENT_MA;
    sConverted[0].values.status = DT_ADC_STALE;
    if (gDriveTelemetryConfig.ratedCurrentMa == 0U)
        sConverted[0].values.status |= DT_RATED_UNSET;
    if (gDriveTelemetryConfig.sensitivityUvA == 0U)
        sConverted[0].values.status |= DT_CURRENT_UNCALIBRATED;
    InvalidMeasurements(&sConverted[0].values);
    sConverted[0].values.hardwareVersion = 0x0102U;
    sConverted[0].values.softwareVersion = DT_INTERFACE_VERSION;
    sConverted[0].values.commandPositionError = DT_INVALID_I32;
    sConverted[1] = sConverted[0];
    DriveTelemetry_Latch();
}

void DriveTelemetry_AdcError(void)
{
    sAdcError = 1U;
    DriveFault_ObserveAdcError();
}

void DriveTelemetry_AdcComplete(void)
{
    RawFrame f;
    uint32_t i, a = 0U, b = 0U, start = DWT->CYCCNT;
    uint32_t slot = sRawIndex ^ 1U;
    /* DMA has reloaded NDTR=10 at transfer completion. If service was late,
     * never combine samples from two conversions. IRQ/SYNC0 stay unmasked. */
    if (DMA_GetCurrDataCounter(DMA2_CH1) != 10U)
        return;
    f.rails = 0U;
    for (i = 0U; i < 10U; ++i) {
        uint32_t v = ((volatile uint32_t *)adc_value)[i];
        if (i == 2U) {
            f.bus = (uint16_t)v;
            if (v >= 4095U)
                f.rails |= 1U;
        } else if (i != 3U) {
            if ((v == 0U) || (v >= 4095U))
                f.rails |= 2U;
            if ((i & 1U) == 0U)
                a += v;
            else
                b += v;
        }
    }
    /* Also reject a preemption long enough for an entire DMA buffer wrap. */
    if ((DMA_GetCurrDataCounter(DMA2_CH1) != 10U) ||
        ((uint32_t)(DWT->CYCCNT - start) >= SystemCoreClock / 32000U))
        return;
    f.a = (uint16_t)((a + 2U) / 4U);
    f.b = (uint16_t)((b + 2U) / 4U);
    f.sequence = sRawCount + 1U;
    if (f.sequence == 0U)
        f.sequence = 1U;
    f.cycle = start;
    sRaw[slot] = f;
    __DMB();
    sRawIndex = slot;
    sRawCount = f.sequence;
    sAdcError = 0U;
    DriveFault_ObserveVoltageSample(f.bus, (f.rails & 1U) == 0U, f.cycle);
}

static uint32_t IntegerSqrt(uint32_t n)
{
    uint32_t result = 0U, bit = 1UL << 30;
    while (bit != 0U) {
        if (n >= result + bit) {
            n -= result + bit;
            result = (result >> 1) + bit;
        } else
            result >>= 1;
        bit >>= 2;
    }
    return result;
}

static int32_t PhaseMa(uint16_t raw, uint32_t zero)
{
    uint32_t adcMvProduct = (uint32_t)raw * gDriveTelemetryConfig.vrefMv;
    /* The ADC product fits in 32 bits (4095 * 3600). Split its quotient and
     * remainder to preserve the original integer microvolt conversion. */
    uint32_t uv = (adcMvProduct / 4095U) * 1000U +
        ((adcMvProduct % 4095U) * 1000U) / 4095U;
    int32_t deltaUv = (int32_t)uv - (int32_t)zero;
    uint32_t divisor = 2U * gDriveTelemetryConfig.sensitivityUvA;
    /* R101/R224=10k, R100/R225=20k: ADC sees 2/3 of sensor output. */
    if (divisor <= INT32_MAX / 3000) {
        /* With this bound, remainder * 3000 also fits int32. On Cortex-M4
         * this uses 32-bit division instead of the expensive 64-bit helper.
         * Truncation toward zero matches the original signed expression. */
        int32_t signedDivisor = (int32_t)divisor;
        int32_t whole = deltaUv / signedDivisor;
        int32_t remainder = deltaUv % signedDivisor;
        return whole * 3000 + remainder * 3000 / signedDivisor;
    }
    return (int32_t)((int64_t)deltaUv * 3000 / (int64_t)divisor);
}

void DriveTelemetry_GetSafetySample(DriveSafetySample *sample)
{
    RawFrame raw;
    uint32_t count = sRawCount;
    memset(sample, 0, sizeof(*sample));
    sample->aMa = sample->bMa = DT_INVALID_I32;
    __DMB();
    raw = sRaw[sRawIndex];
    __DMB();
    if (!count || count != sRawCount || count != raw.sequence || sAdcError ||
        SystemCoreClock < 1000U ||
        (uint32_t)(DWT->CYCCNT - raw.cycle) >= SystemCoreClock / 200U)
        return;
    sample->cycle = raw.cycle;
    sample->sequence = raw.sequence;
    sample->rawA = raw.a;
    sample->rawB = raw.b;
    if (!(raw.rails & 1U)) {
        sample->busMv = ((uint32_t)raw.bus * gDriveTelemetryConfig.vrefMv * 51U + 2047U) / 4095U;
        sample->status |= DT_VALID_VOLTAGE;
    }
    if (!(raw.rails & 2U) && gDriveTelemetryConfig.sensitivityUvA) {
        sample->aMa = PhaseMa(raw.a, gDriveTelemetryConfig.zeroAUv);
        sample->bMa = PhaseMa(raw.b, gDriveTelemetryConfig.zeroBUv);
        if (sample->aMa >= -10000 && sample->aMa <= 10000 &&
            sample->bMa >= -10000 && sample->bMa <= 10000)
            sample->status |= DT_VALID_PHASES;
    }
    if (sAdcError || (uint32_t)(DWT->CYCCNT - raw.cycle) >= SystemCoreClock / 200U) {
        sample->status = sample->sequence = 0U;
        sample->aMa = sample->bMa = DT_INVALID_I32;
    }
}

uint8_t DriveTelemetry_GetPhaseSample(DrivePhaseSample *sample)
{
    RawFrame raw;
    uint32_t count = sRawCount;
    sample->aMa = sample->bMa = DT_INVALID_I32;
    sample->sequence = 0U;
    __DMB();
    raw = sRaw[sRawIndex];
    __DMB();
    /* No foreground conversion, publication or retry loop in the motor ISR.
     * Retain the real 5 ms ADC deadline and reject DMA/rail errors. */
    if ((count != sRawCount) || (count != raw.sequence) || !count ||
        sAdcError || (raw.rails & 2U) || (SystemCoreClock < 1000U) ||
        ((uint32_t)(DWT->CYCCNT - raw.cycle) >= SystemCoreClock / 200U) ||
        !gDriveTelemetryConfig.sensitivityUvA)
        return 0U;
    sample->aMa = PhaseMa(raw.a, gDriveTelemetryConfig.zeroAUv);
    sample->bMa = PhaseMa(raw.b, gDriveTelemetryConfig.zeroBUv);
    /* Recheck ADC freshness after conversion before accepting the sample. */
    if (sAdcError || ((uint32_t)(DWT->CYCCNT - raw.cycle) >= SystemCoreClock / 200U) ||
        (sample->aMa < -10000) || (sample->aMa > 10000) ||
        (sample->bMa < -10000) || (sample->bMa > 10000)) {
        sample->aMa = sample->bMa = DT_INVALID_I32;
        return 0U;
    }
    sample->sequence = raw.sequence;
    return 1U;
}

void DriveTelemetry_Update(void)
{
    RawFrame raw;
    ConvertedFrame *c;
    DriveTelemetryValues *v;
    uint32_t count, now = DWT->CYCCNT, sampleNow, slot;
    int64_t error;
    if ((uint32_t)(now - sLastUpdate) < SystemCoreClock / 1000U) {
        DriveTelemetry_Latch();
        return;
    }
    sLastUpdate = now;
    count = sRawCount;
    __DMB();
    raw = sRaw[sRawIndex];
    __DMB();
    /* A foreground copy may be interrupted by DMA. Retry next loop, not spin. */
    if ((count != sRawCount) || (count != raw.sequence))
        return;
    slot = sConvertedIndex ^ 1U;
    c = &sConverted[slot];
    memset(c, 0, sizeof(*c));
    c->cycle = raw.cycle;
    v = &c->values;
    InvalidMeasurements(v);
    v->rawA = raw.a;
    v->rawB = raw.b;
    v->rawBus = raw.bus;
    v->sampleCount = raw.sequence;
    if (gDriveTelemetryConfig.sensitivityUvA == 0U)
        v->status |= DT_CURRENT_UNCALIBRATED;
    if (gDriveTelemetryConfig.ratedCurrentMa == 0U)
        v->status |= DT_RATED_UNSET;
    if (raw.rails)
        v->status |= DT_ADC_RAIL;
    /* DMA can publish a NEW frame after the rate-limit timestamp above.
     * Read time again after copying/validating that frame. Otherwise unsigned
     * (old now - new raw.cycle) wraps and falsely expires fresh measurements.
     * Do not mask IRQs or relax the real 5 ms deadline. */
    sampleNow = DWT->CYCCNT;
    if ((raw.sequence != 0U) && (raw.sequence != sExpiredSequence) && !sAdcError &&
        ((uint32_t)(sampleNow - raw.cycle) < SystemCoreClock / 200U)) {
        if ((raw.rails & 1U) == 0U) {
            /* PE12/ADC2, R99=100k and R104=2k, nominal VDDA=3.3V. */
            v->busVoltageMv = ((uint32_t)raw.bus * gDriveTelemetryConfig.vrefMv * 51U + 2047U) / 4095U;
            v->busVoltageDecivolt = (uint16_t)((v->busVoltageMv + 50U) / 100U);
            v->status |= DT_VALID_VOLTAGE;
        }
        if (((raw.rails & 2U) == 0U) && (gDriveTelemetryConfig.sensitivityUvA != 0U)) {
            int32_t a = PhaseMa(raw.a, gDriveTelemetryConfig.zeroAUv);
            int32_t b = PhaseMa(raw.b, gDriveTelemetryConfig.zeroBUv);
            /* Software monitoring range +/-10 A, NOT an overcurrent trip.
             * Out-of-range values are never presented as valid clamps. */
            if ((a >= -10000) && (a <= 10000) && (b >= -10000) && (b <= 10000)) {
                uint32_t magnitude = IntegerSqrt((uint32_t)(a * a + b * b));
                v->phaseAMa = a;
                v->phaseBMa = b;
                v->currentMa = (int16_t)magnitude;
                v->status |= DT_VALID_PHASES;
                if (gDriveTelemetryConfig.ratedCurrentMa != 0U) {
                    uint32_t p = magnitude * 1000U / gDriveTelemetryConfig.ratedCurrentMa;
                    if (p <= 32767U) {
                        v->currentPermille = (int16_t)p;
                        v->status |= DT_VALID_CURRENT;
                    }
                }
            } else
                v->status |= DT_ADC_RAIL;
        }
    } else {
        v->status |= DT_ADC_STALE;
        sExpiredSequence = raw.sequence;
    }
    if (sAdcError)
        v->status |= DT_ADC_ERROR;
    v->runState = LocalAxes[0].Objects.objStatusWord;
    v->hardwareVersion = 0x0102U;
    v->softwareVersion = DT_INTERFACE_VERSION;
    v->commandPositionError = DT_INVALID_I32;
    if ((LocalAxes[0].Objects.objModesOfOperationDisplay == 1) ||
        (LocalAxes[0].Objects.objModesOfOperationDisplay == 8)) {
        /* Private diagnostic 0x2009: remaining INTERNAL command distance.
         * Not encoder feedback, a following-error monitor, or proof of motion. */
        error = (int64_t)gEcatPulseDiag.targetPosition - gEcatPulseDiag.countedPosition;
        if (error > 2147483647LL)
            error = 2147483647LL;
        if (error < -2147483647LL)
            error = -2147483647LL;
        v->commandPositionError = (int32_t)error;
        v->status |= DT_VALID_COMMAND_ERROR;
    }
    __DMB();
    sConvertedIndex = slot;
    DriveTelemetry_Latch();
}

void DriveTelemetry_Latch(void)
{
    DriveTelemetry_GetSnapshot(&gDriveTelemetry);
}

static void GetSnapshotTimed(DriveTelemetryValues *snapshot, SnapshotTiming *timing)
{
    const ConvertedFrame *c = &sConverted[sConvertedIndex];
    uint32_t sampleCycle, readCycle, ageCycles;
    __DMB();
    /* Foreground is the only converted-slot writer. Each SDO/PDO reader gets
     * its own snapshot, including during nested IRQ/SYNC0 handling. */
    *snapshot = c->values;
    sampleCycle = c->cycle;
    readCycle = DWT->CYCCNT;
    ageCycles = (uint32_t)(readCycle - sampleCycle);
    if (timing != NULL) {
        timing->sampleCycle = sampleCycle;
        timing->readCycle = readCycle;
        timing->ageCycles = ageCycles;
    }
    if ((c->values.sampleCount == 0U) ||
        (c->values.sampleCount == sExpiredSequence) || sAdcError ||
        (ageCycles >= SystemCoreClock / 200U)) {
        sExpiredSequence = c->values.sampleCount;
        InvalidMeasurements(snapshot);
        snapshot->status |= DT_ADC_STALE;
        if (sAdcError)
            snapshot->status |= DT_ADC_ERROR;
    }
}

void DriveTelemetry_GetSnapshot(DriveTelemetryValues *snapshot)
{
    GetSnapshotTimed(snapshot, NULL);
}

static void LatchReadFailure(volatile DriveTelemetryReadFailure *record,
    UINT16 index, UINT16 required, uint32_t reason,
    uint32_t abortCode, const DriveTelemetryValues *snapshot,
    const SnapshotTiming *timing)
{
    if (record->valid != 0U)
        return;
    record->reason = reason;
    record->abortCode = abortCode;
    record->index = index;
    record->status = snapshot->status;
    record->requiredStatus = required;
    record->sampleCount = snapshot->sampleCount;
    record->sampleCycle = timing->sampleCycle;
    record->readCycle = timing->readCycle;
    record->ageCycles = timing->ageCycles;
    record->rawBus = snapshot->rawBus;
    __DMB();
    record->valid = 1U;
}

static void RecordReadFailure(UINT16 index, UINT16 required, uint32_t reason,
    uint32_t abortCode, const DriveTelemetryValues *snapshot,
    const SnapshotTiming *timing)
{
    /* Only serialized mailbox SDO reads write these diagnostics. ADC/PDO
     * paths never do. Bounded RAM-only stores: no waits, printing or IRQ mask. */
    if (gDriveTelemetryDiag.readFailureCount != UINT32_MAX)
        gDriveTelemetryDiag.readFailureCount++;
    LatchReadFailure(&gDriveTelemetryDiag.firstReadFailure, index, required,
        reason, abortCode, snapshot, timing);
    if ((index == 0x2065U) || (index == 0x6079U)) {
        if (gDriveTelemetryDiag.voltageFailureCount != UINT32_MAX)
            gDriveTelemetryDiag.voltageFailureCount++;
        LatchReadFailure(&gDriveTelemetryDiag.firstVoltageFailure, index, required,
            reason, abortCode, snapshot, timing);
    }
}

UINT8 ReadDriveTelemetry(UINT16 index, UINT8 sub, UINT32 size,
    UINT16 MBXMEM *data, UINT8 complete)
{
    const TOBJECT *object;
    DriveTelemetryValues snapshot;
    SnapshotTiming timing;
    const void *source;
    uint16_t required = 0U;
    if (complete)
        return ABORTIDX_COMPLETE_ACCESS_NOT_SUPPORTED;
    if (sub != 0U)
        return ABORTIDX_SUBINDEX_NOT_EXISTING;
    object = OBJ_GetObjectHandle(index);
    if ((object == NULL) || (object->pVarPtr == NULL))
        return ABORTIDX_OBJECT_NOT_EXISTING;
    if (size != object->pEntryDesc->BitLength / 8U)
        return ABORTIDX_PARAM_LENGTH_ERROR;
    GetSnapshotTimed(&snapshot, &timing);
    switch (index) {
        case 0x6079:
        case 0x2065:
            required = DT_VALID_VOLTAGE;
            break;
        case 0x2003:
        case 0x2004:
        case 0x2064:
            required = DT_VALID_PHASES;
            break;
        case 0x6078:
            required = DT_VALID_CURRENT;
            break;
        /* Private diagnostics remain readable in every mode. 0x2009 returns
     * DT_INVALID_I32 with 0x2001.bit5 clear when PP/CSP is not active.
     * Unimplemented bus current and standard torque/following-error objects
     * are absent from the dictionary, not exposed as zero measurements. */
        default:
            break;
    }
    if (required && !(snapshot.status & required)) {
        RecordReadFailure(index, required, DT_READ_FAIL_INVALID,
            0x08000020UL, &snapshot, &timing);
        return ABORTIDX_DATA_CANNOT_BE_READ_OR_STORED;
    }
    source = object->pVarPtr;
    if (((uintptr_t)source >= (uintptr_t)&gDriveTelemetry) &&
        ((uintptr_t)source + size <= (uintptr_t)&gDriveTelemetry + sizeof(gDriveTelemetry)))
        source = (const uint8_t *)&snapshot + ((uintptr_t)source - (uintptr_t)&gDriveTelemetry);
    MEMCPY(data, source, size);
    return 0U;
}

UINT8 WriteDriveTelemetry(UINT16 index, UINT8 sub, UINT32 size,
    UINT16 MBXMEM *data, UINT8 complete)
{
    uint32_t value;
    uint32_t *target;
    if (complete)
        return ABORTIDX_COMPLETE_ACCESS_NOT_SUPPORTED;
    if (sub != 0U)
        return ABORTIDX_SUBINDEX_NOT_EXISTING;
    if (size != 4U)
        return ABORTIDX_PARAM_LENGTH_ERROR;
    if (((nAlStatus & STATE_MASK) != STATE_PREOP) || gEcatMotorEnabled || DriveBoot_Busy() ||
        !EthercatMotorControl_IsStopped())
        return ABORTIDX_IN_THIS_STATE_DATA_CANNOT_BE_READ_OR_STORED;
    if (!DriveParameters_CanWrite())
        return ABORTIDX_IN_THIS_STATE_DATA_CANNOT_BE_READ_OR_STORED;
    value = SWAPWORD(data[0]) | ((uint32_t)SWAPWORD(data[1]) << 16U);
    switch (index) {
        case 0x2010:
            if ((value < 2800U) || (value > 3600U))
                return ABORTIDX_VALUE_EXCEEDED;
            target = &gDriveTelemetryConfig.vrefMv;
            break;
        case 0x2011:
            if ((value != 0U) && ((value < 10000U) || (value > 1000000U)))
                return ABORTIDX_VALUE_EXCEEDED;
            target = &gDriveTelemetryConfig.sensitivityUvA;
            break;
        case 0x2012:
        case 0x2013:
            if ((value < 500000U) || (value > 2800000U))
                return ABORTIDX_VALUE_EXCEEDED;
            target = (index == 0x2012) ? &gDriveTelemetryConfig.zeroAUv : &gDriveTelemetryConfig.zeroBUv;
            break;
        case 0x6075:
            if ((value != 0U) && ((value < 100U) || (value > 10000U)))
                return ABORTIDX_VALUE_EXCEEDED;
            target = &gDriveTelemetryConfig.ratedCurrentMa;
            break;
        default:
            return ABORTIDX_UNSUPPORTED_ACCESS;
    }
    *target = value;
    /* Immediately invalidate readings from the previous calibration. */
    sConverted[sConvertedIndex].values.sampleCount = 0U;
    sLastUpdate = DWT->CYCCNT - SystemCoreClock / 1000U;
    DriveTelemetry_Latch();
    return 0U;
}
