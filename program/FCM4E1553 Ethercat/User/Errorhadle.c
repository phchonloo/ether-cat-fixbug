/**
 * @file    Errorhadle.c
 * @brief   错误锁存与指示
 * @details 衔接原厂错误码、驱动故障原因、复位入口及指示灯控制。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    保留现有文件名及工程引用。
 */
//error.c
#include "Errorhadle.h"
#include "DriveFault.h"
#include "DriveSafety.h"
s32 ErrMoudleNum = 0, ErrTemNum = 0, ErrVolHNum = 0, ErrVolLNum = 0;
volatile u8 ErrNumFlag = 0;
u16 u16VendorErrorCode = 0;
u8 ErrFlag = 0;
u32 LedErrorC = 0; //一个周期内闪烁次数的倒数(一次闪烁时间)
u32 LedPerriodTime_2 = 0; //闪烁周期变量
u8 LedError_3 = 0, LedMin = 0;

s8 ErrI[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, ErrI_i = 15, ErrI_sum = 16;


void ErrorHandle(void)
{
    //----------判断模块是否保护----------------------------------

    ErrI_sum -= ErrI[ErrI_i];
    ErrI[ErrI_i] = IoVfoGet(); /* STN245M V1.2: PB14, low-active VFO. */
    ErrI_sum += ErrI[ErrI_i];
    ErrI_i++;
    if (ErrI_i > 15)
        ErrI_i = 0;
    if (ErrI_sum <= 2) {
        /* VFO is one low-active driver protection output.  It does not
         * identify an open motor phase or a DC-bus voltage fault. */
        ErrorHandle_ReportFault(10U);
    }


    //  if(ErrNumFlag>0)
    //      LedError(ErrNumFlag);
}

void ErrorHandle_ReportFault(u8 vendor_code)
{
    ErrorHandle_ReportFaultReason(vendor_code,
        (vendor_code == 10U) ? DF_REASON_VFO : (vendor_code == 13U) ? DF_REASON_UNDERVOLTAGE
            : (vendor_code == 14U)                                  ? DF_REASON_OVERVOLTAGE
                                                                    : 0U);
}

void ErrorHandle_ReportFaultReason(u8 vendor_code, u16 reason)
{
    u32 irq;
    u8 latched = 0U;
    /* Codes from the iD256-EC manual.  Other detectors may use this entry
     * point only after their physical measurement has been validated. */
    if ((vendor_code != 10U) && (vendor_code != 11U) &&
        (vendor_code != 13U) && (vendor_code != 14U) && (vendor_code != 15U)) {
        return;
    }
    irq = __get_PRIMASK();
    __disable_irq();
    /* ISR and foreground share one first-cause latch. Only scalar stores
     * under the mask; hardware PWM writes remain outside it. */
    if (ErrNumFlag == 0U) {
        DriveSafety_RequireRearm();
        gDriveFaultReason = reason;
        ++gDriveFaultDiag.faultSequence;
        u16VendorErrorCode = vendor_code;
        ErrNumFlag = vendor_code;
        ready_ad = 0U;
        latched = 1U;
    }
    __set_PRIMASK(irq);
    if (latched)
        UpPwm(0U, 0U, 0U, 0U);
}

u8 ErrorHandle_TryReset(void)
{
    u32 irq;
    u8 observed = ErrNumFlag;
    ++gDriveFaultDiag.resetRequestCount;
    irq = __get_PRIMASK();
    __disable_irq();
    /* Recheck the live samples and interlocks before committing the reset. */
    if (observed && !DriveFault_CanReset()) {
        gDriveFaultDiag.lastResetBlockMask = gDriveFaultResetBlockMask;
        __set_PRIMASK(irq);
        return 0U;
    }
    /* Do not acknowledge an asserted or recently asserted VFO input. */
    if ((ErrNumFlag != observed) || (IoVfoGet() == 0U) || (ErrI_sum != 16)) {
        __set_PRIMASK(irq);
        return 0U;
    }
    ErrNumFlag = 0U;
    u16VendorErrorCode = 0U;
    u16ErrorRegister = 0U;
    ErrFlag = 0U;
    ++gDriveFaultDiag.resetAcceptedCount;
    gDriveFaultDiag.lastResetBlockMask = 0U;
    DriveFault_OnReset();
    __set_PRIMASK(irq);
    return 1U;
}


void LedError(u8 ErrNum)
{
    /* ABI for N32G455.lib. DriveFault_Tick owns PA8 even when MainFunt
     * is bypassed. PC10/Y1 is an external output, not a RUN LED. */
    (void)ErrNum;
}
