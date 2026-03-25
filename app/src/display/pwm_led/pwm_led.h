/*
 * Copyright (C) 2022 Alibaba Group Holding Limited
 */

#ifndef _PWM_LED_H_
#define _PWM_LED_H_

#include <stdint.h>

typedef enum {
    LIGHT_SHOW_NET_UNAUTH = 0,      //
    LIGHT_SHOW_NET_AUTH,      
    LIGHT_SHOW_NET_CONNECTED,
    LIGHT_SHOW_NET_DISCONNECTED,
    LIGHT_SHOW_LISTENING_START,
    LIGHT_SHOW_LISTENING_ACTIVE,
    LIGHT_SHOW_LISTENING_STOP,
    LIGHT_SHOW_TALKING_ACTIVE,
    LIGHT_SHOW_TALKING_STOP,
    LIGHT_SHOW_RGB_TEST,
    LIGHT_SHOW_STATE_MAX
} light_show_state_types_t;

int led_pwm_rgb_init(void);
int led_pwm_rgb_start(void);
int led_pwm_rgb_config(uint32_t r_period, uint32_t g_period, uint32_t b_period, uint32_t r_pulse, uint32_t g_pulse, uint32_t b_pulse);
void led_pwm_rgb_stop(void);
void led_pwm_rgb_uninit(void);

int light_show_state_init(void);
int light_show_state_msg_send(light_show_state_types_t state_id, void *arg);

#endif
