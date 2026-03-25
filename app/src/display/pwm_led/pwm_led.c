/*
 * Copyright (C) 2022 Alibaba Group Holding Limited
 */
#include <aos/kernel.h>
#include <ulog/ulog.h>

#include <drv/pwm.h>

#include "pwm_led.h"
#include "pwm_led_shows.h"

#define TAG "PWMLED"

#define PWM_ID                    (0)
#define USE_PWM_CHANNEL_RED       (3)
#define USE_PWM_CHANNEL_GREEN     (0)
#define USE_PWM_CHANNEL_BLUE      (1)

#define PWM_LED_MESSAGE_NUM       (10)
#define PWM_LED_RGB_PERIOD        (255U)
#define PWM_LED_BRIGHTNESS_DIVISOR (2U)
#define PWM_LED_TASK_STACK_SIZE   (4096)

static csi_pwm_t g_pwm_r_handler;
static csi_pwm_t g_pwm_g_handler;
static csi_pwm_t g_pwm_b_handler;

static uint8_t s_q_buffer[sizeof(light_show_state_types_t) * PWM_LED_MESSAGE_NUM];
static aos_queue_t s_queue;

static int pwm_led_is_valid_effect(int effect_id)
{
    return (effect_id >= 0) && (effect_id < LIGHT_SHOW_STATE_MAX);
}

static const pwm_led_show_t *pwm_led_get_show(light_show_state_types_t effect_id)
{
    const pwm_led_show_t *show;

    if (!pwm_led_is_valid_effect((int)effect_id)) {
        LOGE(TAG, "invalid effect id:%d", effect_id);
        return NULL;
    }

    show = &g_pwm_led_shows[effect_id];
    if ((show->frames == NULL) || (show->frame_count == 0U)) {
        LOGE(TAG, "effect %d has no frames", effect_id);
        return NULL;
    }

    return show;
}

static uint32_t pwm_led_show_total_duration_ms(const pwm_led_show_t *show)
{
    uint16_t frame_index;
    uint32_t total_duration_ms = 0U;

    for (frame_index = 0U; frame_index < show->frame_count; ++frame_index) {
        total_duration_ms += show->frames[frame_index].duration_ms;
    }

    return total_duration_ms;
}

static uint8_t pwm_led_scale_channel(uint8_t channel)
{
    return (uint8_t)(channel / PWM_LED_BRIGHTNESS_DIVISOR);
}

static uint32_t pwm_led_channel_to_pulse_percent(uint8_t channel)
{
    return ((uint32_t)100U * (uint32_t)channel) / PWM_LED_RGB_PERIOD;
}

static int pwm_led_apply_frame(const pwm_led_frame_t *frame)
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    if (frame == NULL) {
        return -1;
    }

    red = pwm_led_scale_channel(frame->red);
    green = pwm_led_scale_channel(frame->green);
    blue = pwm_led_scale_channel(frame->blue);

    return led_pwm_rgb_config(PWM_LED_RGB_PERIOD, PWM_LED_RGB_PERIOD, PWM_LED_RGB_PERIOD,
                              pwm_led_channel_to_pulse_percent(red),
                              pwm_led_channel_to_pulse_percent(green),
                              pwm_led_channel_to_pulse_percent(blue));
}

static int pwm_led_validate_recv(size_t len, light_show_state_types_t effect_id)
{
    if (len != sizeof(effect_id)) {
        LOGE(TAG, "unexpected effect size:%u", (unsigned int)len);
        return -1;
    }

    if (!pwm_led_is_valid_effect((int)effect_id)) {
        LOGE(TAG, "discard invalid effect id:%d", effect_id);
        return -1;
    }

    return 0;
}

static int pwm_led_wait_for_next_effect(light_show_state_types_t *effect_id)
{
    int ret;
    size_t len;

    if (effect_id == NULL) {
        return -1;
    }

    while (1) {
        ret = aos_queue_recv(&s_queue, AOS_WAIT_FOREVER, effect_id, &len);
        if (ret != 0) {
            continue;
        }

        if (pwm_led_validate_recv(len, *effect_id) == 0) {
            return 0;
        }
    }
}

static int pwm_led_try_get_next_effect(light_show_state_types_t *effect_id)
{
    int ret;
    size_t len;

    if (effect_id == NULL) {
        return -1;
    }

    ret = aos_queue_recv(&s_queue, 0, effect_id, &len);
    if (ret != 0) {
        return 0;
    }

    if (pwm_led_validate_recv(len, *effect_id) != 0) {
        return -1;
    }

    return 1;
}

static int pwm_led_play_effect(light_show_state_types_t effect_id, light_show_state_types_t *next_effect_id)
{
    const pwm_led_show_t *show;
    uint16_t frame_index;
    int recv_status;
    int loop_enabled;

    show = pwm_led_get_show(effect_id);
    if (show == NULL) {
        return 0;
    }

    loop_enabled = (show->loop != 0U);
    if (loop_enabled && (pwm_led_show_total_duration_ms(show) == 0U)) {
        LOGW(TAG, "effect %d loop disabled because duration is zero", effect_id);
        loop_enabled = 0;
    }

    do {
        for (frame_index = 0U; frame_index < show->frame_count; ++frame_index) {
            if (pwm_led_apply_frame(&show->frames[frame_index]) != 0) {
                LOGE(TAG, "failed to apply frame %u for effect %d", frame_index, effect_id);
                return 0;
            }

            if (show->frames[frame_index].duration_ms > 0U) {
                aos_msleep(show->frames[frame_index].duration_ms);
            }

            recv_status = pwm_led_try_get_next_effect(next_effect_id);
            if (recv_status > 0) {
                return 1;
            }
        }
    } while (loop_enabled);

    return 0;
}

static void light_show_msg_task(void *arg)
{
    light_show_state_types_t effect_id;

    (void)arg;

    while (1) {
        if (pwm_led_wait_for_next_effect(&effect_id) != 0) {
            continue;
        }

        while (pwm_led_play_effect(effect_id, &effect_id) > 0) {
        }
    }
}

int led_pwm_rgb_init(void)
{
    int ret = 0;

    ret |= csi_pwm_init(&g_pwm_r_handler, PWM_ID);
    ret |= csi_pwm_init(&g_pwm_g_handler, PWM_ID);
    ret |= csi_pwm_init(&g_pwm_b_handler, PWM_ID);

    if (ret != 0) {
        LOGE(TAG, "PWM init failed");
        return -1;
    }

    return 0;
}

int led_pwm_rgb_start(void)
{
    int ret = 0;

    ret |= csi_pwm_out_start(&g_pwm_b_handler, USE_PWM_CHANNEL_BLUE);
    ret |= csi_pwm_out_start(&g_pwm_g_handler, USE_PWM_CHANNEL_GREEN);
    ret |= csi_pwm_out_start(&g_pwm_r_handler, USE_PWM_CHANNEL_RED);

    if (ret != 0) {
        LOGE(TAG, "PWM start failed");
        return -1;
    }

    return 0;
}

int led_pwm_rgb_config(uint32_t r_period, uint32_t g_period, uint32_t b_period, uint32_t r_pulse, uint32_t g_pulse, uint32_t b_pulse)
{
    int ret = 0;
    uint32_t r_pulse_us = r_period * r_pulse / 100U;
    uint32_t g_pulse_us = g_period * g_pulse / 100U;
    uint32_t b_pulse_us = b_period * b_pulse / 100U;

    ret |= csi_pwm_out_config(&g_pwm_r_handler, USE_PWM_CHANNEL_RED, r_period, r_pulse_us, 0);
    ret |= csi_pwm_out_config(&g_pwm_g_handler, USE_PWM_CHANNEL_GREEN, g_period, g_pulse_us, 0);
    ret |= csi_pwm_out_config(&g_pwm_b_handler, USE_PWM_CHANNEL_BLUE, b_period, b_pulse_us, 0);

    if (ret != 0) {
        LOGE(TAG, "PWM config failed");
        return -1;
    }

    return 0;
}

void led_pwm_rgb_stop(void)
{
    csi_pwm_out_stop(&g_pwm_r_handler, USE_PWM_CHANNEL_RED);
    csi_pwm_out_stop(&g_pwm_g_handler, USE_PWM_CHANNEL_GREEN);
    csi_pwm_out_stop(&g_pwm_b_handler, USE_PWM_CHANNEL_BLUE);
}

void led_pwm_rgb_uninit(void)
{
    csi_pwm_uninit(&g_pwm_r_handler);
    csi_pwm_uninit(&g_pwm_g_handler);
    csi_pwm_uninit(&g_pwm_b_handler);
}

int light_show_state_init(void)
{
    aos_task_t task_msg;
    int ret;

    ret = led_pwm_rgb_init();
    if (ret != 0) {
        LOGE(TAG, "init light show error");
        return -1;
    }

    ret = led_pwm_rgb_config(PWM_LED_RGB_PERIOD, PWM_LED_RGB_PERIOD, PWM_LED_RGB_PERIOD, 0U, 0U, 0U);
    if (ret != 0) {
        led_pwm_rgb_uninit();
        return -1;
    }

    ret = led_pwm_rgb_start();
    if (ret != 0) {
        led_pwm_rgb_uninit();
        return -1;
    }

    ret = aos_queue_new(&s_queue, s_q_buffer,
                        PWM_LED_MESSAGE_NUM * sizeof(light_show_state_types_t),
                        sizeof(light_show_state_types_t));
    if (ret != 0) {
        LOGE(TAG, "init light show queue error");
        led_pwm_rgb_stop();
        led_pwm_rgb_uninit();
        return -1;
    }

    ret = aos_task_new_ext(&task_msg, "light_show_msg_task", light_show_msg_task, NULL,
                           PWM_LED_TASK_STACK_SIZE, AOS_DEFAULT_APP_PRI);
    if (ret != 0) {
        LOGE(TAG, "create light show task failed");
        aos_queue_free(&s_queue);
        led_pwm_rgb_stop();
        led_pwm_rgb_uninit();
        return -1;
    }

    return 0;
}

int light_show_state_msg_send(light_show_state_types_t state_id, void *arg)
{
    int ret;

    (void)arg;

    if (!pwm_led_is_valid_effect((int)state_id)) {
        LOGE(TAG, "invalid effect id:%d", state_id);
        return -1;
    }

    ret = aos_queue_send(&s_queue, &state_id, sizeof(state_id));
    if (ret < 0) {
        LOGE(TAG, "queue send failed");
        return -1;
    }

    return 0;
}
