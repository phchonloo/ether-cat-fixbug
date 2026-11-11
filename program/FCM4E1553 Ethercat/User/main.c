/**
 * @file    main.c
 * @brief   应用启动与主循环
 * @details 按现有顺序初始化外设及驱动，并轮询EtherCAT、通信和保护服务。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    PWM启动与看门狗使能时序保持原实现。
 */
#include "mcuinit.h"
#include "EthercatMotorControl.h"
#include "SciInit.h"
#include "DriveTelemetry.h"
#include "DriveCurrent.h"
#include "DriveFault.h"
#include "DriveBoot.h"
#include "DriveSafety.h"
#include "DriveParameters.h"
static void eeprom_self_test_report(void);
static void motor_control_irq_priority_config(void);
static void application_init_error_loop(void);

int main(void)
{
    uint8_t hw_init_result;
    uint16_t cia402_result;
    uint16_t mapping_result;

    systick_config();
    rcu_config();
    systick_config();

    hw_init_result = HW_Init();

    GpioIni();
    com_gpio_init();
    com_usart_init();

    eeprom_init();
    eeprom_self_test_report();
    eeprom_console_init();
    modbus_init(MODBUS_DEFAULT_SLAVE_ADDR,
        MODBUS_DEFAULT_BAUDRATE);

    DriveTelemetry_Init();
    dma_config();
    adc_config();

    /* Initialise all motor data before TIM8 can enter MainFunt(). */
    DataIni();
    shuzu_initial();
    IniSaveFunc();

    if (hw_init_result != 0U) {
        application_init_error_loop();
    }

    (void)MainInit();

    cia402_result = CiA402_Init();
    if (cia402_result != ALSTATUSCODE_NOERROR) {
        application_init_error_loop();
    }

    EthercatMotorControl_Init();
    DriveCurrent_Init();
    DriveFault_Init();
    DriveBoot_Init();
    DriveSafety_Init();
    DriveParameters_Init();
    mapping_result = APPL_GenerateMapping(&nPdInputSize,
        &nPdOutputSize);
    if (mapping_result != ALSTATUSCODE_NOERROR) {
        application_init_error_loop();
    }

    /* Start the motor loop only after protocol and motor state are safe. */
    pwm_config();
    UpPwm(0U, 0U, 0U, 0U);
    motor_control_irq_priority_config();

    DriveSafety_StartWatchdog();
    bRunApplication = TRUE;
    while (bRunApplication == TRUE) {
        MainLoop();
        DriveTelemetry_Update();
        DriveFault_Update();
        EthercatMotorControl_Update();
        modbus_process();
        eeprom_console_process();
        DriveParameters_Update();
        DriveSafety_Update();
    }

    EthercatMotorControl_Update();
    UpPwm(0U, 0U, 0U, 0U);
    CiA402_DeallocateAxis();
    HW_Release();

    return 0;
}

void rcu_config(void)
{
    SystemCoreClockUpdate();
}

void led_spark(void)
{
}

static void motor_control_irq_priority_config(void)
{
    NVIC_InitType nvic_init;

    /* Match timer.c: the motor update preempts ESC IRQ and SYNC0. */
    nvic_init.NVIC_IRQChannel = TIM8_UP_IRQn;
    nvic_init.NVIC_IRQChannelPreemptionPriority = 0U;
    nvic_init.NVIC_IRQChannelSubPriority = 0U;
    nvic_init.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init);
}

static void application_init_error_loop(void)
{
    UpPwm(0U, 0U, 0U, 0U);
    for (;;) {
        MainLoop();
        modbus_process();
        eeprom_console_process();
    }
}

static void eeprom_self_test_report(void)
{
    eeprom_status_t status;
    uint8_t status_code;
    uint8_t probe[8];
    static const uint8_t start_text[] =
        "[24C02] READ PROBE START\r\n";
    static const uint8_t pass_text[] =
        "[24C02] READ PROBE PASS\r\n";
    static const uint8_t fail_text[] =
        "[24C02] READ PROBE FAIL, STATUS=";
    static const uint8_t line_end[] = "\r\n";

    (void)uart_write(start_text,
        (uint16_t)(sizeof(start_text) - 1U));

    /* A power interruption during the old write/restore self-test could
     * leave its 0xF8 page altered. Startup must never write EEPROM. */
    status = eeprom_is_ready();
    if (status == BSP_24C02_OK)
        status = eeprom_read(0xF8U, probe, sizeof(probe));
    if (status == BSP_24C02_OK) {
        (void)uart_write(pass_text,
            (uint16_t)(sizeof(pass_text) - 1U));
        return;
    }

    status_code = (uint8_t)('0' + (uint8_t)status);
    (void)uart_write(fail_text,
        (uint16_t)(sizeof(fail_text) - 1U));
    (void)uart_write(&status_code, 1U);
    (void)uart_write(line_end,
        (uint16_t)(sizeof(line_end) - 1U));
}
