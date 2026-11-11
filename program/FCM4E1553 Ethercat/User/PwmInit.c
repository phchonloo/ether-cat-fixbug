/**
 * @file    PwmInit.c
 * @brief   PWM与电机中断调度
 * @details 配置PWM并在TIM8更新中断中执行现有电机控制流程。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    PWM寄存器、中断顺序及周期诊断保持原实现。
 */
#include "PwmInit.h"
#include "EthercatMotorControl.h"
#include "DriveCurrent.h"
#include "DriveFault.h"
#include "DriveBoot.h"
#include "DriveSafety.h"
#include "qspi.h"
/*
 * CoE Online isolation test:
 * 0 = normal motor control, 1 = no algorithm/PWM, 2 = algorithm with PWM pins off.
 * Mode 2 separates 16 kHz CPU load from motor power-stage interference.
 */
#define PWM_COE_TEST_MODE 0U
#if PWM_COE_TEST_MODE > 2U
#error PWM_COE_TEST_MODE must be 0, 1, or 2
#endif
#define PWM_PINS   (GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9)
#define PWM_CLOCKS (RCC_APB2_PERIPH_GPIOC \
                    | RCC_APB2_PERIPH_AFIO)
u8 PwmFlag = 0U;

/* Live elapsed-time diagnostics; read these while the MCU is running.
 * TIM8 now preempts ESC IRQ/SYNC0. Time masked before TIM8 entry is not
 * included, so compare the serviced IRQ count against SYNC0 as well. */
volatile uint32_t gMotorIsrLoadPercent = 0U;
volatile uint32_t gMotorIsrMaxCycles = 0U;
volatile uint32_t gMotorIsrOverrunCount = 0U;
volatile uint32_t gMotorIsrPeriodCycles = 0U;
volatile uint8_t gMotorCycleCounterAvailable = 0U;

/* Keil Watch diagnostics. At 500 Hz SYNC0, a healthy 16 kHz TIM8 gives
 * about 16000 serviced update interrupts per 500 SYNC0 intervals. This
 * comparison does not depend on SystemCoreClock or the PLL/HSE definition. */
volatile uint32_t gMotorTim8IrqCount = 0U;
volatile uint32_t gMotorTim8IrqsPer500Sync0 = 0U;
volatile uint32_t gMotorTim8OverrunsPer500Sync0 = 0U;
volatile uint32_t gMotorTim8Sync0Samples = 0U;
/* Lifetime maxima for the main TIM8 stages, in DWT CPU cycles. These help
 * distinguish motor algorithm load from the 1 ms fault/safety work. */
volatile uint32_t gMotorPreMainMaxCycles = 0U;
volatile uint32_t gMotorMainFuntMaxCycles = 0U;
volatile uint32_t gMotorFeedbackMaxCycles = 0U;
volatile uint32_t gMotorFaultMaxCycles = 0U;

static uint32_t sMotorIsrWindowCycles = 0U;
static uint8_t sMotorIsrWindowSamples = 0U;
static uint32_t sMotorTim8Sync0StartCount = 0U;
static uint32_t sMotorTim8Sync0StartOverruns = 0U;
static uint16_t sMotorTim8Sync0Intervals = 0U;
static uint8_t sMotorTim8Sync0Started = 0U;

void MotorTim8_RecordSync0Tick(void)
{
    uint32_t current_count = gMotorTim8IrqCount;

    if (sMotorTim8Sync0Started == 0U) {
        sMotorTim8Sync0StartCount = current_count;
        sMotorTim8Sync0StartOverruns = gMotorIsrOverrunCount;
        sMotorTim8Sync0Intervals = 0U;
        sMotorTim8Sync0Started = 1U;
        return;
    }

    sMotorTim8Sync0Intervals++;
    if (sMotorTim8Sync0Intervals >= 500U) {
        /* Unsigned subtraction also handles a 32-bit IRQ counter wrap. */
        gMotorTim8IrqsPer500Sync0 =
            current_count - sMotorTim8Sync0StartCount;
        gMotorTim8OverrunsPer500Sync0 =
            gMotorIsrOverrunCount - sMotorTim8Sync0StartOverruns;
        gMotorTim8Sync0Samples++;
        sMotorTim8Sync0StartCount = current_count;
        sMotorTim8Sync0StartOverruns = gMotorIsrOverrunCount;
        sMotorTim8Sync0Intervals = 0U;
    }
}

/*
 * PWM与ADC同步关系：
 *
 * PC6/TIM8_CH1、PC7/TIM8_CH2、PC8/TIM8_CH3、PC9/TIM8_CH4只负责
 * 输出PWM波形。ADC由TIM8更新TRGO直接触发。因此修改任意一路占空比，
 * 不会改变ADC触发频率；修改TIM8的ARR才会改变PWM和ADC共同频率。
 *
 * TIM8的计数模式、ARR、重复计数器和TRGO统一放在timer.c中配置；
 * 本文件只配置GPIO、四个输出比较通道以及比较值预装载。
 */

/**
 * @brief 初始化TIM8四路独立PWM并启动PWM/ADC共用时基。
 *
 * PC6、PC7、PC8、PC9分别使用TIM8_CH1、CH2、CH3、CH4。
 * 四路共用TIM8的CNT和ARR，所以频率及周期边界完全相同；四个CCR比较值
 * 分别控制各自占空比。TIM8时基和ADC同步触发配置集中在timer.c中。
 */
void pwm_config(void)
{
    GPIO_InitType gpio_init;
    OCInitType compare_init;

    /* DWT observes the motor ISR without a breakpoint or GPIO timing probe. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) == 0U) {
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        gMotorIsrPeriodCycles = SystemCoreClock / 16000U;
        gMotorCycleCounterAvailable =
            (gMotorIsrPeriodCycles != 0U) ? 1U : 0U;
    }
    gEscQspiMaxCriticalCycles = 0U;

    RCC_EnableAPB2PeriphClk(PWM_CLOCKS, ENABLE);

    /* TIM8 默认映射：PC6/PC7/PC8/PC9 对应 CH1/CH2/CH3/CH4。 */
    GPIO_ConfigPinRemap(GPIO_RMP3_TIM8, DISABLE);

    GPIO_ResetBits(GPIOC, PWM_PINS);

    gpio_init.Pin = PWM_PINS;
    gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOC, &gpio_init);

    /*
     * 配置16 kHz中心对齐时基和TRGO，但此处暂不启动TIM8。
     * 等四个PWM通道全部配置完再启动，避免初始化过程中输出不完整波形。
     */
    timer_pwm_adc_init();

    /*
     * PWM1模式通过CNT与CCR比较形成波形。Pulse初始为0，所以刚启动时
     * 四路均为0占空比；后续只由UpPwm()直接修改比较值。
     */
    TIM_InitOcStruct(&compare_init);
    compare_init.OcMode = TIM_OCMODE_PWM1;
    compare_init.OutputState = TIM_OUTPUT_STATE_ENABLE;
    compare_init.OutputNState = TIM_OUTPUT_NSTATE_DISABLE;
    compare_init.Pulse = 0U;
    compare_init.OcPolarity = TIM_OC_POLARITY_HIGH;
    compare_init.OcNPolarity = TIM_OCN_POLARITY_HIGH;
    compare_init.OcIdleState = TIM_OC_IDLE_STATE_RESET;
    compare_init.OcNIdleState = TIM_OCN_IDLE_STATE_RESET;

    TIM_InitOc1(TIM8, &compare_init);
    TIM_InitOc2(TIM8, &compare_init);
    TIM_InitOc3(TIM8, &compare_init);
    TIM_InitOc4(TIM8, &compare_init);

    /*
     * 打开CCR预装载后，UpPwm()写入的是影子值。四路新比较值
     * 在TIM8更新边界统一生效；同一个更新边界还会直接启动ADC3
     * 常规转换序列。
     */
    TIM_ConfigOc1Preload(TIM8, TIM_OC_PRE_LOAD_ENABLE);
    TIM_ConfigOc2Preload(TIM8, TIM_OC_PRE_LOAD_ENABLE);
    TIM_ConfigOc3Preload(TIM8, TIM_OC_PRE_LOAD_ENABLE);
    TIM_ConfigOc4Preload(TIM8, TIM_OC_PRE_LOAD_ENABLE);

    /* TIM8 keeps running for the 16 kHz algorithm and ADC trigger in mode 2. */
#if PWM_COE_TEST_MODE == 2U
    TIM_EnableCtrlPwmOutputs(TIM8, DISABLE);
#else
    /* 高级定时器TIM8必须打开主输出开关，CH1～CH4才能驱动到GPIO。 */
    TIM_EnableCtrlPwmOutputs(TIM8, ENABLE);
#endif
    /* 启动TIM8后，PWM输出和ADC常规序列周期触发同时开始运行。 */
    timer_pwm_adc_start();
}


/**
 * @brief 更新 PC6=A-、PC7=A+、PC8=B-、PC9=B+ 四路比较值。
 *
 * 参数直接写入TIM8比较寄存器，不换算占空比，也不限制输入范围。
 * 四路输出通过预装载寄存器在下一个完整PWM周期边界统一生效。
 * 本函数不会启动ADC；ADC只由TIM8更新TRGO周期触发。
 */
void RAMRUN UpPwm(u16 pwm1, u16 pwm2, u16 pwm3, u16 pwm4)
{
    /* Gate the only PWM write boundary, not just the position command. The
     * vendor algorithm may still execute ADC/current housekeeping disabled.
     * This is software gating, not STO; disabling removes holding torque. */
    if (EthercatMotorControl_PowerAllowed() == 0U) {
        pwm1 = 0U;
        pwm2 = 0U;
        pwm3 = 0U;
        pwm4 = 0U;
    }
    TIM_SetCmp1(TIM8, pwm1);
    TIM_SetCmp2(TIM8, pwm2);
    TIM_SetCmp3(TIM8, pwm3);
    TIM_SetCmp4(TIM8, pwm4);
}

void TIM8_UP_IRQHandler(void)
{
    if (TIM_GetIntStatus(TIM8, TIM_INT_UPDATE) != RESET) {
        uint32_t start_cycles = 0U;
        uint32_t stage_start_cycles = 0U;
        uint32_t stage_elapsed_cycles;

        if (gMotorCycleCounterAvailable != 0U) {
            start_cycles = DWT->CYCCNT;
        }

        TIM_ClrIntPendingBit(TIM8, TIM_INT_UPDATE);
        gMotorTim8IrqCount++;
        DriveSafety_MotorTick();
        EthercatMotorControl_BeginPhaseCycle(Iangle);
        ErrorHandle();
        DriveFault_Tick();
        DriveCurrent_BeginCycle();
        if (gMotorCycleCounterAvailable != 0U) {
            stage_start_cycles = DWT->CYCCNT;
            stage_elapsed_cycles = stage_start_cycles - start_cycles;
            if (stage_elapsed_cycles > gMotorPreMainMaxCycles)
                gMotorPreMainMaxCycles = stage_elapsed_cycles;
        }
        if (gEscPdiDiag.faultLatched != 0U) {
            DriveSafety_RequireRearm();
            /* Do not wait for foreground or a successful PDO to stop. */
            gEcatMotorEnabled = 0U;
            gEcatBaseRpmCommand = 0;
            EthercatMotorControl_EmergencyStop();
            UpPwm(0U, 0U, 0U, 0U);
        } else if (ErrNumFlag != 0U) {
            /* Enforce zero PWM on every latched-fault cycle, not only at
             * detection; retire the ramp for the CiA402 stop acknowledgement. */
            gEcatMotorEnabled = 0U;
            gEcatBaseRpmCommand = 0;
            EthercatMotorControl_EmergencyStop();
            UpPwm(0U, 0U, 0U, 0U);
        } else {
#if PWM_COE_TEST_MODE == 1U
            /* Keep TIM8/ADC running while preventing motor phase output. */
            UpPwm(0U, 0U, 0U, 0U);
#else
            /* Mode 2 still runs the full algorithm; hardware PWM outputs stay off. */
            if (!DriveBoot_RunCycle()) {
                if (gMotorCycleCounterAvailable != 0U)
                    stage_start_cycles = DWT->CYCCNT;
                MainFunt();
                if (gMotorCycleCounterAvailable != 0U) {
                    stage_elapsed_cycles = DWT->CYCCNT - stage_start_cycles;
                    if (stage_elapsed_cycles > gMotorMainFuntMaxCycles)
                        gMotorMainFuntMaxCycles = stage_elapsed_cycles;
                }
            }
#endif
        }
        if (gMotorCycleCounterAvailable != 0U)
            stage_start_cycles = DWT->CYCCNT;
        EthercatMotorControl_EndPhaseCycle(Iangle);
        if (gMotorCycleCounterAvailable != 0U) {
            stage_elapsed_cycles = DWT->CYCCNT - stage_start_cycles;
            if (stage_elapsed_cycles > gMotorFeedbackMaxCycles)
                gMotorFeedbackMaxCycles = stage_elapsed_cycles;
            stage_start_cycles = DWT->CYCCNT;
        }
        DriveFault_PhaseTick();
        if (gMotorCycleCounterAvailable != 0U) {
            stage_elapsed_cycles = DWT->CYCCNT - stage_start_cycles;
            if (stage_elapsed_cycles > gMotorFaultMaxCycles)
                gMotorFaultMaxCycles = stage_elapsed_cycles;
        }
        PwmFlag = 1U;

        if (gMotorCycleCounterAvailable != 0U) {
            uint32_t elapsed_cycles = DWT->CYCCNT - start_cycles;

            if (elapsed_cycles > gMotorIsrMaxCycles) {
                gMotorIsrMaxCycles = elapsed_cycles;
            }
            if (elapsed_cycles >= gMotorIsrPeriodCycles) {
                gMotorIsrOverrunCount++;
            }

            sMotorIsrWindowCycles += elapsed_cycles;
            sMotorIsrWindowSamples++;
            if (sMotorIsrWindowSamples >= 100U) {
                /* 100 samples: elapsed TIM8 occupancy at current priority. */
                gMotorIsrLoadPercent =
                    sMotorIsrWindowCycles / gMotorIsrPeriodCycles;
                sMotorIsrWindowCycles = 0U;
                sMotorIsrWindowSamples = 0U;
            }
        }
    }
}
