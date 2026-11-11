/**
 * @file    DriveHomingImpl.h
 * @brief   回零状态处理
 * @details 在运动控制翻译单元内维护回零状态、输入判定及运动指令。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    由EthercatMotorControl.c包含，保留现有无保护结构。
 */
/* Private implementation included after EthercatMotorControl's ISR-owned state.
 * Only HM executes this state machine; existing CSP filters remain unchanged. */
THomingConfig gDriveHomingConfig = {37, 0, 50000, 30000, 10000};
THomingSpeeds gDriveHomingSpeeds = {2, 500, 83};
volatile THomingDiagnostics gDriveHomingDiag = {0};
static volatile u8 sHmAbortRequested = 1U;
static u8 sHmPrevStart = 1U, sHmDivider, sHmDebounce[3], sHmLimits;
static s32 sHmRequest, sHmSearch, sHmRelease, sHmDirection, sHmReferenceDirection;
static u8 sHmSensorMask, sHmSearchLevel, sHmReferenceLevel;
static UINT32 sHmRateWhole, sHmRateRemainder, sHmRateFraction, sHmRateDivisor;
static signed long long sHmStartPhase, sHmEdgePhase, sHmMaximumPhase;

static UINT32 HomingInputs(void)
{
    UINT32 x1 = io_read_input(IO_INPUT_X1), x2 = io_read_input(IO_INPUT_X2);
    UINT32 x3 = io_read_input(IO_INPUT_X3), x4 = io_read_input(IO_INPUT_X4);
    return x2 | (x3 << 1) | (x1 << 2) |
        (x1 << 16) | (x2 << 17) | (x3 << 18) | (x4 << 19);
}
static void HomingInit(s32 resolution)
{
    memset((void *)&gDriveHomingDiag, 0, sizeof(gDriveHomingDiag));
    gDriveHomingDiag.version = 0x20261102UL;
    gDriveHomingConfig.method = 37;
    gDriveHomingConfig.offset = 0;
    gDriveHomingConfig.acceleration = (UINT32)resolution * 50U; /* 3000 rpm/s */
    gDriveHomingConfig.timeoutMs = 30000U;
    gDriveHomingConfig.maximumTravel = (UINT32)resolution * 10U;
    gDriveHomingSpeeds.count = 2U;
    gDriveHomingSpeeds.search = (UINT32)resolution / 2U; /* 30 rpm */
    gDriveHomingSpeeds.release = (UINT32)resolution / 12U; /* about 5 rpm */
    if (!gDriveHomingSpeeds.search)
        gDriveHomingSpeeds.search = 1;
    if (!gDriveHomingSpeeds.release)
        gDriveHomingSpeeds.release = 1;
    sHmAbortRequested = sHmPrevStart = 1U;
    sHmRequest = 0;
    sHmDivider = sHmLimits = 0;
    memset(sHmDebounce, 0, sizeof(sHmDebounce));
}
static u8 HomingMethodSupported(int8_t method)
{
    return ((method >= 17 && method <= 22) || method == 37) ? 1U : 0U;
}
static u8 HomingUsesHomeSwitch(void)
{
    return (gDriveHomingConfig.method >= 19 && gDriveHomingConfig.method <= 22) ? 1U : 0U;
}
static u8 HomingBusy(void)
{
    UINT16 state = gDriveHomingDiag.state;
    return (state == 1 || state == 2 || state == 3 || state == 4 || state == 6 || state == 8);
}
static void HomingError(UINT16 error)
{
    if (!gDriveHomingDiag.error)
        gDriveHomingDiag.error = error;
    gDriveHomingDiag.state = 6;
    sHmRequest = 0;
}
static u8 HomingRebase(void)
{
    signed long long phase = sPulsePosition.phase - sHmEdgePhase +
        EcatPulse_TargetPhase(gDriveHomingConfig.offset, sPulsePosition.resolution);
    signed long long scaled;
    if (phase > sPulsePosition.maximum_phase || phase < sPulsePosition.minimum_phase)
        return 0U;
    scaled = phase * sPulsePosition.resolution;
    sPulsePosition.phase = phase;
    sPulsePosition.whole_pulses = (s32)(scaled / ECAT_PHASE_UNITS_PER_REV);
    sPulsePosition.fraction = (s32)(scaled % ECAT_PHASE_UNITS_PER_REV);
    sPositionActual = EcatPulse_Position(&sPulsePosition);
    sPhaseFeedback = phase;
    gEcatPulseDiag.countedPosition = sPositionActual;
    sCspResetRequested = 1U;
    return 1U;
}
static u8 HomingStart(void)
{
    UINT32 resolution = (UINT32)sPulsePosition.resolution;
    UINT32 maximumSpeed = resolution * 100U / 60U;
    u8 sensor;
    unsigned long long rate;
    gDriveHomingDiag.error = 0;
    gDriveHomingDiag.elapsedMs = 0;
    if (!HomingMethodSupported(gDriveHomingConfig.method) || !gDriveHomingConfig.acceleration ||
        gDriveHomingConfig.acceleration > resolution * 10000U / 60U ||
        !gDriveHomingSpeeds.search || !gDriveHomingSpeeds.release ||
        gDriveHomingSpeeds.search > maximumSpeed ||
        gDriveHomingSpeeds.release > gDriveHomingSpeeds.search ||
        gDriveHomingConfig.timeoutMs < 100U || gDriveHomingConfig.timeoutMs > 600000U ||
        !gDriveHomingConfig.maximumTravel || gDriveHomingConfig.maximumTravel > resolution * 1000U) {
        HomingError(1);
        return 0;
    }
    if ((gDriveHomingDiag.inputs & 3U) == 3U) {
        HomingError(3);
        return 0;
    }
    sHmLimits = (u8)(gDriveHomingDiag.inputs & 7U);
    memset(sHmDebounce, 0, sizeof(sHmDebounce));
    sHmStartPhase = sPulsePosition.phase;
    sHmMaximumPhase = EcatPulse_TargetPhase((INT32)gDriveHomingConfig.maximumTravel,
        sPulsePosition.resolution);
    sHmRateDivisor = resolution * ECAT_MOTOR_CONTROL_HZ;
    rate = (unsigned long long)gDriveHomingConfig.acceleration * 60U * ECAT_MOTOR_BASE_UNITS_PER_RPM;
    sHmRateWhole = (UINT32)(rate / sHmRateDivisor);
    sHmRateRemainder = (UINT32)(rate % sHmRateDivisor);
    sHmRateFraction = 0;
    if (sAppliedBaseRpm || gEcatPulseDiag.lastPhaseStep) {
        HomingError(2);
        return 0;
    }
    sHmSearch = (s32)((unsigned long long)gDriveHomingSpeeds.search * 960U / resolution);
    sHmRelease = (s32)((unsigned long long)gDriveHomingSpeeds.release * 960U / resolution);
    if (!sHmSearch || !sHmRelease) {
        HomingError(1);
        return 0;
    }
    gDriveHomingDiag.startCount++;
    if (gDriveHomingConfig.method == 37) {
        sHmEdgePhase = sPulsePosition.phase;
        gDriveHomingDiag.referencePosition = sPositionActual;
        gDriveHomingDiag.state = 4;
        sHmRequest = 0;
    } else {
        sHmReferenceLevel = 0U;
        switch (gDriveHomingConfig.method) {
            case 17:
                sHmSensorMask = 1U;
                sHmReferenceDirection = 1;
                break;
            case 18:
                sHmSensorMask = 2U;
                sHmReferenceDirection = -1;
                break;
            case 19:
                sHmSensorMask = 4U;
                sHmReferenceDirection = -1;
                break;
            case 20:
                sHmSensorMask = 4U;
                sHmReferenceDirection = 1;
                sHmReferenceLevel = 1U;
                break;
            case 21:
                sHmSensorMask = 4U;
                sHmReferenceDirection = 1;
                break;
            default:
                sHmSensorMask = 4U;
                sHmReferenceDirection = -1;
                sHmReferenceLevel = 1U;
                break; /* 22 */
        }
        sHmSearchLevel = (u8)!sHmReferenceLevel;
        sHmDirection = -sHmReferenceDirection;
        sensor = (sHmLimits & sHmSensorMask) ? 1U : 0U;
        /* If already on the approach side, go directly at reference speed.
         * Otherwise search away, stop, then approach from the specified side. */
        gDriveHomingDiag.state = (sensor == sHmSearchLevel) ? 3 : 1;
        sHmRequest = (gDriveHomingDiag.state == 3) ? sHmReferenceDirection * sHmRelease : sHmDirection * sHmSearch;
    }
    return 1;
}
/* Low-cost fractional ramp: divide once at start, never at 16 kHz. */
static s32 HomingRamp(s32 target)
{
    UINT32 step;
    if (target == sAppliedBaseRpm) {
        sHmRateFraction = 0;
        return target;
    }
    step = sHmRateWhole;
    sHmRateFraction += sHmRateRemainder;
    if (sHmRateFraction >= sHmRateDivisor) {
        sHmRateFraction -= sHmRateDivisor;
        step++;
    }
    if (target > sAppliedBaseRpm) {
        sAppliedBaseRpm += (s32)step;
        if (sAppliedBaseRpm > target)
            sAppliedBaseRpm = target;
    } else {
        sAppliedBaseRpm -= (s32)step;
        if (sAppliedBaseRpm < target)
            sAppliedBaseRpm = target;
    }
    sRampAccumulator = 0;
    return sAppliedBaseRpm;
}
static s32 HomingStep(void)
{
    UINT16 control = LocalAxes[0].Objects.objControlWord;
    u8 start = (control & 0x10U) ? 1U : 0U;
    UINT32 inputs, mask, i;
    u8 sensor;
    s32 direction;
    signed long long excursion;
    if (sHmAbortRequested) {
        sHmAbortRequested = 0;
        gDriveHomingDiag.state = 0;
        gDriveHomingDiag.error = 0;
        sHmRequest = 0;
        sHmPrevStart = 1U;
        sHmDivider = 0;
        sHmLimits = 0;
        memset(sHmDebounce, 0, sizeof(sHmDebounce));
    }
    if (++sHmDivider >= 16U) {
        sHmDivider = 0;
        inputs = HomingInputs();
        gDriveHomingDiag.inputs = inputs;
        for (i = 0; i < 3; i++) {
            mask = 1UL << i;
            if (!!(inputs & mask) == !!(sHmLimits & mask))
                sHmDebounce[i] = 0;
            else if (++sHmDebounce[i] >= 3U) {
                sHmLimits ^= (u8)mask;
                sHmDebounce[i] = 0;
            }
        }
        if (!start || (control & 0x100U)) {
            if (HomingBusy() && gDriveHomingDiag.state != 6) {
                gDriveHomingDiag.state = 8;
                sHmRequest = 0;
            }
        } else if (!sHmPrevStart && !(control & 0x100U) && !HomingBusy()) {
            HomingStart();
        }
        sHmPrevStart = start;
        if (HomingBusy()) {
            gDriveHomingDiag.elapsedMs++;
            excursion = sPulsePosition.phase - sHmStartPhase;
            if (excursion < 0)
                excursion = -excursion;
            if (gDriveHomingDiag.state != 6 && gDriveHomingDiag.state != 8) {
                if (gDriveHomingDiag.elapsedMs >= gDriveHomingConfig.timeoutMs)
                    HomingError(5);
                else if (excursion > sHmMaximumPhase)
                    HomingError(6);
                else if ((inputs & 3U) == 3U)
                    HomingError(3);
                else if (HomingUsesHomeSwitch()) {
                    /* 19..22 do not implement limit reversal (23..30).
                     * A limit in the movement direction stops this HM attempt.
                     * Preserve existing immediate raw limit fault behavior. */
                    direction = sAppliedBaseRpm ? sAppliedBaseRpm : ((gDriveHomingDiag.state == 2) ? sHmReferenceDirection : sHmRequest);
                    if ((direction < 0) && (inputs & 1U))
                        HomingError(8);
                    else if ((direction > 0) && (inputs & 2U))
                        HomingError(9);
                }
            }
        }
        sensor = (sHmLimits & sHmSensorMask) ? 1U : 0U;
        switch (gDriveHomingDiag.state) {
            case 1:
                if (sensor == sHmSearchLevel) {
                    gDriveHomingDiag.state = 2;
                    sHmRequest = 0;
                }
                break;
            case 2:
                if (!sAppliedBaseRpm) {
                    /* Reject a HOME window/pulse lost during braking instead of
                 * silently referencing the wrong edge after the reversal. */
                    if (HomingUsesHomeSwitch() && sensor != sHmSearchLevel)
                        HomingError(10);
                    else {
                        gDriveHomingDiag.state = 3;
                        sHmRequest = sHmReferenceDirection * sHmRelease;
                    }
                }
                break;
            case 3:
                if (!HomingUsesHomeSwitch() && (inputs & (sHmSensorMask ^ 3U))) {
                    HomingError(4);
                    break;
                }
                if (sensor == sHmReferenceLevel) {
                    sHmEdgePhase = sPulsePosition.phase;
                    gDriveHomingDiag.referencePosition = sPositionActual;
                    gDriveHomingDiag.state = 4;
                    sHmRequest = 0;
                }
                break;
            case 4:
                if (!sAppliedBaseRpm) {
                    if (HomingRebase())
                        gDriveHomingDiag.state = 5;
                    else
                        HomingError(7);
                }
                break;
            case 6:
                if (!sAppliedBaseRpm)
                    gDriveHomingDiag.state = 7;
                break;
            case 8:
                if (!sAppliedBaseRpm)
                    gDriveHomingDiag.state = 0;
                break;
            default:
                break;
        }
    }
    if (!HomingBusy() && !sHmRequest)
        return sAppliedBaseRpm;
    return HomingRamp(sHmRequest);
}

UINT8 ReadDriveHoming(UINT16 index, UINT8 sub, UINT32 size, UINT16 MBXMEM *data, UINT8 complete)
{
    UINT32 value = 0, expected = 4;
    if (complete)
        return ABORTIDX_COMPLETE_ACCESS_NOT_SUPPORTED;
    if ((index != 0x6099 && sub) || (index == 0x6099 && sub > 2))
        return ABORTIDX_SUBINDEX_NOT_EXISTING;
    switch (index) {
        case 0x607C:
            value = (UINT32)gDriveHomingConfig.offset;
            break;
        case 0x6098:
            value = (UINT8)gDriveHomingConfig.method;
            expected = 1;
            break;
        case 0x6099:
            if (!sub) {
                value = 2;
                expected = 1;
            } else
                value = (sub == 1) ? gDriveHomingSpeeds.search : gDriveHomingSpeeds.release;
            break;
        case 0x609A:
            value = gDriveHomingConfig.acceleration;
            break;
        case 0x60FD:
            value = HomingInputs();
            break;
        case 0x20E0:
            value = gDriveHomingDiag.state;
            expected = 2;
            break;
        case 0x20E1:
            value = gDriveHomingConfig.timeoutMs;
            break;
        case 0x20E2:
            value = gDriveHomingConfig.maximumTravel;
            break;
        case 0x20E3:
            value = gDriveHomingDiag.error;
            expected = 2;
            break;
        case 0x20E4:
            value = gDriveHomingDiag.version;
            break;
        default:
            return ABORTIDX_UNSUPPORTED_ACCESS;
    }
    if (size != expected)
        return ABORTIDX_PARAM_LENGTH_ERROR;
    data[0] = SWAPWORD((UINT16)value);
    if (expected == 4)
        data[1] = SWAPWORD((UINT16)(value >> 16));
    return 0;
}
UINT8 WriteDriveHoming(UINT16 index, UINT8 sub, UINT32 size, UINT16 MBXMEM *data, UINT8 complete)
{
    UINT32 value, expected = (index == 0x6098) ? 1U : 4U;
    UINT32 resolution = (UINT32)sPulsePosition.resolution;
    if (!DriveParameters_CanWrite())
        return ABORTIDX_IN_THIS_STATE_DATA_CANNOT_BE_READ_OR_STORED;
    if (complete)
        return ABORTIDX_COMPLETE_ACCESS_NOT_SUPPORTED;
    if ((index != 0x6099 && sub) || (index == 0x6099 && (sub < 1 || sub > 2)))
        return ABORTIDX_SUBINDEX_NOT_EXISTING;
    if (size != expected)
        return ABORTIDX_PARAM_LENGTH_ERROR;
    if ((nAlStatus & STATE_MASK) != STATE_PREOP || gEcatMotorEnabled ||
        !EthercatMotorControl_IsStopped() || DriveBoot_Busy())
        return ABORTIDX_IN_THIS_STATE_DATA_CANNOT_BE_READ_OR_STORED;
    value = SWAPWORD(data[0]);
    if (expected == 4)
        value |= (UINT32)SWAPWORD(data[1]) << 16;
    if (expected == 1)
        value &= 0xFFU;
    switch (index) {
        case 0x607C:
            if ((INT32)value < -2000000000L || (INT32)value > 2000000000L)
                return ABORTIDX_VALUE_EXCEEDED;
            gDriveHomingConfig.offset = (INT32)value;
            break;
        case 0x6098:
            if (!HomingMethodSupported((int8_t)value))
                return ABORTIDX_VALUE_EXCEEDED;
            gDriveHomingConfig.method = (int8_t)value;
            break;
        case 0x6099:
            if (!value || value > resolution * 100U / 60U || (unsigned long long)value * 960U < resolution)
                return ABORTIDX_VALUE_EXCEEDED;
            if (sub == 1) {
                if (value < gDriveHomingSpeeds.release)
                    return ABORTIDX_VALUE_EXCEEDED;
                gDriveHomingSpeeds.search = value;
            } else {
                if (value > gDriveHomingSpeeds.search)
                    return ABORTIDX_VALUE_EXCEEDED;
                gDriveHomingSpeeds.release = value;
            }
            break;
        case 0x609A:
            if (!value || value > resolution * 10000U / 60U)
                return ABORTIDX_VALUE_EXCEEDED;
            gDriveHomingConfig.acceleration = value;
            break;
        case 0x20E1:
            if (value < 100 || value > 600000)
                return ABORTIDX_VALUE_EXCEEDED;
            gDriveHomingConfig.timeoutMs = value;
            break;
        case 0x20E2:
            if (!value || value > resolution * 1000U)
                return ABORTIDX_VALUE_EXCEEDED;
            gDriveHomingConfig.maximumTravel = value;
            break;
        default:
            return ABORTIDX_UNSUPPORTED_ACCESS;
    }
    return 0;
}
