#include "usbd_midi.h"
#include "usbd_ctlreq.h"
#include "usbd_ioreq.h"

/* ── Full USB MIDI descriptor ─────────────────────────────────────────────── */
__ALIGN_BEGIN static uint8_t USBD_MIDI_CfgDesc[] __ALIGN_END =
{
    /* Configuration Descriptor */
    0x09,                        /* bLength */
    USB_DESC_TYPE_CONFIGURATION, /* bDescriptorType */
    0x65, 0x00,                  /* wTotalLength */
    0x02,                        /* bNumInterfaces */
    0x01,                        /* bConfigurationValue */
    0x00,                        /* iConfiguration */
    0xC0,                        /* bmAttributes: self powered */
    0x32,                        /* MaxPower: 100mA */

    /* Standard AC Interface Descriptor (Interface 0) */
    0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,

    /* Class-specific AC Interface Descriptor */
    0x09, 0x24, 0x01, 0x00, 0x01, 0x09, 0x00, 0x01, 0x01,

    /* Standard MS Interface Descriptor (Interface 1) */
    0x09, 0x04, 0x01, 0x00, 0x02, 0x01, 0x03, 0x00, 0x00,

    /* Class-specific MS Interface Descriptor */
    0x07, 0x24, 0x01, 0x00, 0x01, 0x41, 0x00,

    /* MIDI IN Jack (Embedded) */
    0x06, 0x24, 0x02, 0x01, 0x01, 0x00,

    /* MIDI IN Jack (External) */
    0x06, 0x24, 0x02, 0x02, 0x02, 0x00,

    /* MIDI OUT Jack (Embedded) */
    0x09, 0x24, 0x03, 0x01, 0x03, 0x01, 0x02, 0x01, 0x00,

    /* MIDI OUT Jack (External) */
    0x09, 0x24, 0x03, 0x02, 0x04, 0x01, 0x01, 0x01, 0x00,

    /* Standard Bulk OUT Endpoint Descriptor */
    0x09, 0x05,
    MIDI_EPOUT_ADDR,             /* bEndpointAddress: OUT */
    0x02,                        /* bmAttributes: Bulk */
    MIDI_EPOUT_SIZE, 0x00,       /* wMaxPacketSize */
    0x00, 0x00, 0x00,

    /* Class-specific MS Bulk OUT Endpoint Descriptor */
    0x05, 0x25, 0x01, 0x01, 0x01,

    /* Standard Bulk IN Endpoint Descriptor */
    0x09, 0x05,
    MIDI_EPIN_ADDR,              /* bEndpointAddress: IN */
    0x02,                        /* bmAttributes: Bulk */
    MIDI_EPOUT_SIZE, 0x00,       /* wMaxPacketSize */
    0x00, 0x00, 0x00,

    /* Class-specific MS Bulk IN Endpoint Descriptor */
    0x05, 0x25, 0x01, 0x01, 0x03,
};

/* ── Receive buffer ───────────────────────────────────────────────────────── */
static uint8_t midi_rx_buf[MIDI_EPOUT_SIZE];

/* ── Class callbacks ──────────────────────────────────────────────────────── */
static uint8_t USBD_MIDI_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    (void)cfgidx;
    USBD_LL_OpenEP(pdev, MIDI_EPOUT_ADDR, USBD_EP_TYPE_BULK, MIDI_EPOUT_SIZE);
    USBD_LL_OpenEP(pdev, MIDI_EPIN_ADDR,  USBD_EP_TYPE_BULK, MIDI_EPOUT_SIZE);
    USBD_LL_PrepareReceive(pdev, MIDI_EPOUT_ADDR, midi_rx_buf, MIDI_EPOUT_SIZE);
    return USBD_OK;
}

static uint8_t USBD_MIDI_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    (void)cfgidx;
    USBD_LL_CloseEP(pdev, MIDI_EPOUT_ADDR);
    USBD_LL_CloseEP(pdev, MIDI_EPIN_ADDR);
    return USBD_OK;
}

static uint8_t USBD_MIDI_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    (void)pdev; (void)req;
    return USBD_OK;
}

static uint8_t USBD_MIDI_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    if (epnum == (MIDI_EPOUT_ADDR & 0x7F)) {
        uint32_t len = USBD_LL_GetRxDataSize(pdev, epnum);
        MIDI_DataRx(midi_rx_buf, len);
        USBD_LL_PrepareReceive(pdev, MIDI_EPOUT_ADDR, midi_rx_buf, MIDI_EPOUT_SIZE);
    }
    return USBD_OK;
}

static uint8_t *USBD_MIDI_GetCfgDesc(uint16_t *length)
{
    *length = sizeof(USBD_MIDI_CfgDesc);
    return USBD_MIDI_CfgDesc;
}

static uint8_t *USBD_MIDI_GetDeviceQualifierDesc(uint16_t *length)
{
    (void)length;
    return NULL;
}

USBD_ClassTypeDef USBD_MIDI = {
    USBD_MIDI_Init,
    USBD_MIDI_DeInit,
    USBD_MIDI_Setup,
    NULL,                        /* EP0_TxSent    */
    NULL,                        /* EP0_RxReady   */
    NULL,                        /* DataIn        */
    USBD_MIDI_DataOut,
    NULL,                        /* SOF           */
    NULL,                        /* IsoINIncomplete  */
    NULL,                        /* IsoOUTIncomplete */
    USBD_MIDI_GetCfgDesc,
    USBD_MIDI_GetCfgDesc,
    USBD_MIDI_GetCfgDesc,
    USBD_MIDI_GetDeviceQualifierDesc,
};

/* ── Parse incoming USB MIDI packets and call NoteOn/Off ─────────────────── */
uint8_t MIDI_DataRx(uint8_t *msg, uint32_t len)
{
    for (uint32_t i = 0; i + 3 < len; i += 4) {
        uint8_t status   = msg[i + 1];
        uint8_t note     = msg[i + 2];
        uint8_t velocity = msg[i + 3];
        uint8_t msg_type = status & 0xF0;
        uint8_t channel  = status & 0x0F;

        if (msg_type == 0x90 && velocity > 0) {
            MIDI_NoteOn(channel, note, velocity);
        } else if (msg_type == 0x80 || (msg_type == 0x90 && velocity == 0)) {
            MIDI_NoteOff(channel, note);
        }
    }
    return USBD_OK;
}
