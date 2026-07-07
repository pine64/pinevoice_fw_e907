#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <aos/kernel.h>
#include <bl_efuse.h>
#include "usbd_core.h"
#include "usbd_cdc_acm.h"

#include "../../../../components/cli/src/cli_console.h"

#define USB_CONSOLE_CDC_IN_EP   0x81
#define USB_CONSOLE_CDC_OUT_EP  0x02
#define USB_CONSOLE_CDC_INT_EP  0x83

#define USB_CONSOLE_VID           0xFFFF
#define USB_CONSOLE_PID           0xFFFF
#define USB_CONSOLE_MAX_POWER     100
#define USB_CONSOLE_LANGID_STRING 1033

#ifdef CONFIG_USB_HS
#define USB_CONSOLE_CDC_MAX_MPS 512
#else
#define USB_CONSOLE_CDC_MAX_MPS 64
#endif

#define USB_CONSOLE_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)
#define USB_CONSOLE_RX_XFER_SIZE 2048
#define USB_CONSOLE_RX_BUFFER_SIZE 1024
#define USB_CONSOLE_TX_BUFFER_SIZE 512
#define USB_CONSOLE_READ_WAIT_MS 20
#define USB_CONSOLE_TX_TIMEOUT_MS 100
#define USB_CONSOLE_TX_LOCK_TIMEOUT_MS 20

#ifndef USB_CONSOLE_REQUIRE_RTS
#define USB_CONSOLE_REQUIRE_RTS 1
#endif

typedef struct {
    volatile uint32_t write_idx;
    volatile uint32_t read_idx;
    uint8_t data[USB_CONSOLE_RX_BUFFER_SIZE];
} usb_console_rx_ring_t;

static const uint8_t s_device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01,
                               USB_CONSOLE_VID, USB_CONSOLE_PID, 0x0100, 0x01)
};

static const uint8_t s_config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONSOLE_CONFIG_SIZE, 0x02, 0x01,
                               USB_CONFIG_BUS_POWERED, USB_CONSOLE_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, USB_CONSOLE_CDC_INT_EP,
                            USB_CONSOLE_CDC_OUT_EP, USB_CONSOLE_CDC_IN_EP,
                            USB_CONSOLE_CDC_MAX_MPS, 0x02)
};

static const uint8_t s_device_quality_descriptor[] = {
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00,
    0x02,
    0x00,
    0x00,
    0x00,
    0x40,
    0x00,
    0x00,
};

static char s_serial_num[(6 * 2) + 1] = "000000000000";

static const char *s_string_descriptors[] = {
    (const char[]){ 0x09, 0x04 },
    "PINE64",
    "PineVoice Console",
    s_serial_num,
};

static usb_console_rx_ring_t s_rx;
static aos_sem_t s_rx_sem;
static aos_sem_t s_tx_lock;
static aos_sem_t s_tx_done_sem;

static volatile bool s_started;
static volatile bool s_usb_registered;
static volatile bool s_usb_initialized;
static volatile bool s_sem_ready;
static volatile bool s_configured;
static volatile bool s_dtr;
static volatile bool s_rts;
static volatile bool s_write_failed;
static volatile bool s_tx_busy;
static volatile bool s_tx_zlp_pending;
static volatile uint32_t s_rx_reset_seq;
static uint8_t s_busid;
static uint8_t s_tx_ep = USB_CONSOLE_CDC_IN_EP;

static struct cdc_line_coding s_line_coding = {
    .dwDTERate = 2000000,
    .bCharFormat = 0,
    .bParityType = 0,
    .bDataBits = 8,
};

static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t s_rx_xfer_buffer[USB_CONSOLE_RX_XFER_SIZE];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t s_tx_buffer[USB_CONSOLE_TX_BUFFER_SIZE];

static struct usbd_interface s_intf0;
static struct usbd_interface s_intf1;

static bool usb_console_line_open(void)
{
    if (!s_configured || !s_dtr || s_write_failed) {
        return false;
    }

#if USB_CONSOLE_REQUIRE_RTS
    return s_rts;
#else
    return true;
#endif
}

int usb_console_is_open(void)
{
    return usb_console_line_open() ? 1 : 0;
}

/*
 * SPSC RX ring: USB OUT callback advances write_idx, CLI task advances read_idx.
 * Clear intentionally scratches pending input by advancing read_idx only.
 */
static void usb_console_rx_clear(void)
{
    s_rx.read_idx = s_rx.write_idx;
    s_rx_reset_seq++;
}

static int usb_console_rx_get(uint8_t *ch)
{
    uint32_t reset_seq = s_rx_reset_seq;
    uint32_t read_idx;

    read_idx = s_rx.read_idx;

    if (read_idx == s_rx.write_idx) {
        return 0;
    }

    *ch = s_rx.data[read_idx];
    read_idx++;
    if (read_idx >= USB_CONSOLE_RX_BUFFER_SIZE) {
        read_idx = 0;
    }
    s_rx.read_idx = read_idx;

    if (reset_seq != s_rx_reset_seq) {
        s_rx.read_idx = s_rx.write_idx;
        return 0;
    }

    return 1;
}

static void usb_console_rx_put(const uint8_t *data, uint32_t len)
{
    bool wrote = false;

    if (!usb_console_line_open() || data == NULL) {
        return;
    }

    for (uint32_t i = 0; i < len; i++) {
        uint32_t next;

        next = s_rx.write_idx + 1;
        if (next >= USB_CONSOLE_RX_BUFFER_SIZE) {
            next = 0;
        }

        if (next == s_rx.read_idx) {
            break;
        }

        s_rx.data[s_rx.write_idx] = data[i];
        s_rx.write_idx = next;
        wrote = true;
    }

    if (wrote && s_sem_ready) {
        aos_sem_signal(&s_rx_sem);
    }
}

static void usb_console_mark_closed(void)
{
    s_configured = false;
    s_dtr = false;
    s_rts = false;
    s_write_failed = false;
    s_tx_busy = false;
    s_tx_zlp_pending = false;
    usb_console_rx_clear();

    if (s_sem_ready) {
        aos_sem_signal(&s_rx_sem);
        aos_sem_signal(&s_tx_done_sem);
    }
}

static void usb_console_mark_write_failed(void)
{
    s_write_failed = true;
    s_tx_busy = false;
    s_tx_zlp_pending = false;

    if (s_sem_ready) {
        aos_sem_signal(&s_tx_done_sem);
    }
}

static void usb_console_set_line_state(uint8_t busid, bool dtr, bool rts)
{
    s_busid = busid;
    s_dtr = dtr;
    s_rts = rts;
    s_write_failed = false;

    if (!usb_console_line_open()) {
        usb_console_rx_clear();
    }
}

static void usb_console_event(uint8_t busid, uint8_t event)
{
    s_busid = busid;

    switch (event) {
        case USBD_EVENT_CONFIGURED:
            s_configured = true;
            s_write_failed = false;
            usb_console_rx_clear();
            usbd_ep_start_read(busid, USB_CONSOLE_CDC_OUT_EP,
                               s_rx_xfer_buffer, sizeof(s_rx_xfer_buffer));
            break;

        case USBD_EVENT_RESET:
        case USBD_EVENT_DISCONNECTED:
        case USBD_EVENT_SUSPEND:
        case USBD_EVENT_DEINIT:
            usb_console_mark_closed();
            break;

        default:
            break;
    }
}

static void usb_console_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)ep;

    usb_console_rx_put(s_rx_xfer_buffer, nbytes);
    usbd_ep_start_read(busid, USB_CONSOLE_CDC_OUT_EP,
                       s_rx_xfer_buffer, sizeof(s_rx_xfer_buffer));
}

static void usb_console_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    int ret;

    if (!s_tx_busy || ep != s_tx_ep) {
        return;
    }

    if (s_tx_zlp_pending) {
        s_tx_zlp_pending = false;
        s_tx_busy = false;
        if (s_sem_ready) {
            aos_sem_signal(&s_tx_done_sem);
        }
        return;
    }

    if (nbytes != 0) {
        uint16_t mps = usbd_get_ep_mps(busid, ep);
        if (mps != 0 && (nbytes % mps) == 0) {
            s_tx_zlp_pending = true;
            ret = usbd_ep_start_write(busid, ep, NULL, 0);
            if (ret == 0) {
                return;
            }
            usb_console_mark_write_failed();
            return;
        }
    }

    s_tx_busy = false;
    if (s_sem_ready) {
        aos_sem_signal(&s_tx_done_sem);
    }
}

static struct usbd_endpoint s_cdc_out_ep = {
    .ep_addr = USB_CONSOLE_CDC_OUT_EP,
    .ep_cb = usb_console_bulk_out
};

static struct usbd_endpoint s_cdc_in_ep = {
    .ep_addr = USB_CONSOLE_CDC_IN_EP,
    .ep_cb = usb_console_bulk_in
};

static const uint8_t *usb_console_device_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return s_device_descriptor;
}

static const uint8_t *usb_console_config_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return s_config_descriptor;
}

static const uint8_t *usb_console_device_quality_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return s_device_quality_descriptor;
}

static const char *usb_console_string_descriptor_callback(uint8_t speed, uint8_t index)
{
    (void)speed;

    if (index >= (sizeof(s_string_descriptors) / sizeof(s_string_descriptors[0]))) {
        return NULL;
    }

    return s_string_descriptors[index];
}

static const struct usb_descriptor s_cdc_descriptor = {
    .device_descriptor_callback = usb_console_device_descriptor_callback,
    .config_descriptor_callback = usb_console_config_descriptor_callback,
    .device_quality_descriptor_callback = usb_console_device_quality_descriptor_callback,
    .string_descriptor_callback = usb_console_string_descriptor_callback
};

static int usb_console_cdc_request_handler(uint8_t busid,
                                           struct usb_setup_packet *setup,
                                           uint8_t **data,
                                           uint32_t *len)
{
    switch (setup->bRequest) {
        case CDC_REQUEST_SET_LINE_CODING:
            if (data != NULL && *data != NULL && setup->wLength >= sizeof(s_line_coding)) {
                memcpy(&s_line_coding, *data, sizeof(s_line_coding));
            }
            break;

        case CDC_REQUEST_GET_LINE_CODING:
            if (data == NULL || *data == NULL || len == NULL) {
                return -1;
            }
            memcpy(*data, &s_line_coding, sizeof(s_line_coding));
            *len = sizeof(s_line_coding);
            break;

        case CDC_REQUEST_SET_CONTROL_LINE_STATE:
            usb_console_set_line_state(busid,
                                       (setup->wValue & 0x0001) != 0,
                                       (setup->wValue & 0x0002) != 0);
            break;

        case CDC_REQUEST_SEND_BREAK:
            break;

        default:
            return -1;
    }

    return 0;
}

static void usb_console_sem_drain(aos_sem_t *sem)
{
    while (aos_sem_wait(sem, AOS_NO_WAIT) == 0) {
    }
}

static int usb_console_wait_tx_done(void)
{
    if (aos_sem_wait(&s_tx_done_sem, USB_CONSOLE_TX_TIMEOUT_MS) != 0) {
        usb_console_mark_write_failed();
        return -1;
    }

    return usb_console_line_open() ? 0 : -1;
}

static int usb_console_wait_tx_idle(void)
{
    while (s_tx_busy && usb_console_line_open()) {
        if (usb_console_wait_tx_done() != 0) {
            return -1;
        }
    }

    return usb_console_line_open() ? 0 : -1;
}

static int usb_console_start_tx_locked(const uint8_t *src, size_t len)
{
    size_t chunk = len;
    int ret;

    if (src == NULL || len == 0 || !usb_console_line_open()) {
        return 0;
    }

    if (chunk > sizeof(s_tx_buffer)) {
        chunk = sizeof(s_tx_buffer);
    }

    memcpy(s_tx_buffer, src, chunk);
    usb_console_sem_drain(&s_tx_done_sem);

    s_tx_ep = USB_CONSOLE_CDC_IN_EP;
    s_tx_busy = true;
    s_tx_zlp_pending = false;

    ret = usbd_ep_start_write(s_busid, s_tx_ep, s_tx_buffer, (uint32_t)chunk);
    if (ret != 0) {
        usb_console_mark_write_failed();
        return -1;
    }

    return (int)chunk;
}

int usb_console_raw_write(const void *buf, size_t len)
{
    const uint8_t *src = (const uint8_t *)buf;

    if (buf == NULL || len == 0) {
        return 0;
    }

    if (!s_sem_ready || !usb_console_line_open()) {
        return (int)len;
    }

    if (aos_irq_context()) {
        return (int)len;
    }

    if (aos_sem_wait(&s_tx_lock, AOS_NO_WAIT) != 0) {
        return (int)len;
    }

    if (!s_tx_busy && usb_console_line_open()) {
        (void)usb_console_start_tx_locked(src, len);
    }

    aos_sem_signal(&s_tx_lock);

    return (int)len;
}

static int usb_console_write_blocking(const void *buf, size_t len)
{
    const uint8_t *src = (const uint8_t *)buf;
    size_t offset = 0;

    if (buf == NULL || len == 0) {
        return 0;
    }

    if (!s_sem_ready || !usb_console_line_open()) {
        return (int)len;
    }

    if (aos_irq_context()) {
        return (int)len;
    }

    if (aos_sem_wait(&s_tx_lock, USB_CONSOLE_TX_LOCK_TIMEOUT_MS) != 0) {
        return (int)len;
    }

    while (offset < len && usb_console_line_open()) {
        int started;

        if (usb_console_wait_tx_idle() != 0) {
            break;
        }

        started = usb_console_start_tx_locked(src + offset, len - offset);
        if (started <= 0) {
            break;
        }

        if (usb_console_wait_tx_done() != 0) {
            break;
        }

        offset += (size_t)started;
    }

    aos_sem_signal(&s_tx_lock);

    return (int)len;
}

int usb_console_raw_putc(int ch)
{
    uint8_t c = (uint8_t)ch;
    return usb_console_raw_write(&c, 1) == 1 ? 0 : -1;
}

static int usb_console_write_cb(const void *buf, size_t len, void *private_data)
{
    (void)private_data;

    return usb_console_write_blocking(buf, len);
}

static int usb_console_read_cb(void *buf, size_t len, void *private_data)
{
    uint8_t ch;

    (void)private_data;

    if (buf == NULL || len == 0) {
        return 0;
    }

    if (!usb_console_line_open()) {
        aos_msleep(USB_CONSOLE_READ_WAIT_MS);
        return 0;
    }

    if (usb_console_rx_get(&ch) == 0) {
        aos_sem_wait(&s_rx_sem, USB_CONSOLE_READ_WAIT_MS);
        if (usb_console_rx_get(&ch) == 0) {
            return 0;
        }
    }

    ((uint8_t *)buf)[0] = ch;
    return 1;
}

static int usb_console_init_cb(void *private_data)
{
    (void)private_data;

    if (!s_sem_ready) {
        if (aos_sem_new(&s_rx_sem, 0) != 0) {
            return -1;
        }
        if (aos_sem_new(&s_tx_lock, 1) != 0) {
            aos_sem_free(&s_rx_sem);
            return -1;
        }
        if (aos_sem_new(&s_tx_done_sem, 0) != 0) {
            aos_sem_free(&s_tx_lock);
            aos_sem_free(&s_rx_sem);
            return -1;
        }
        s_sem_ready = true;
    }

    usb_console_rx_clear();
    return 0;
}

static int usb_console_deinit_cb(void *private_data)
{
    (void)private_data;

    usb_console_mark_closed();
    return 0;
}

static device_console s_usb_device_console = {
    .name = "usb-cdc-console",
    .fd = -1,
    .write = usb_console_write_cb,
    .read = usb_console_read_cb,
    .init = usb_console_init_cb,
    .deinit = usb_console_deinit_cb
};

cli_console cli_usb_console = {
    .i_list = {0},
    .name = "cli-usb",
    .dev_console = &s_usb_device_console,
    .init_flag = 0,
    .exit_flag = 0,
    .alive = 1,
    .private_data = NULL,
    .cli_tag = {0},
    .cli_tag_len = 0,
    .task_list = {0},
    .finsh_callback = NULL,
    .start_callback = NULL,
};

int usb_console_start(void)
{
    int ret;

    if (s_started) {
        return 0;
    }

    ret = cli_console_task_create(&cli_usb_console, CLI_CONFIG_STACK_SIZE, CLI_TASK_PRIORITY);
    if (ret == 0) {
        s_started = true;
    }

    return ret;
}

int usb_console_cdc_acm_init(uint8_t busid, uintptr_t reg_base)
{
    struct usbd_interface *intf;
    uint8_t mac[6];
    int ret;

    if (aos_irq_context()) {
        return -1;
    }

    if (s_usb_initialized) {
        return 0;
    }

    ret = usb_console_start();
    if (ret != 0) {
        return ret;
    }

    if (bl_efuse_read_mac_smart(1, mac, 0) != 0) {
        memset(mac, 0, sizeof(mac));
    }
    snprintf(s_serial_num, sizeof(s_serial_num), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (!s_usb_registered) {
        usbd_desc_register(busid, &s_cdc_descriptor);

        intf = usbd_cdc_acm_init_intf(busid, &s_intf0);
        intf->class_interface_handler = usb_console_cdc_request_handler;
        usbd_add_interface(busid, intf);

        intf = usbd_cdc_acm_init_intf(busid, &s_intf1);
        intf->class_interface_handler = usb_console_cdc_request_handler;
        usbd_add_interface(busid, intf);

        usbd_add_endpoint(busid, &s_cdc_out_ep);
        usbd_add_endpoint(busid, &s_cdc_in_ep);
        s_usb_registered = true;
    }

    ret = usbd_initialize(busid, reg_base, usb_console_event);
    if (ret == 0) {
        s_usb_initialized = true;
    }

    return ret;
}
