/**
 * @file    DriveSafety.c
 * @brief   驱动保护
 * @details 处理前台与中断心跳、供电失效以及方向限位相关状态。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    看门狗喂养条件与直接输出关闭序列保持原实现。
 */
#include "DriveSafety.h"
#include "EthercatMotorControl.h"
#include "DriveFault.h"
#include "DriveParameters.h"
#include "Errorhadle.h"
#include "IoIntInit.h"
#include "cia402appl.h"
extern TCiA402Axis LocalAxes[MAX_AXES];
#include "qspi.h"
#include "n32g45x_pwr.h"
#include "n32g45x_iwdg.h"
#include "n32g45x_exti.h"
volatile DriveSafetyDiagnostics gDriveSafetyDiag = {DS_VERSION, 0U, 1U};
static uint32_t sFedMotor, sFedSys, sBusSequence;
static uint8_t sBusArmed;

void DriveSafety_FatalOutputOff(void)
{
    /* Direct MOE clear also works when software/ADC state is corrupt.
     * Ordinary GPIO/PWM off is not a safety-rated torque-off function. */
    TIM8->BKDT &= (uint16_t)~TIM_BKDT_MOEN;
    TIM8->CCDAT1 = TIM8->CCDAT2 = TIM8->CCDAT3 = TIM8->CCDAT4 = 0U;
}

void DriveSafety_Init(void)
{
    EXTI_InitType exti;
    NVIC_InitType nvic;
    memset((void *)&gDriveSafetyDiag, 0, sizeof(gDriveSafetyDiag));
    gDriveSafetyDiag.version = DS_VERSION;
    gDriveSafetyDiag.restartInhibit = 1U;
    gDriveSafetyDiag.resetFlags = RCC->CTRLSTS;
    RCC_ClrFlag();
    sFedMotor = sFedSys = sBusSequence = 0U;
    sBusArmed = 0U;
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR, ENABLE);
    /* Logic rail is nominally 3.3 V. PVD monitors VDD, not the 24 V bus.
     * Use the SDK's normal 2.9 V range (EXMODE=0), not extended-mode values. */
    PWR->CTRL3 &= (uint32_t)~PWR_CTRL3_EXMODE;
    PWR_PvdRangeConfig(PWR_PVDRANGRE_2V9);
    EXTI_ClrITPendBit(EXTI_LINE16);
    exti.EXTI_Line = EXTI_LINE16;
    exti.EXTI_Mode = EXTI_Mode_Interrupt;
    exti.EXTI_Trigger = EXTI_Trigger_Rising; /* PVDO rises when VDD falls */
    exti.EXTI_LineCmd = ENABLE;
    EXTI_InitPeripheral(&exti);
    nvic.NVIC_IRQChannel = PVD_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 0U;
    nvic.NVIC_IRQChannelSubPriority = 0U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    PWR_PvdEnable(ENABLE);
    if (PWR->CTRLSTS & PWR_CTRLSTS_PVDO)
        DriveSafety_PowerFail(DS_REASON_LOGIC_SUPPLY);
}

void DriveSafety_StartWatchdog(void)
{
    /* ~400 ms at nominal 40 kHz LSI. No debug freeze, no reload in IRQs.
     * The actual timeout follows the silicon's LSI tolerance. */
    PWR->CTRL2 |= PWR_CTRL2_IWDGRSTEN;
    IWDG_WriteConfig(IWDG_WRITE_ENABLE);
    IWDG_SetPrescalerDiv(IWDG_PRESCALER_DIV64);
    IWDG_CntReload(249U);
    IWDG_ReloadKey();
    IWDG_Enable();
    sFedMotor = gDriveSafetyDiag.motorTicks;
    sFedSys = gDriveSafetyDiag.systickTicks;
    gDriveSafetyDiag.watchdogStarted = 1U;
}

void DriveSafety_Update(void)
{
    uint32_t motor = gDriveSafetyDiag.motorTicks, sys = gDriveSafetyDiag.systickTicks;
    if (gDriveSafetyDiag.watchdogStarted && motor != sFedMotor && sys != sFedSys) {
        /* Completing the whole foreground pass AND both periodic paths is
         * required. A live ISR cannot keep a dead main loop alive. */
        IWDG_ReloadKey();
        sFedMotor = motor;
        sFedSys = sys;
        ++gDriveSafetyDiag.watchdogFeeds;
    }
}
void DriveSafety_SysTick(void)
{
    ++gDriveSafetyDiag.systickTicks;
}
void DriveSafety_MotorTick(void)
{
    ++gDriveSafetyDiag.motorTicks;
    if ((PWR->CTRLSTS & PWR_CTRLSTS_PVDO) && !gDriveSafetyDiag.powerLossLatched)
        DriveSafety_PowerFail(DS_REASON_LOGIC_SUPPLY);
    if (gDriveSafetyDiag.limitStopping &&
        (!gEcatMotorEnabled || !(LocalAxes[0].Objects.objControlWord & 2U)))
        DriveSafety_FinishLimit();
}
void DriveSafety_RequireRearm(void)
{
    gDriveSafetyDiag.restartInhibit = 1U;
}
void DriveSafety_OnValidPdo(uint16_t control)
{
    /* Never consume an old OD value / SDO write as a fresh disable command.
     * Require a post-event, successfully received all-low drive command. */
    if (gDriveSafetyDiag.restartInhibit && (nAlStatus & STATE_MASK) == STATE_OP &&
        !ErrNumFlag && !gEscPdiDiag.faultLatched && !DriveParameters_Busy() && !gDriveSafetyDiag.powerLossLatched &&
        (control & 0x008FU) == 0U) {
        gDriveSafetyDiag.restartInhibit = 0U;
        ++gDriveSafetyDiag.rearmCount;
    }
}
uint8_t DriveSafety_PowerAllowed(void)
{
    return !gDriveSafetyDiag.restartInhibit && !gDriveSafetyDiag.powerLossLatched &&
        !(PWR->CTRLSTS & PWR_CTRLSTS_PVDO) && !DriveParameters_Busy();
}
void DriveSafety_PowerFail(uint16_t reason)
{
    /* Do not attempt Flash writes here: hold-up time is not specified. */
    DriveSafety_FatalOutputOff();
    gDriveSafetyDiag.powerLossLatched = 1U;
    DriveSafety_RequireRearm();
    gEcatMotorEnabled = 0U;
    gEcatBaseRpmCommand = 0;
    EthercatMotorControl_EmergencyStop();
    ErrorHandle_ReportFaultReason(13U, reason);
}
void PVD_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_LINE16) != RESET) {
        EXTI_ClrITPendBit(EXTI_LINE16);
        if (PWR->CTRLSTS & PWR_CTRLSTS_PVDO)
            DriveSafety_PowerFail(DS_REASON_LOGIC_SUPPLY);
    }
}
void DriveSafety_ObserveBus(const DriveSafetySample *v)
{
    uint8_t fresh = v->sequence && v->sequence != sBusSequence;
    sBusSequence = v->sequence;
    if (fresh && (v->status & DT_VALID_VOLTAGE) &&
        v->busMv >= 19000U && v->busMv <= 29000U)
        sBusArmed = 1U;
    if (fresh && (v->status & DT_VALID_VOLTAGE) && sBusArmed && v->busMv < 18000U) {
        if (++gDriveSafetyDiag.busLowMs >= 3U && !gDriveSafetyDiag.powerLossLatched)
            DriveSafety_PowerFail(DS_REASON_FAST_BUS_LOSS);
    } else
        gDriveSafetyDiag.busLowMs = 0U;
}
uint16_t DriveSafety_ResetBlockMask(void)
{
    uint16_t mask = 0U;
    if (PWR->CTRLSTS & PWR_CTRLSTS_PVDO)
        mask |= DS_RESET_LOGIC_SUPPLY;
    if (io_read_input(IO_INPUT_X2) && io_read_input(IO_INPUT_X3))
        mask |= DS_RESET_BOTH_LIMITS;
    return mask;
}
void DriveSafety_OnReset(void)
{
    gDriveSafetyDiag.powerLossLatched = 0U;
    gDriveSafetyDiag.busLowMs = 0U;
    gDriveSafetyDiag.limitStopping = 0U;
    gDriveSafetyDiag.limitReason = 0U;
    DriveSafety_RequireRearm();
    /* Fault reset has already qualified 200 ms of healthy samples. Four
     * zero CCR values have crossed many update events before MOE is set. */
    TIM_EnableCtrlPwmOutputs(TIM8, ENABLE);
}
uint8_t DriveSafety_CheckLimit(int32_t requested, int32_t applied)
{
    uint32_t mask = io_read_input(IO_INPUT_X2) | ((uint32_t)io_read_input(IO_INPUT_X3) << 1);
    uint32_t reason = 0U;
    gDriveSafetyDiag.limitMask = mask;
    if (gDriveSafetyDiag.limitStopping)
        return 1U;
    if (mask == 3U)
        reason = DS_REASON_BOTH_LIMITS;
    else if ((mask & 1U) && (applied < 0 || (applied == 0 && requested < 0)))
        reason = DS_REASON_NEGATIVE_LIMIT;
    else if ((mask & 2U) && (applied > 0 || (applied == 0 && requested > 0)))
        reason = DS_REASON_POSITIVE_LIMIT;
    if (!reason)
        return 0U;
    gDriveSafetyDiag.limitReason = reason;
    gDriveSafetyDiag.limitStopping = 1U;
    ++gDriveSafetyDiag.limitTrips;
    if (mask == 3U || applied == 0)
        DriveSafety_FinishLimit();
    return 1U;
}
void DriveSafety_FinishLimit(void)
{
    uint16_t reason = (uint16_t)gDriveSafetyDiag.limitReason;
    if (!reason)
        return;
    gDriveSafetyDiag.limitStopping = 0U;
    DriveSafety_RequireRearm();
    ErrorHandle_ReportFaultReason(15U, reason);
}
