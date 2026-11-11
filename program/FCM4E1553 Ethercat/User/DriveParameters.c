/**
 * @file    DriveParameters.c
 * @brief   参数持久化
 * @details 通过CoE接口请求保存，并在前台执行参数快照、CRC校验和双页Flash提交。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    参数记录布局与提交顺序保持原实现。
 */
#include "DriveParameters.h"
#include "DriveSafety.h"
#include "DriveCurrent.h"
#include "DriveFault.h"
#include "DriveBoot.h"
#include "DriveHoming.h"
#include "EthercatMotorControl.h"
#include "Errorhadle.h"
#include "cia402appl.h"
extern TCiA402Axis LocalAxes[MAX_AXES];
#include "coeappl.h"
#include "n32g45x_flash.h"
#include "qspi.h"
#include <stddef.h>
#define DP_MAGIC 0x5041524DUL
#define DP_COMMIT 0x434F4D54UL
#define DP_ERROR_STATE 1U
#define DP_ERROR_POWER 2U
#define DP_ERROR_ERASE 3U
#define DP_ERROR_PROGRAM 4U
#define DP_ERROR_VERIFY 5U
#define DP_ERROR_RECORD 6U
enum { P_UNITS,
    P_PROFILE,
    P_MAX,
    P_ACCEL,
    P_DECEL,
    P_QUICK,
    P_CURRENT,
    P_HOLD,
    P_DELAY,
    P_METHOD,
    P_OFFSET,
    P_SEARCH,
    P_RELEASE,
    P_HM_ACCEL,
    P_TIMEOUT,
    P_TRAVEL,
    P_VREF,
    P_GAIN,
    P_RATED,
    P_DETECTORS,
    P_OC,
    P_QOPT,
    P_SOPT,
    P_DOPT,
    P_HOPT,
    P_FOPT,
    P_COUNT };
typedef struct {
    uint32_t magic, schema, length, generation, resolution;
    uint32_t value[P_COUNT];
    uint32_t crc, commit;
} ParameterRecord;
volatile DriveParameterDiagnostics gDriveParameterDiag;
static ParameterRecord sFactory, sPending;
static uint8_t sRestore;

static uint32_t Crc(const ParameterRecord *r)
{
    const uint8_t *p = (const uint8_t *)r;
    uint32_t crc = 0xFFFFFFFFUL, i, k;
    for (i = 0; i < offsetof(ParameterRecord, crc); ++i) {
        crc ^= p[i];
        for (k = 0; k < 8U; ++k)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
    }
    return crc ^ 0xFFFFFFFFUL;
}
static void Capture(ParameterRecord *r)
{
    CiA402Objects *o = &LocalAxes[0].Objects;
    uint32_t *v = r->value;
    memset(r, 0, sizeof(*r));
    r->magic = DP_MAGIC;
    r->schema = DP_SCHEMA;
    r->length = sizeof(*r);
    r->resolution = (uint32_t)gEcatPulseDiag.resolution;
    v[P_UNITS] = o->objMotionUnits;
    v[P_PROFILE] = o->objProfileVelocity;
    v[P_MAX] = o->objMaxProfileVelocity;
    v[P_ACCEL] = o->objProfileAcceleration;
    v[P_DECEL] = o->objProfileDeceleration;
    v[P_QUICK] = o->objQuickStopDeclaration;
    v[P_CURRENT] = (uint16_t)gDriveCurrentConfig.runPeakMa;
    v[P_HOLD] = gDriveCurrentConfig.holdPercent;
    v[P_DELAY] = gDriveCurrentConfig.holdDelayMs;
    v[P_METHOD] = (uint32_t)(int32_t)gDriveHomingConfig.method;
    v[P_OFFSET] = (uint32_t)gDriveHomingConfig.offset;
    v[P_SEARCH] = gDriveHomingSpeeds.search;
    v[P_RELEASE] = gDriveHomingSpeeds.release;
    v[P_HM_ACCEL] = gDriveHomingConfig.acceleration;
    v[P_TIMEOUT] = gDriveHomingConfig.timeoutMs;
    v[P_TRAVEL] = gDriveHomingConfig.maximumTravel;
    v[P_VREF] = gDriveTelemetryConfig.vrefMv;
    v[P_GAIN] = gDriveTelemetryConfig.sensitivityUvA;
    v[P_RATED] = gDriveTelemetryConfig.ratedCurrentMa;
    v[P_DETECTORS] = gDriveFaultConfig.currentEnable;
    v[P_OC] = gDriveFaultConfig.overcurrentMa;
    v[P_QOPT] = (uint16_t)o->objQuickStopOptionCode;
    v[P_SOPT] = (uint16_t)o->objShutdownOptionCode;
    v[P_DOPT] = (uint16_t)o->objDisableOperationOptionCode;
    v[P_HOPT] = (uint16_t)o->objHaltOptionCode;
    v[P_FOPT] = (uint16_t)o->objFaultReactionCode;
}
static uint8_t ValidValues(const ParameterRecord *r)
{
    const uint32_t *v = r->value;
    uint32_t res = r->resolution, min = 1U, speed = 1000U, rate = 10000U, i, m = v[P_METHOD];
    if (!res || res != (uint32_t)gEcatPulseDiag.resolution || v[P_UNITS] > 1U)
        return 0;
    if (v[P_UNITS]) {
        min = (res + 59U) / 60U;
        speed = res * 1000U / 60U;
        rate = res * 10000U / 60U;
    }
    for (i = P_PROFILE; i <= P_QUICK; ++i)
        if (v[i] < min || v[i] > ((i == P_PROFILE || i == P_MAX) ? speed : rate))
            return 0;
    if ((v[P_CURRENT] && (v[P_CURRENT] < 100U || v[P_CURRENT] > 3000U)) ||
        v[P_HOLD] < 10U || v[P_HOLD] > 100U || v[P_DELAY] < 1U || v[P_DELAY] > 30000U)
        return 0;
    if (!((m >= 17U && m <= 22U) || m == 37U) || (int32_t)v[P_OFFSET] < -2000000000L ||
        (int32_t)v[P_OFFSET] > 2000000000L)
        return 0;
    if (!v[P_SEARCH] || v[P_SEARCH] > res * 100U / 60U || !v[P_RELEASE] ||
        v[P_RELEASE] > v[P_SEARCH] || (uint64_t)v[P_RELEASE] * 960U < res ||
        !v[P_HM_ACCEL] || v[P_HM_ACCEL] > res * 10000U / 60U ||
        v[P_TIMEOUT] < 100U || v[P_TIMEOUT] > 600000U ||
        !v[P_TRAVEL] || v[P_TRAVEL] > res * 1000U)
        return 0;
    if (v[P_VREF] < 2800U || v[P_VREF] > 3600U ||
        (v[P_GAIN] && (v[P_GAIN] < 10000U || v[P_GAIN] > 1000000U)) ||
        (v[P_RATED] && (v[P_RATED] < 100U || v[P_RATED] > 10000U)) ||
        v[P_DETECTORS] > 3U || v[P_OC] < 500U || v[P_OC] > 6000U)
        return 0;
    if (!(v[P_QOPT] == 0U || v[P_QOPT] == 1U || v[P_QOPT] == 2U || v[P_QOPT] == 5U || v[P_QOPT] == 6U) ||
        v[P_SOPT] > 1U || v[P_DOPT] > 1U || (v[P_HOPT] != 1U && v[P_HOPT] != 2U) || v[P_FOPT] > 2U)
        return 0;
    return 1;
}
static uint8_t Valid(const ParameterRecord *r)
{
    return r->magic == DP_MAGIC && r->schema == DP_SCHEMA && r->length == sizeof(*r) &&
        r->commit == DP_COMMIT && r->crc == Crc(r) && ValidValues(r);
}
static const ParameterRecord *SelectStored(void)
{
    const ParameterRecord *a = (const ParameterRecord *)DP_SLOT_A, *b = (const ParameterRecord *)DP_SLOT_B;
    uint8_t va = Valid(a), vb = Valid(b);
    gDriveParameterDiag.validMask = (va ? 1U : 0U) | (vb ? 2U : 0U);
    if (va && vb)
        return ((int32_t)(b->generation - a->generation) > 0) ? b : a;
    return va ? a : (vb ? b : 0);
}
static void Apply(const ParameterRecord *r)
{
    const uint32_t *v = r->value;
    CiA402Objects *o = &LocalAxes[0].Objects;
    o->objMotionUnits = (uint16_t)v[P_UNITS];
    o->objProfileVelocity = v[P_PROFILE];
    o->objMaxProfileVelocity = v[P_MAX];
    o->objProfileAcceleration = v[P_ACCEL];
    o->objProfileDeceleration = v[P_DECEL];
    o->objQuickStopDeclaration = v[P_QUICK];
    gDriveCurrentConfig.runPeakMa = (int16_t)v[P_CURRENT];
    gDriveCurrentConfig.holdPercent = (uint16_t)v[P_HOLD];
    gDriveCurrentConfig.holdDelayMs = (uint16_t)v[P_DELAY];
    gDriveHomingConfig.method = (int8_t)v[P_METHOD];
    gDriveHomingConfig.offset = (int32_t)v[P_OFFSET];
    gDriveHomingSpeeds.search = v[P_SEARCH];
    gDriveHomingSpeeds.release = v[P_RELEASE];
    gDriveHomingConfig.acceleration = v[P_HM_ACCEL];
    gDriveHomingConfig.timeoutMs = v[P_TIMEOUT];
    gDriveHomingConfig.maximumTravel = v[P_TRAVEL];
    gDriveTelemetryConfig.vrefMv = v[P_VREF];
    gDriveTelemetryConfig.sensitivityUvA = v[P_GAIN];
    gDriveTelemetryConfig.ratedCurrentMa = v[P_RATED];
    gDriveFaultConfig.currentEnable = (uint16_t)v[P_DETECTORS];
    gDriveFaultConfig.overcurrentMa = (uint16_t)v[P_OC];
    o->objQuickStopOptionCode = (int16_t)v[P_QOPT];
    o->objShutdownOptionCode = (int16_t)v[P_SOPT];
    o->objDisableOperationOptionCode = (int16_t)v[P_DOPT];
    o->objHaltOptionCode = (int16_t)v[P_HOPT];
    o->objFaultReactionCode = (int16_t)v[P_FOPT];
    /* Never restore CW, enable, targets, counted position or a homed flag. */
    o->objControlWord = 0U;
    o->objTargetVelocity = 0;
    o->objTargetPosition = 0;
}
void DriveParameters_Init(void)
{
    const ParameterRecord *selected;
    memset((void *)&gDriveParameterDiag, 0, sizeof(gDriveParameterDiag));
    gDriveParameterDiag.version = DP_SCHEMA;
    Capture(&sFactory);
    selected = SelectStored();
    if (selected) {
        Apply(selected);
        gDriveParameterDiag.loadedSlot = ((uint32_t)selected == DP_SLOT_A) ? 1U : 2U;
        gDriveParameterDiag.generation = selected->generation;
    }
    sRestore = 0U;
    DriveSafety_RequireRearm();
}
uint8_t DriveParameters_Busy(void)
{
    return gDriveParameterDiag.status == DP_PENDING || gDriveParameterDiag.restorePending;
}
uint8_t DriveParameters_CanWrite(void)
{
    return !DriveParameters_Busy();
}
uint8_t DriveParameters_PowerHealthy(void)
{
    DriveSafetySample v;
    DriveTelemetry_GetSafetySample(&v);
    return (v.status & DT_VALID_VOLTAGE) && v.busMv >= 19000U && v.busMv <= 29000U &&
        !(PWR->CTRLSTS & PWR_CTRLSTS_PVDO) && !gDriveSafetyDiag.powerLossLatched;
}
static uint8_t StateSafe(void)
{
    return (nAlStatus & STATE_MASK) == STATE_PREOP && !gEcatMotorEnabled &&
        EthercatMotorControl_IsStopped() && !DriveBoot_Busy() && !ErrNumFlag &&
        !(LocalAxes[0].Objects.objControlWord & 0xFU) && !gEscPdiDiag.faultLatched;
}
uint32_t DriveParameters_Request(uint8_t restore)
{
    if (DriveParameters_Busy() || !StateSafe() || !DriveParameters_PowerHealthy())
        return ABORTIDX_IN_THIS_STATE_DATA_CANNOT_BE_READ_OR_STORED;
    gDriveParameterDiag.status = DP_PENDING;
    gDriveParameterDiag.error = 0U;
    sRestore = restore;
    if (restore)
        sPending = sFactory;
    else
        Capture(&sPending);
    if (!ValidValues(&sPending)) {
        gDriveParameterDiag.status = DP_FAILED;
        gDriveParameterDiag.error = DP_ERROR_RECORD;
        return ABORTIDX_VALUE_EXCEEDED;
    }
    __DMB();
    return 0U;
}
static void Failed(uint32_t error)
{
    FLASH_ClearFlag(FLASH_STS_CLRFLAG);
    FLASH_Lock();
    gDriveParameterDiag.error = error;
    ++gDriveParameterDiag.failedCount;
    gDriveParameterDiag.status = DP_FAILED;
}
void DriveParameters_Update(void)
{
    uint32_t address, i;
    const ParameterRecord *old, *stored;
    const uint32_t *words;
    ParameterRecord live;
    if (gDriveParameterDiag.status != DP_PENDING) {
        Capture(&live);
        old = (gDriveParameterDiag.loadedSlot == 1U) ? (const ParameterRecord *)DP_SLOT_A : ((gDriveParameterDiag.loadedSlot == 2U) ? (const ParameterRecord *)DP_SLOT_B : 0);
        gDriveParameterDiag.dirty = old ? (memcmp(live.value, old->value, sizeof(live.value)) != 0) : 1U;
        return;
    }
    if (!StateSafe()) {
        Failed(DP_ERROR_STATE);
        return;
    }
    if (!DriveParameters_PowerHealthy()) {
        Failed(DP_ERROR_POWER);
        return;
    }
    old = (gDriveParameterDiag.loadedSlot == 1U) ? (const ParameterRecord *)DP_SLOT_A : ((gDriveParameterDiag.loadedSlot == 2U) ? (const ParameterRecord *)DP_SLOT_B : 0);
    if (old && Valid(old) && !memcmp(old->value, sPending.value, sizeof(sPending.value))) {
        gDriveParameterDiag.status = sRestore ? DP_RESTORE_SAVED : DP_UNCHANGED;
        gDriveParameterDiag.restorePending = sRestore;
        gDriveParameterDiag.dirty = 0U;
        return;
    }
    /* Revalidate both pages before erasing: a runtime bit error in the latest
  * record must not cause us to erase the sole remaining valid fallback. */
    old = SelectStored();
    address = ((uint32_t)old == DP_SLOT_A) ? DP_SLOT_B : DP_SLOT_A;
    sPending.generation = old ? (old->generation + 1U) : 1U;
    sPending.crc = Crc(&sPending);
    sPending.commit = DP_COMMIT;
    words = (const uint32_t *)&sPending;
    DriveSafety_RequireRearm();
    UpPwm(0, 0, 0, 0);
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_STS_CLRFLAG);
    if (FLASH_EraseOnePage(address) != FLASH_COMPL) {
        Failed(DP_ERROR_ERASE);
        return;
    }
    for (i = 0; i < offsetof(ParameterRecord, commit) / 4U; ++i) {
        if (!StateSafe()) {
            Failed(DP_ERROR_STATE);
            return;
        }
        if (!DriveParameters_PowerHealthy()) {
            Failed(DP_ERROR_POWER);
            return;
        }
        if (FLASH_ProgramWord(address + i * 4U, words[i]) != FLASH_COMPL) {
            Failed(DP_ERROR_PROGRAM);
            return;
        }
        if (*(const uint32_t *)(address + i * 4U) != words[i]) {
            Failed(DP_ERROR_VERIFY);
            return;
        }
    }
    /* Commit is the only publication point; old slot is never erased here. */
    if (!StateSafe()) {
        Failed(DP_ERROR_STATE);
        return;
    }
    if (!DriveParameters_PowerHealthy()) {
        Failed(DP_ERROR_POWER);
        return;
    }
    if (FLASH_ProgramWord(address + offsetof(ParameterRecord, commit), DP_COMMIT) != FLASH_COMPL) {
        Failed(DP_ERROR_PROGRAM);
        return;
    }
    FLASH_ClearFlag(FLASH_STS_CLRFLAG);
    FLASH_Lock();
    stored = (const ParameterRecord *)address;
    if (!Valid(stored) || memcmp(stored, &sPending, sizeof(sPending))) {
        Failed(DP_ERROR_VERIFY);
        return;
    }
    gDriveParameterDiag.loadedSlot = (address == DP_SLOT_A) ? 1U : 2U;
    gDriveParameterDiag.validMask |= (address == DP_SLOT_A) ? 1U : 2U;
    gDriveParameterDiag.generation = sPending.generation;
    ++gDriveParameterDiag.saveCount;
    gDriveParameterDiag.dirty = 0U;
    gDriveParameterDiag.restorePending = sRestore;
    __DMB();
    gDriveParameterDiag.status = sRestore ? DP_RESTORE_SAVED : DP_SAVED;
}
UINT8 ReadDriveParameters(UINT16 index, UINT8 sub, UINT32 size, UINT16 MBXMEM *data, UINT8 complete)
{
    uint32_t value = 0U, expected = 4U;
    if (complete)
        return ABORTIDX_COMPLETE_ACCESS_NOT_SUPPORTED;
    if (index == 0x1010U || index == 0x1011U) {
        if (sub > 1U)
            return ABORTIDX_SUBINDEX_NOT_EXISTING;
        value = 1U;
        if (!sub)
            expected = 1U;
    } else {
        if (sub)
            return ABORTIDX_SUBINDEX_NOT_EXISTING;
        switch (index) {
            case 0x20C0:
                value = gDriveParameterDiag.status;
                break;
            case 0x20C1:
                value = gDriveParameterDiag.generation;
                break;
            case 0x20C2:
                value = gDriveParameterDiag.loadedSlot;
                break;
            case 0x20C3:
                value = gDriveParameterDiag.error;
                break;
            case 0x20C4:
                value = gDriveParameterDiag.dirty;
                break;
            case 0x20C5:
                value = gDriveSafetyDiag.restartInhibit;
                break;
            case 0x20C6:
                value = gDriveSafetyDiag.resetFlags;
                break;
            case 0x20C7:
                value = gDriveSafetyDiag.limitStopping;
                break;
            case 0x20C8:
                value = gDriveSafetyDiag.version;
                break;
            case 0x20C9:
                value = gDriveParameterDiag.validMask;
                break;
            default:
                return ABORTIDX_UNSUPPORTED_ACCESS;
        }
    }
    if (size != expected)
        return ABORTIDX_PARAM_LENGTH_ERROR;
    data[0] = SWAPWORD((UINT16)value);
    if (expected == 4U)
        data[1] = SWAPWORD((UINT16)(value >> 16));
    return 0U;
}
UINT8 WriteDriveParameters(UINT16 index, UINT8 sub, UINT32 size, UINT16 MBXMEM *data, UINT8 complete)
{
    uint32_t value;
    if (complete)
        return ABORTIDX_COMPLETE_ACCESS_NOT_SUPPORTED;
    if (sub != 1U)
        return ABORTIDX_SUBINDEX_NOT_EXISTING;
    if (size != 4U)
        return ABORTIDX_PARAM_LENGTH_ERROR;
    value = SWAPWORD(data[0]) | ((uint32_t)SWAPWORD(data[1]) << 16);
    if ((index == 0x1010U && value == 0x65766173UL) ||
        (index == 0x1011U && value == 0x64616F6CUL)) {
        UINT8 result = (UINT8)DriveParameters_Request(index == 0x1011U);
        if (result)
            return result;
        /* A successful SDO response means the Flash commit/readback completed,
   * not merely that a request was queued. PREOP and output-off are mandatory. */
        DriveParameters_Update();
        return (gDriveParameterDiag.status == DP_SAVED ||
                   gDriveParameterDiag.status == DP_UNCHANGED ||
                   gDriveParameterDiag.status == DP_RESTORE_SAVED)
            ? 0U
            : ABORTIDX_DATA_CANNOT_BE_READ_OR_STORED;
    }
    return ABORTIDX_VALUE_EXCEEDED;
}
