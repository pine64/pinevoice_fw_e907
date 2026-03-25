/*
 * Copyright (C) 2019-2022 Alibaba Group Holding Limited
 */

#ifdef CONFIG_BOARD_BL606P_EVB
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <aos/cli.h>

#include <board.h>

#include "key_msg/app_key_msg.h"

static void cmd_board_ext_func(char *wbuf, int wbuf_len, int argc, char **argv)
{
    if (argc < 2) {
        return;
    }

    if (strcmp(argv[1], "init") == 0) {
        extern void board_pwm_init(void);
        extern void board_adc_init(void);
        board_pwm_init();
        board_adc_init();

#ifdef CONFIG_BOARD_BUTTON
        static int  g_board_button_inited = 0;
        if (g_board_button_inited == 0) {
            g_board_button_inited = 1;
            board_button_init(app_key_msg_send);
        }
#endif
        printf("Please check the LED and button pin jumper\r\n");
    }
}

void cli_reg_cmd_board_ext(void)
{
    static const struct cli_command cmd_info = { "board", "board extend command.", cmd_board_ext_func };

    aos_cli_register_command(&cmd_info);
}
#else
void cli_reg_cmd_board_ext(void)
{
    ;
}

#endif
