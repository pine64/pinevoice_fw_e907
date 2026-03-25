/*
 * Copyright (C) 2019-2022 Alibaba Group Holding Limited
 */

#include <stdbool.h>

#include <yoc/init.h>
#include <uservice/uservice.h>

#include <aos/yloop.h>
#include <aos/cli.h>
#include <devices/console_uart.h>
#include <drv/uart.h>
#include <drv/mbox.h>
#include <blyoc_mbox/blyoc_mbox.h>
#include <blyoc_flash.h>

#include <board.h>
#include <log_ipc.h>
#include "bl606p_gpio.h"
#include "bl606p_glb.h"
#include "key_msg/app_key_msg.h"

#ifdef AOS_COMP_DEBUG
#include <debug/dbg.h>
#endif

#define TAG "init"

#define CACHE_ALIGN_MASK    (__riscv_xlen - 1)
#define CACHE_ALIGN_UP(a)   (((a) + CACHE_ALIGN_MASK) & ~CACHE_ALIGN_MASK)
#define CACHE_ALIGN_DOWN(a) ((a) & ~CACHE_ALIGN_MASK)

static void yloop_thread(void *arg)
{
    aos_loop_run();
}

static void stduart_init(void)
{
    console_init(CONSOLE_UART_IDX, CONFIG_CLI_USART_BAUD, CONFIG_CONSOLE_UART_BUFSIZE);
}

#define ALG_CPUID 0
static void log_ipc_init(void)
{
    uint8_t cpu_id[] = { ALG_CPUID };

    ipc_log_ap_init(cpu_id, (int)sizeof(cpu_id));
}

int board_get_alg_cpuid()
{
    return ALG_CPUID;
}

void c906_uart_init(int id)
{
    static csi_uart_t uart_c906;
    csi_uart_init(&uart_c906, id);
}

#define C906_FW_BOOT_ADDR 0x54C00000
#define C906_RAM_ADDR_END 0x55000000

#if 1
static uint8_t g_c906_fw[] = {
#include "firmware/c906fdv_fw.h"
};
#else
#include "firmware/c906fdv_fw_mind.h"
#endif

void bootc906_start(void)
{
    extern int xz_decompress(uint8_t *cache, uint32_t cache_size,
        uint8_t *src, uint8_t *dest, uint32_t *p_dest_size);
    extern void boot_c906(int boot_addr);

    uint8_t *cache;
    uint32_t cache_size;
    uint8_t *src;
    uint8_t *dest;
    uint32_t dest_size = 0;

    GLB_Halt_CPU(GLB_CORE_ID_D0);

    cache       = (uint8_t *)C906_RAM_ADDR_END-0x20000;// last 500K
    cache_size  = 0x20000;
    src         = (uint8_t *)g_c906_fw;
    dest        = (uint8_t *)C906_FW_BOOT_ADDR;
    dest_size   = 0x100000;
    xz_decompress(cache, cache_size, src, dest, &dest_size);
    //printf("xxxxxxxx %d\r\n", dest_size);
    //memcpy((uint32_t *)C906_FW_BOOT_ADDR, g_c906_fw, sizeof(g_c906_fw));
    csi_dcache_clean_invalid_range((uint32_t *)CACHE_ALIGN_DOWN(C906_FW_BOOT_ADDR), dest_size);
    boot_c906(C906_FW_BOOT_ADDR);
}

static void c906_init(void)
{
    c906_uart_init(2);
    log_ipc_init();
    bootc906_start();
}

static void _mipc_process(void *arg, const void *data, uint32_t size, void *reslut, uint32_t reslut_size)
{

}

void board_yoc_init(void)
{
    multicore_server_init(_mipc_process, NULL);
    // multicore_heartbeat_init(multicore_cli_init());

    board_init();
    stduart_init();
    c906_init();

    /* 开发板PWM与JTAG复用，默认不复用PWM */
#ifndef CONFIG_BOARD_BL606P_EVB
    extern void board_pwm_init(void);
    extern void board_adc_init(void);
    board_pwm_init();
    board_adc_init();
#endif

    extern int hal_board_cfg(uint8_t board_code);
 
    bl_flash_init();
    hal_board_cfg(0);
    hal_boot2_init();

    aos_cli_init();
    event_service_init(NULL);
    light_show_state_init();
    board_audio_init();

#if 0
    extern void av_set_ao_channel_num(int num);
    av_set_ao_channel_num(1);
#endif

    ulog_init();
    aos_set_log_level(AOS_LL_DEBUG);

#ifdef AOS_COMP_DEBUG
    aos_debug_init();
#endif

    player_init();
    
    /* start yloop, wifi driver deps */
    aos_loop_init();
    static aos_task_t task_handle;
    aos_task_new_ext(&task_handle, "yloop", yloop_thread, NULL, 6 * 1024, AOS_DEFAULT_APP_PRI);

#ifdef CONFIG_BOARD_BUTTON
#ifndef CONFIG_BOARD_BL606P_EVB /*开发板默认不启动按键*/
    board_button_init(app_key_msg_send);
#endif
#endif

extern void cli_reg_cmd_board_ext(void);
    cli_reg_cmd_board_ext();

extern void cli_reg_cmd_mfg(void);
extern void cli_reg_cmd_isp(void);
extern void cli_reg_cmd_codec(void);
extern void cli_reg_cmd_peripherals(void);
    cli_reg_cmd_mfg();
    cli_reg_cmd_isp();
	cli_reg_cmd_codec();
    cli_reg_cmd_peripherals();
}
