/*
 * Copyright (C) 2022 Alibaba Group Holding Limited
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <aos/cli.h>
#include <board.h>

#include "pwm_led.h"

/*************************************************
 * 通过命令行模拟按键，测试消息
 *************************************************/
static void cmd_rgb_msg_func(char *wbuf, int wbuf_len, int argc, char **argv)
{
    if (argc == 2) {
        int keymsg_id = atoi(argv[1]);
        light_show_state_msg_send(keymsg_id, NULL);
    } else {
        printf("usage:keymsg msgid[0~%d]\r\n", 7);
    }
}

void cli_reg_cmd_rgbmsg(void)
{
    static const struct cli_command cmd_info = { "rgbmsg", "led egb msg test", cmd_rgb_msg_func };

    aos_cli_register_command(&cmd_info);
}
