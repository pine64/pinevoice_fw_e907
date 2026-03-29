#include "usbd_core.h"
#include "usbd_cdc.h"
#include <ulog/ulog.h>
#include <bl_efuse.h>

/*!< endpoint address */
#define CDC_IN_EP  0x81
#define CDC_OUT_EP 0x01
#define CDC_INT_EP 0x82

#define USBD_VID           0xFFFF
#define USBD_PID           0xFFFF
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

/*!< config descriptor size */
#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)

/*!< global descriptor */
static uint8_t cdc_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x02, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01),
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, 0x02),
    ///////////////////////////////////////
    /// string0 descriptor
    ///////////////////////////////////////
    USB_LANGID_INIT(USBD_LANGID_STRING),
    ///////////////////////////////////////
    /// string1 descriptor
    ///////////////////////////////////////
    0x12,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    'B', 0x00,                  /* wcChar0 */
    'o', 0x00,                  /* wcChar1 */
    'u', 0x00,                  /* wcChar2 */
    'f', 0x00,                  /* wcChar3 */
    'f', 0x00,                  /* wcChar4 */
    'a', 0x00,                  /* wcChar5 */
    'l', 0x00,                  /* wcChar6 */
    'o', 0x00,                  /* wcChar7 */
    ///////////////////////////////////////
    /// string2 descriptor
    ///////////////////////////////////////
    0x1E,                       /* bLength 36 */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    'P', 0x00,                  /* wcChar0 */
    'i', 0x00,                  /* wcChar1 */
    'n', 0x00,                  /* wcChar2 */
    'e', 0x00,                  /* wcChar3 */
    'V', 0x00,                  /* wcChar4 */
    'o', 0x00,                  /* wcChar5 */
    'i', 0x00,                  /* wcChar6 */
    'c', 0x00,                  /* wcChar7 */
    'e', 0x00,                  /* wcChar8 */
    ' ', 0x00,                  /* wcChar13 */
    'D', 0x00,                  /* wcChar14 */
    'E', 0x00,                  /* wcChar15 */
    'M', 0x00,                  /* wcChar16 */
    'O', 0x00,                  /* wcChar17 */
    ///////////////////////////////////////
    /// string3 descriptor
    ///////////////////////////////////////
    0x1A,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    '2', 0x00,                  /* wcChar0 */
    '0', 0x00,                  /* wcChar1 */
    '2', 0x00,                  /* wcChar2 */
    '1', 0x00,                  /* wcChar3 */
    '0', 0x00,                  /* wcChar4 */
    '3', 0x00,                  /* wcChar5 */
    '1', 0x00,                  /* wcChar6 */
    '0', 0x00,                  /* wcChar7 */
    '0', 0x00,                  /* wcChar8 */
    '0', 0x00,                  /* wcChar9 */
    '0', 0x00,                  /* wcChar10 */
    '1', 0x00,                  /* wcChar11 */
#ifdef CONFIG_USB_HS
    ///////////////////////////////////////
    /// device qualifier descriptor
    ///////////////////////////////////////
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00,
    0x02,
    0x02,
    0x02,
    0x01,
    0x40,
    0x01,
    0x00,
#endif
    0x00
};
/*!< class */
usbd_class_t cdc_class;
/*!< interface one */
usbd_interface_t cdc_cmd_intf;
/*!< interface two */
usbd_interface_t cdc_data_intf;

/* function ------------------------------------------------------------------*/
void usbd_cdc_acm_out(uint8_t ep)
{
    uint8_t data[64];
    uint32_t read_byte;
    
    usbd_ep_read(ep, data, 64, &read_byte, 0);
    printf("read len:%d\r\n", read_byte);
    usbd_ep_read(ep, NULL, 0, NULL, 0);
}

void usbd_cdc_acm_in(uint8_t ep)
{
    printf("in\r\n");
}

/*!< endpoint call back */
usbd_endpoint_t cdc_out_ep = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = usbd_cdc_acm_out
};

usbd_endpoint_t cdc_in_ep = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = usbd_cdc_acm_in
};

/* function ------------------------------------------------------------------*/
void cdc_init(void)
{
    uint8_t* desc_p = cdc_descriptor;
    for (int i = 0; i < 20; i++) {
        uint8_t len = desc_p[0];
        uint8_t type = desc_p[1];
        if (len == 0) break;
        if (type == USB_DESCRIPTOR_TYPE_STRING && len == 0x1A) {
            LOGI("USB", "Serial number found!");
            uint8_t mac[6];
            char s_service_instance[(6 * 2) + 1];
            bl_efuse_read_mac_smart(1, mac, 0);
            snprintf(s_service_instance, sizeof(s_service_instance), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            for (int j = 0; j < 12; j++) {
                desc_p[2 + (j * 2)] = s_service_instance[j];
            }

            break;
        }
        desc_p = &desc_p[len];
    }

    usbd_desc_register(cdc_descriptor);
    /*!< add interface */
    usbd_cdc_add_acm_interface(&cdc_class, &cdc_cmd_intf);
    usbd_cdc_add_acm_interface(&cdc_class, &cdc_data_intf);
    /*!< interface add endpoint */
    usbd_interface_add_endpoint(&cdc_data_intf, &cdc_out_ep);
    usbd_interface_add_endpoint(&cdc_data_intf, &cdc_in_ep);
}

volatile uint8_t dtr_enable = 0;

void usbd_cdc_acm_set_dtr(bool dtr)
{
    if (dtr) {
        dtr_enable = 1;
    } else {
        dtr_enable = 0;
    }
}
