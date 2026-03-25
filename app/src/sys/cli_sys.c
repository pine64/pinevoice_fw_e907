/*
 * Copyright (C) 2019-2022 Alibaba Group Holding Limited
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include <aos/cli.h>
#include <aos/debug.h>

#include <drv/tick.h>
#include <ntp.h>
#include <ipc.h>
#include "app_sys.h"
#include <csi_core.h>

static void cmd_appsys_func(char *wbuf, int wbuf_len, int argc, char **argv)
{
    int item_count = argc;

    if (item_count == 2) {
        if (strcmp(argv[1], "us") == 0) {
            uint64_t us = csi_tick_get_us();
            printf("\ttick us: %llu\n", (long long unsigned)us);
        } else if (strcmp(argv[1], "time") == 0) {
            time_t t   = time(NULL);
            time_t lct = t + timezone * 3600;
            if (t >= 0) {
                printf("\tTZ(%02ld): %s %lld\n", timezone, asctime(localtime(&t)), (long long)lct);
                printf("\t   UTC: %s %lld\n", asctime(gmtime(&t)), (long long)t);
            }
        } else if (strcmp(argv[1], "assert") == 0) {
            printf("start aos_assert test\r\n");
            aos_assert(0);
        } else if (strcmp(argv[1], "assert2") == 0) {
            printf("start assert test\r\n");
            assert(0);
        } else if (strcmp(argv[1], "abort") == 0) {
            printf("start abort test\r\n");
            abort();
        } else {
            ;
        }
    } else if (item_count == 3) {
        if (strcmp(argv[1], "ntp") == 0) {
            ntp_sync_time(argv[2]);
        } else if (strcmp(argv[1], "crash") == 0) {
            int type = atoi(argv[2]);
            switch (type) {
                case 0: {
                    int *nullprt = NULL;
                    *nullprt     = 1;
                    break;
                }
                case 1: {
                    typedef void (*func_ptr_t)();
                    func_ptr_t f = (func_ptr_t)0x12345678;
                    f();
                    break;
                }
                case 2: {
                    int a = 100;
                    int b = 0;
                    int c = a / b;
                    printf("a/b=%d\r\n", c);
                    break;
                }
                default:;
            }
        } else if (strcmp(argv[1], "wdt") == 0) {
            int type = atoi(argv[2]);
            app_sys_except_init(type);
        }
    } else {
        ;
    }
}

static void cmd_cli_switch(char *wbuf, int wbuf_len, int argc, char **argv)
{
	extern int32_t g_cli_disable_read;
	g_cli_disable_read = !g_cli_disable_read;
}

static void cmd_cli_ipccount(char *wbuf, int wbuf_len, int argc, char **argv)
{
    extern void debug_printf_ipc_count(void);
    debug_printf_ipc_count();
}

void _crash_cmd(char *buf, int len, int argc, char **argv)
{
	//extern uint8_t __heap_start, __heap_end;

    if (2 != argc) {
        return;
    }
    if (strcmp(argv[1], "c906") == 0) {
        memset(0x54C00000, 0, 1024*10);
        csi_dcache_invalid_range(0x54C00000, 1024*10);
    } else if (strcmp(argv[1], "e907") == 0) {
    	void (*func)() = NULL;
    	func();
    	//memset(&__heap_start, 0x55, (uint32_t)&__heap_end - (uint32_t)&__heap_start);
    }
}

// extern void chatgpt_https_async_chatapi(void);
// extern void chatgpt_https_async_mainapi(void);

static void cmd_cli_mainapi(char *wbuf, int wbuf_len, int argc, char **argv)
{
    // chatgpt_https_async_mainapi();
}

static void cmd_cli_chatapi(char *wbuf, int wbuf_len, int argc, char **argv)
{
    // chatgpt_https_async_chatapi();
}

static const struct cli_command cmd_info[] = {
	{ "appsys", "app extend command.", cmd_appsys_func },
	{ "s", "Cli switch.", cmd_cli_switch },
    { "ipccount", "ipccount.", cmd_cli_ipccount },
	{ "crash", "crash.", _crash_cmd },
    { "mainapi", "https test.", cmd_cli_mainapi},
    { "chatapi", "https test.", cmd_cli_chatapi},
};

void cli_reg_cmd_appsys(void)
{
    aos_cli_register_commands(cmd_info, sizeof(cmd_info)/sizeof(struct cli_command));
}
