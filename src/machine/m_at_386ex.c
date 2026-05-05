/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Intel 386EX-based embedded machines.
 *
 *          The 386EX shares the 386SX instruction core but extends the
 *          external address bus to 26 bits (64 MB) and integrates a
 *          number of peripherals (DMA, PIC, timers, UARTs, watchdog,
 *          chip selects, SMM). 86Box currently models only the address
 *          bus extension (CPU_PKG_386EX); the integrated peripherals
 *          and SMM are not emulated.
 *
 * Authors: Nathan Lewis
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/flash.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/nvr.h>
#include <86box/port_92.h>
#include <86box/serial.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>

/*
 * TS-3100 board glue: jumper block + board ID register.
 *
 * Each "checked" config value represents a physically installed jumper. Real
 * hardware uses pull-ups with the jumper grounding the pin, so an installed
 * jumper reads back as 0 (active-low). Flip the sense in the read handlers
 * below if a future BIOS expects active-high.
 *
 *   JP1 - Manufacturing Mode        - 386EX P1.7  - I/O 0xF860 bit 7
 *   JP2 - Redirect Console to COM2  - 386EX P1.4  - I/O 0xF860 bit 4
 *   JP3 - Enable Flash Write        -             - I/O 0x0077 bit 1
 *   JP4 - Reduced Clock Rate        -             - I/O 0x0077 bit 2
 *   JP5 - User Jumper               - 386EX P3.2  - I/O 0xF870 bit 2
 *
 * Board ID:
 *   I/O 0x0074 returns 0x01 (TS-3100 identifier).
 */
#define TS3100_BOARD_ID 0x01
typedef struct ts3100_jumpers_t {
    uint8_t jp1;
    uint8_t jp2;
    uint8_t jp3;
    uint8_t jp4;
    uint8_t jp5;
} ts3100_jumpers_t;

static uint8_t
ts3100_jumpers_p1_read(UNUSED(uint16_t port), void *priv)
{
    const ts3100_jumpers_t *j = (ts3100_jumpers_t *) priv;
    uint8_t val = 0x90;

    if (j->jp1)
        val &= ~(1 << 7);
    if (j->jp2)
        val &= ~(1 << 4);

    return val;
}

static uint8_t
ts3100_jumpers_p3_read(UNUSED(uint16_t port), void *priv)
{
    const ts3100_jumpers_t *j = (ts3100_jumpers_t *) priv;
    uint8_t val = 0x04;

    if (j->jp5)
        val &= ~(1 << 2);

    return val;
}

static uint8_t
ts3100_jumpers_isa_read(UNUSED(uint16_t port), void *priv)
{
    const ts3100_jumpers_t *j = (ts3100_jumpers_t *) priv;
    uint8_t val = 0x00;

    if (j->jp3)
        val |= (1 << 1);
    if (j->jp4)
        val |= (1 << 2);

    return val;
}

static uint8_t
ts3100_board_id_read(UNUSED(uint16_t port), UNUSED(void *priv))
{
    return TS3100_BOARD_ID;
}

static void *
ts3100_jumpers_init(UNUSED(const device_t *info))
{
    ts3100_jumpers_t *j = (ts3100_jumpers_t *) calloc(1, sizeof(ts3100_jumpers_t));

    j->jp1 = device_get_config_int("jp1");
    j->jp2 = device_get_config_int("jp2");
    j->jp3 = device_get_config_int("jp3");
    j->jp4 = device_get_config_int("jp4");
    j->jp5 = device_get_config_int("jp5");

    io_sethandler(0x0074, 1,
                  ts3100_board_id_read, NULL, NULL,
                  NULL, NULL, NULL, NULL);
    io_sethandler(0x0077, 1,
                  ts3100_jumpers_isa_read, NULL, NULL,
                  NULL, NULL, NULL, j);
    io_sethandler(0xf860, 1,
                  ts3100_jumpers_p1_read, NULL, NULL,
                  NULL, NULL, NULL, j);
    io_sethandler(0xf870, 1,
                  ts3100_jumpers_p3_read, NULL, NULL,
                  NULL, NULL, NULL, j);

    return j;
}

static void
ts3100_jumpers_close(void *priv)
{
    free(priv);
}

static const device_config_t ts3100_jumpers_config[] = {
    // clang-format off
    {
        .name        = "jp1",
        .description = "JP1: Manufacturing Mode",
        .type        = CONFIG_BINARY,
        .default_int = 0
    },
    {
        .name        = "jp2",
        .description = "JP2: Redirect Console to COM2",
        .type        = CONFIG_BINARY,
        .default_int = 1
    },
    {
        .name        = "jp3",
        .description = "JP3: Enable Flash Write",
        .type        = CONFIG_BINARY,
        .default_int = 1
    },
    {
        .name        = "jp4",
        .description = "JP4: Reduced Clock Rate",
        .type        = CONFIG_BINARY,
        .default_int = 0
    },
    {
        .name        = "jp5",
        .description = "JP5: User Jumper",
        .type        = CONFIG_BINARY,
        .default_int = 0
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ts3100_jumpers_device = {
    .name          = "TS-3100 Jumper Block",
    .internal_name = "ts3100_jumpers",
    .flags         = 0,
    .local         = 0,
    .init          = ts3100_jumpers_init,
    .close         = ts3100_jumpers_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ts3100_jumpers_config
};

int
machine_at_ts3100_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ts3100/ts3100.bin",
                           0x00080000, 524288, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    void *kbc_priv = device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    /* The TS-3100 BIOS doesn't issue the AMI 0xCB command that normally
       sets FLAG_PS2 on the KBC, and it leaves command-byte bit 5 set,
       which in the AT KBC means "XT mode" - and XT mode bypasses the
       set-2->set-1 scancode translation regardless of KBC_FLAG_IS_TYPE2.
       Force PS/2 mode here so translation always runs. */
    kbc_at_set_ps2(kbc_priv, 1);

    device_add(&port_92_device);

    device_add_inst(&ns16450_device, 1);
    device_add_inst(&ns16450_device, 2);

    device_add(&ts3100_jumpers_device);

    device_add(&amd_flash_29f040a_device);

    return ret;
}
