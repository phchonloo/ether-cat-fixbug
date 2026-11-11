/**
 * @file    flash.c
 * @brief   旧Flash兼容接口
 * @details 保留旧接口入口，并与独立参数保存模块保持现有职责边界。
 * @mcu     N32G45x
 * @tool    Keil MDK
 * @author  <待填>
 * @version <待填>
 * @date    2026-11-02
 * @note    现有空操作及选项字节保护行为保持原实现。
 */
#include "Flash.h"
/* Release 15 reserves 0x0803F000..0x0803FFFF for the transactional parameter
 * store. Unused legacy destructive APIs must never erase either active slot. */
void fmc_erase_pages(void)
{
}
void fmc_program(void)
{
}
void Flash_WriteInfo(u32 *data, u8 length)
{
    (void)data;
    (void)length;
}
void Flash_ReadInfo(u32 *data, u8 length)
{
    u32 i;
    if (!data)
        return;
    for (i = 0; i < length; ++i)
        data[i] = ((const u32 *)0x0803F800U)[i];
}
void Flash_Read_Protect(u8 yesno)
{
    (void)yesno; /* never change option bytes at runtime */
}
