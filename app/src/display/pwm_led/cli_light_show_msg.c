/*
 * Copyright (C) 2022 Alibaba Group Holding Limited
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <aos/cli.h>
#include <board.h>

#include "pwm_led.h"

static const char *s_light_show_names[LIGHT_SHOW_STATE_MAX] = {
    [LIGHT_SHOW_NONE] = "LIGHT_SHOW_NONE",
    [LIGHT_SHOW_PROVISIONING] = "LIGHT_SHOW_PROVISIONING",
    [LIGHT_SHOW_ERROR] = "LIGHT_SHOW_ERROR",
    [LIGHT_SHOW_RGB_TEST] = "LIGHT_SHOW_RGB_TEST",
    [LIGHT_SHOW_STARTUP] = "LIGHT_SHOW_STARTUP",
    [LIGHT_SHOW_IDENTIFY] = "LIGHT_SHOW_IDENTIFY",
    [LIGHT_SHOW_WIFI_CONNECTING] = "LIGHT_SHOW_WIFI_CONNECTING",
    [LIGHT_SHOW_SAT_CONN_PENDING] = "LIGHT_SHOW_SAT_CONN_PENDING",
    [LIGHT_SHOW_READY] = "LIGHT_SHOW_READY",
    [LIGHT_SHOW_PROCESSING] = "LIGHT_SHOW_PROCESSING",
    [LIGHT_SHOW_ANSWER] = "LIGHT_SHOW_ANSWER",
    [LIGHT_SHOW_LISTENING] = "LIGHT_SHOW_LISTENING",
};

static const char *pwm_led_cli_show_name(light_show_state_types_t show_id)
{
    if ((show_id < 0) || (show_id >= LIGHT_SHOW_STATE_MAX) || (s_light_show_names[show_id] == NULL)) {
        return "UNKNOWN";
    }

    return s_light_show_names[show_id];
}

static void pwm_led_cli_print_show_list(void)
{
    int show_id;

    printf("available LED shows:\r\n");
    for (show_id = 0; show_id < LIGHT_SHOW_STATE_MAX; ++show_id) {
        printf("  %d -> %s\r\n", show_id, pwm_led_cli_show_name((light_show_state_types_t)show_id));
    }
}

static int pwm_led_cli_parse_show_id(const char *text, light_show_state_types_t *show_id)
{
    char *endptr;
    long value;

    if ((text == NULL) || (show_id == NULL)) {
        return -1;
    }

    value = strtol(text, &endptr, 10);
    if ((*text == '\0') || (endptr == NULL) || (*endptr != '\0')) {
        return -1;
    }

    if ((value < 0) || (value >= LIGHT_SHOW_STATE_MAX)) {
        return -1;
    }

    *show_id = (light_show_state_types_t)value;
    return 0;
}

static void cmd_rgb_msg_func(char *wbuf, int wbuf_len, int argc, char **argv)
{
    light_show_state_types_t show_id;
    light_show_msg_flags_t flags = LIGHT_SHOW_MSG_FLAG_NONE;

    (void)wbuf;
    (void)wbuf_len;

    if ((argc != 2) && (argc != 3)) {
        printf("usage: rgbmsg <show_id> [interrupt]\r\n");
        pwm_led_cli_print_show_list();
        return;
    }

    if (pwm_led_cli_parse_show_id(argv[1], &show_id) != 0) {
        printf("invalid show id: %s\r\n", argv[1]);
        pwm_led_cli_print_show_list();
        return;
    }

    if (show_id == LIGHT_SHOW_NONE) {
        printf("LIGHT_SHOW_NONE is reserved. Use rgbblack to queue black/off.\r\n");
        return;
    }

    if (argc == 3) {
        if ((strcmp(argv[2], "interrupt") == 0) || (strcmp(argv[2], "now") == 0)) {
            flags |= LIGHT_SHOW_MSG_FLAG_INTERRUPT;
        } else {
            printf("invalid rgbmsg option: %s\r\n", argv[2]);
            printf("usage: rgbmsg <show_id> [interrupt]\r\n");
            return;
        }
    }

    if (light_show_state_msg_send(show_id, LIGHT_SHOW_MSG_FLAGS(flags)) != 0) {
        printf("failed to queue transient show %d\r\n", show_id);
        return;
    }

    printf("%s transient show %d (%s)\r\n",
           (flags & LIGHT_SHOW_MSG_FLAG_INTERRUPT) != 0U ? "interrupting with" : "queued",
           show_id,
           pwm_led_cli_show_name(show_id));
}

static void cmd_rgb_state_func(char *wbuf, int wbuf_len, int argc, char **argv)
{
    light_show_state_types_t show_id;

    (void)wbuf;
    (void)wbuf_len;

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "get") == 0)) {
        show_id = light_show_state_get();
        printf("state show: %d (%s)\r\n", show_id, pwm_led_cli_show_name(show_id));
        return;
    }

    if (argc == 2 && strcmp(argv[1], "clear") == 0) {
        if (light_show_state_clear() != 0) {
            printf("failed to clear state show\r\n");
            return;
        }

        printf("state show cleared\r\n");
        return;
    }

    if (argc == 2) {
        if (pwm_led_cli_parse_show_id(argv[1], &show_id) != 0) {
            printf("invalid show id: %s\r\n", argv[1]);
            pwm_led_cli_print_show_list();
            return;
        }

        if (light_show_state_set(show_id) != 0) {
            printf("failed to set state show %d\r\n", show_id);
            return;
        }

        if (show_id == LIGHT_SHOW_NONE) {
            printf("state show cleared\r\n");
        } else {
            printf("state show set to %d (%s)\r\n", show_id, pwm_led_cli_show_name(show_id));
        }
        return;
    }

    printf("usage: rgbstate\r\n");
    printf("   or: rgbstate get\r\n");
    printf("   or: rgbstate <show_id>\r\n");
    printf("   or: rgbstate clear\r\n");
    pwm_led_cli_print_show_list();
}

static void cmd_rgb_black_func(char *wbuf, int wbuf_len, int argc, char **argv)
{
    (void)wbuf;
    (void)wbuf_len;
    (void)argv;

    if (argc != 1) {
        printf("usage: rgbblack\r\n");
        return;
    }

    if (light_show_rgb_clear() != 0) {
        printf("failed to queue RGB clear\r\n");
        return;
    }

    printf("queued RGB clear to black\r\n");
}

static void cmd_rgb_list_func(char *wbuf, int wbuf_len, int argc, char **argv)
{
    (void)wbuf;
    (void)wbuf_len;
    (void)argv;

    if (argc != 1) {
        printf("usage: rgblist\r\n");
        return;
    }

    pwm_led_cli_print_show_list();
}

void cli_reg_cmd_rgbmsg(void)
{
    static const struct cli_command cmd_info[] = {
        { "rgbmsg", "queue or interrupt with transient LED show: rgbmsg <show_id> [interrupt]", cmd_rgb_msg_func },
        { "rgbstate", "get/set/clear state LED show: rgbstate [get|<show_id>|clear]", cmd_rgb_state_func },
        { "rgbblack", "queue black/off LED command", cmd_rgb_black_func },
        { "rgblist", "list LED show ids", cmd_rgb_list_func }
    };

    aos_cli_register_commands(cmd_info, sizeof(cmd_info) / sizeof(cmd_info[0]));
}
