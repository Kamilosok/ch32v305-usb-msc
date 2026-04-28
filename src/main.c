/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#include "debug.h"
#include <stdio.h>
#include <string.h>

#define nice_return                       \
    do                                    \
    {                                     \
        usb_got = 1;                      \
        NVIC_ClearPendingIRQ(USBFS_IRQn); \
        USBFSD->INT_FG = intflag;         \
        return;                           \
    } while (0)

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

void USBFS_RCC_Init(void)
{
#ifdef CH32V30x_D8C
    RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_USBPHY);
    RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSE);
    RCC_USBHSConfig(RCC_USBPLL_Div2);
    RCC_USBHSPLLCKREFCLKConfig(RCC_USBHSPLLCKREFCLK_4M);
    RCC_USBHSPHYPLLALIVEcmd(ENABLE);
    // THIS IS ALSO CONFIGURED BY TIM1_INT_Init
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);
#else
    if (SystemCoreClock == 144000000)
    {
        RCC_USBFSCLKConfig(RCC_USBFSCLKSource_PLLCLK_Div3);
    }
    else if (SystemCoreClock == 96000000)
    {
        RCC_USBFSCLKConfig(RCC_USBFSCLKSource_PLLCLK_Div2);
    }
    else if (SystemCoreClock == 48000000)
    {
        RCC_USBFSCLKConfig(RCC_USBFSCLKSource_PLLCLK_Div1);
    }
#endif
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBFS, ENABLE);
}

void TIM1_INT_Init(u16 arr, u16 psc)
{

    NVIC_InitTypeDef NVIC_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);

    TIM_TimeBaseInitStructure.TIM_Period = arr;
    TIM_TimeBaseInitStructure.TIM_Prescaler = psc;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &TIM_TimeBaseInitStructure);

    TIM_ARRPreloadConfig(TIM1, ENABLE);

    TIM_ClearFlag(TIM1, TIM_FLAG_Update);
    TIM_ClearITPendingBit(TIM1, TIM_IT_Update);

    NVIC_InitStructure.NVIC_IRQChannel = TIM1_UP_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_ITConfig(TIM1, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM1, ENABLE);
}

static __attribute__((aligned(4))) uint8_t ep0_buf[64];
static __attribute__((aligned(4))) uint8_t ep1_buf[128];

#define EP1_RX_BUF (ep1_buf)
#define EP1_TX_BUF (ep1_buf + 64)

#define CBWSignature (0x43425355)
#define CSWSignature (0x53425355)

#define INQUIRY_OP (0x12)
#define TEST_UNIT_READY_OP (0x00)
#define REQUEST_SENSE_OP (0x03)
#define READ_CAPACITY_10_OP (0x25)
#define READ_10_OP (0x28)
#define MODE_SENSE_6_OP (0x1A)
#define PREVENT_ALLOW_MEDIUM_REMOVAL_OP (0x1E)
#define WRITE_10_OP (0x2A)

uint8_t usb_got = 0;

void usb_min_init(void)
{
    USBFSD->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL;
    Delay_Us(10);
    USBFSD->BASE_CTRL = 0x00;
    Delay_Us(10);

    USBFSD->UEP4_1_MOD = USBFS_UEP1_RX_EN | USBFS_UEP1_TX_EN; // Enable RX TX EP1
    USBFSD->UEP4_1_MOD &= ~USBFS_UEP1_BUF_MOD;
    USBFSD->UEP2_3_MOD = 0;
    USBFSD->UEP5_6_MOD = 0;
    USBFSD->UEP7_MOD = 0;

    USBFSD->UEP0_DMA = (uint32_t)ep0_buf;
    USBFSD->UEP0_TX_LEN = 0;
    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_ACK;
    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;

    USBFSD->UEP1_DMA = (uint32_t)ep1_buf;
    USBFSD->UEP1_TX_LEN = 0;
    USBFSD->UEP1_RX_CTRL = USBFS_UEP_R_RES_ACK | USBFS_UEP_R_AUTO_TOG;
    USBFSD->UEP1_TX_CTRL = USBFS_UEP_T_RES_NAK | USBFS_UEP_T_AUTO_TOG;

    USBFSD->DEV_ADDR = 0x00;

    /* Enable interrupts */
    USBFSD->INT_EN = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER;

    /* Enable device with pull-up, DMA */
    USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;

    /* Enable port */
    USBFSD->UDEV_CTRL = USBFS_UD_PD_DIS | USBFS_UD_PORT_EN;

    NVIC_EnableIRQ(USBFS_IRQn);
}

volatile uint8_t tim1_update_flag = 0;

// Interrupt fast makes it enter only once?
[[gnu::interrupt]]
void TIM1_UP_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM1, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
        tim1_update_flag = 1;
    }
}

typedef struct __attribute__((packed))
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    // For EP0
    uint8_t bMaxPacketSize;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} device_descriptor;

typedef struct __attribute__((packed))
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} config_descriptor;

typedef struct __attribute__((packed))
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} interface_descriptor;

typedef struct __attribute__((packed))
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} endpoint_descriptor;

typedef struct __attribute__((packed))
{
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t bmCBWFlags;
    uint8_t bCBWLUN;
    uint8_t bCBWCBLength;
    uint8_t CBWCB[16];
} cbw;

typedef struct __attribute__((packed))
{
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t bCSWStatus;
} csw;

typedef struct
{
    uint8_t sense_key;
    uint8_t asc;  // Additional Sense Code
    uint8_t ascq; // Additional Sense Code Qualifier
} sense;

typedef struct __attribute__((packed))
{
    uint8_t peripheral;
    uint8_t rmb;
    uint8_t version;
    uint8_t resp_data_format;
    uint8_t additional_length;
    uint8_t flags1;
    uint8_t flags2;
    uint8_t flags3;
    uint8_t vendor[8];
    uint8_t product[16];
    uint8_t revision[4];
} inquiry_data;

typedef struct __attribute__((packed))
{
    uint32_t ret_lba;
    uint32_t block_length;
} read_capacity_data;

typedef struct __attribute__((packed))
{
    uint8_t mode_data_length;
    uint8_t medium_type;
    uint8_t device_specific_parameter;
    uint8_t block_descriptor_length;
} mode_parameter_header_6;

sense current_sense;
csw current_csw;

static inline void set_sense(uint8_t key, uint8_t asc, uint8_t ascq)
{
    current_sense.sense_key = key;
    current_sense.asc = asc;
    current_sense.ascq = ascq;
}

static inline void device_descriptor_init(device_descriptor *dd)
{
    dd->bLength = 18;
    dd->bDescriptorType = 0x01;
    dd->bcdUSB = 0x0200;

    dd->bDeviceClass = 0x00;
    dd->bDeviceSubClass = 0;
    dd->bDeviceProtocol = 0;
    dd->bMaxPacketSize = 64;
    dd->idVendor = 0;
    dd->iProduct = 0;
    dd->bcdDevice = 0x0100;

    dd->iManufacturer = 0;
    dd->iProduct = 0;
    dd->iSerialNumber = 0;
    dd->bNumConfigurations = 1;
}

static inline void config_descriptor_init(config_descriptor *cd)
{
    cd->bLength = 9;
    cd->bDescriptorType = 0x02;
    cd->wTotalLength = sizeof(config_descriptor) + sizeof(interface_descriptor) + 2 * sizeof(endpoint_descriptor); // + (endpoint, and class or vendor specific descriptors
    cd->bNumInterfaces = 1;
    cd->bConfigurationValue = 1;
    cd->iConfiguration = 0; // No string
    cd->bmAttributes = 0x80;
    cd->bMaxPower = 50; // 100 mA
}

static inline void interface_descriptor_init(interface_descriptor *ic)
{
    ic->bLength = 9;
    ic->bDescriptorType = 0x04;
    ic->bInterfaceNumber = 0;
    ic->bAlternateSetting = 0;
    ic->bNumEndpoints = 2; // EP1 IN and OUT
    ic->bInterfaceClass = 0x08;
    ic->bInterfaceSubClass = 0x06;
    ic->bInterfaceProtocol = 0x50;
    ic->iInterface = 0;
}

// Bulk in only rn
static inline void endpoint_descriptor_init(endpoint_descriptor *ed, uint8_t ep_num, uint8_t is_in)
{
    ed->bLength = 7;
    ed->bDescriptorType = 0x05;
    ed->bEndpointAddress = ((is_in & 0x01) << 7) | (ep_num & 0b1111);
    ed->bmAttributes = 0x02; // Bulk
    ed->wMaxPacketSize = 64;
    // Ignored for Bulk & Control Endpoints
    ed->bInterval = 0;
}

static inline void inquiry_data_init(inquiry_data *id)
{
    id->peripheral = 0x00;
    id->rmb = 0x80;
    id->version = (1 << 7); // Compliant with SPC-3, but we don't support a lot of it
    id->resp_data_format = 0x02;
    id->additional_length = 31; // Number of bytes following this field
    id->flags1 = 0;
    id->flags2 = 0; // Maybe look into QUE and CMDQUE
    id->flags3 = 0;

    memset(id->vendor, ' ', sizeof(id->vendor));
    memset(id->product, ' ', sizeof(id->product));
    memset(id->revision, ' ', sizeof(id->revision));
    memcpy(id->vendor, "NONE", 4);             // 8
    memcpy(id->product, "RISC-V SOC MSC", 15); // 16
    memcpy(id->revision, "0.01", 4);           // 4
}

static inline void mode_parameter_header_6_init(mode_parameter_header_6 *mph6)
{
    // Only the header
    mph6->mode_data_length = sizeof(mode_parameter_header_6) - 1;
    // Data medium?
    mph6->medium_type = 0x00;
    mph6->device_specific_parameter = 0x00;
    // No blocks
    mph6->block_descriptor_length = 0;
}

#define NUM_LBA (0x0036)
#define LBA_LENGTH (0x0200)

static uint8_t disk[NUM_LBA][LBA_LENGTH];

static inline uint32_t l_to_b_u32(uint32_t num)
{
    return ((num & 0xFF000000) >> 24) | ((num & 0x00FF0000) >> 8) | ((num & 0x0000FF00) << 8) | ((num & 0x000000FF) << 24);
}

static inline void read_capacity_data_init(read_capacity_data *rcd)
{
    // Big endian nums
    rcd->ret_lba = l_to_b_u32(NUM_LBA - 1);
    rcd->block_length = l_to_b_u32(LBA_LENGTH);
}

uint8_t before_csw = 0;
uint64_t total_bytes = 0;
uint64_t sent_bytes = 0;
uint64_t received_bytes = 0;
uint64_t start_LBA = 0;
uint32_t cbw_tag = 0;

static inline uint8_t validCBW(cbw *CBW)
{
    if (CBW->dCBWSignature != CBWSignature || USBFSD->RX_LEN != sizeof(cbw) || before_csw)
        return 0;
    return 1;
}

static inline uint8_t meaningfulCBW(cbw *CBW)
{
    if (((CBW->bCBWLUN & 0xF0) != 0) || ((CBW->bCBWCBLength & 0xE0) != 0) || CBW->bCBWLUN != 0)
        return 0;

    // TODO: Check if the command is actually supported

    return 1;
}

device_descriptor dd;
config_descriptor cd;
interface_descriptor id;
endpoint_descriptor ed1_out;
endpoint_descriptor ed1_in;

uint8_t dev_addr = 0;
uint8_t send_len = sizeof(device_descriptor);
// Maybe change this to an enum later
uint8_t addr_stage = 0;
uint8_t config_stage = 0;
uint8_t configured = 0;
uint8_t bot_stage = 0;
uint8_t write_stage = 0;

uint8_t prevent_medium_removal;

[[gnu::interrupt]]
void USBFS_IRQHandler(void)
{
    uint16_t len, wValue, wIndex, wLength;
    uint8_t bmRequestType, bRequest;
    uint8_t intflag, stflag, errflag = 0;

    intflag = USBFSD->INT_FG;
    stflag = USBFSD->INT_ST;

    uint8_t endp = stflag & USBFS_UIS_ENDP_MASK;

    if (intflag & USBFS_UIF_TRANSFER)
    {
        if (endp == 0 || !configured)
        {
            // 0x80 -> Device to Host to standard device
            bmRequestType = ep0_buf[0];
            // 0x06 -> Get descriptor
            bRequest = ep0_buf[1];
            // 0x0100 -> Device descriptor
            wValue = (ep0_buf[3] << 8) + ep0_buf[2];
            // 0x0000 -> Index 0
            wIndex = (ep0_buf[5] << 8) + ep0_buf[4];
            // 0x0040 -> 64 bytes
            wLength = (ep0_buf[7] << 8) + ep0_buf[6];

            if ((stflag & USBFS_UIS_TOKEN_MASK) == USBFS_UIS_TOKEN_SETUP)
            {
                len = USBFSD->RX_LEN;

                if (bmRequestType == 0x80 && bRequest == USB_GET_DESCRIPTOR && (wValue >> 8) == USB_DESCR_TYP_DEVICE)
                {
                    send_len = sizeof(device_descriptor);

                    if (wLength < send_len)
                        send_len = wLength;

                    memcpy(ep0_buf, &dd, send_len);

                    USBFSD->UEP0_TX_LEN = send_len;
                    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;

                    printf("Device descriptor\r\n");
                }
                else if (bmRequestType == 0x00 && bRequest == USB_SET_ADDRESS)
                {
                    // Set AFTER setup stage
                    dev_addr = wValue;
                    USBFSD->UEP0_TX_LEN = 0;
                    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                    addr_stage = 1;
                    printf("Address setting\r\n");
                }
                else if (bmRequestType == 0x80 && bRequest == USB_GET_DESCRIPTOR && (wValue >> 8) == USB_DESCR_TYP_QUALIF)
                {
                    // Full-Speed device must respond with a request error
                    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_STALL;
                    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_STALL;
                    printf("Qualifier\r\n");
                }
                else if (bmRequestType == 0x80 && bRequest == USB_GET_DESCRIPTOR && (wValue >> 8) == USB_DESCR_TYP_CONFIG)
                {
                    send_len = sizeof(config_descriptor) + sizeof(interface_descriptor) + 2 * sizeof(endpoint_descriptor);

                    if (wLength < send_len)
                        send_len = wLength;

                    memcpy(ep0_buf, &cd, sizeof(config_descriptor));
                    // We need to send the config descriptor by itself first, then everything
                    if (send_len > sizeof(config_descriptor))
                    {
                        memcpy(ep0_buf + sizeof(config_descriptor), &id, sizeof(interface_descriptor));
                        memcpy(ep0_buf + sizeof(config_descriptor) + sizeof(interface_descriptor), &ed1_out, sizeof(endpoint_descriptor));
                        memcpy(ep0_buf + sizeof(config_descriptor) + sizeof(interface_descriptor) + sizeof(endpoint_descriptor), &ed1_in, sizeof(endpoint_descriptor));
                    }

                    USBFSD->UEP0_TX_LEN = send_len;
                    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                    config_stage = 1;
                    printf("%u CONFIG\r\n", send_len);
                }
                else if (bmRequestType == 0x00 && bRequest == USB_SET_CONFIGURATION)
                {

                    // We only have 1 configuration, so we ignore wValue
                    uint8_t config_value = wValue;
                    configured = 1;

                    USBFSD->UEP0_TX_LEN = 0;
                    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                    printf("%u SET CONFIG\r\n", config_value);
                }
                else
                {
                    // Somehow getting here?
                    printf("BAD\r\n");
                }
            }
            else if ((stflag & USBFS_UIS_TOKEN_MASK) == USBFS_UIS_TOKEN_IN)
            {
                if (addr_stage)
                {
                    addr_stage = 0;
                    USBFSD->DEV_ADDR = dev_addr;
                    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                }
                else if (config_stage)
                {
                    config_stage = 0;
                    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
                }
                // printf("IN\r\n");
            }
            else if ((stflag & USBFS_UIS_TOKEN_MASK) == USBFS_UIS_TOKEN_OUT)
            {
                USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_ACK;
                // printf("OUT\r\n");
            }
        }
        else if (endp == 1)
        {
            // printf("EP1\r\n");
            if ((stflag & USBFS_UIS_TOKEN_MASK) == USBFS_UIS_TOKEN_OUT)
            {
                if (write_stage)
                {
                    // Potentially slow
                    uint16_t chunk1 = (total_bytes - received_bytes > 64) ? 64 : (total_bytes - received_bytes);
                    uint16_t chunk2 = 0;
                    uint64_t curr_LBA = start_LBA + received_bytes / LBA_LENGTH;
                    uint16_t off_in_block = received_bytes % LBA_LENGTH;

                    if (off_in_block + chunk1 > LBA_LENGTH)
                    {
                        chunk2 = off_in_block + chunk1 - LBA_LENGTH;
                        chunk1 -= chunk2;
                        printf("LBA border W!\r\n");
                    }

                    printf("RX_BUF:\r\n");
                    for (int i = 0; i < chunk1 + chunk2; i++)
                        printf("%02x ", EP1_RX_BUF[i]);
                    printf("\r\n");

                    memcpy(disk[curr_LBA] + off_in_block, EP1_RX_BUF, chunk1);
                    if (curr_LBA < NUM_LBA - 1)
                        memcpy(disk[curr_LBA + 1], EP1_RX_BUF + chunk1, chunk2);
                    else
                        printf("WRITING TOO FAR\r\n");

                    received_bytes += chunk1 + chunk2;
                    current_csw.dCSWDataResidue = total_bytes - received_bytes;

                    if (total_bytes == received_bytes)
                    {
                        write_stage = 0;
                        before_csw = 0;
                        total_bytes = 0;
                        received_bytes = 0;

                        memcpy(EP1_TX_BUF, &current_csw, sizeof(csw));
                        USBFSD->UEP1_TX_LEN = sizeof(csw);
                        printf("OFF\r\n");
                        USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;
                    }
                    else
                        USBFSD->UEP1_RX_CTRL = ((USBFSD->UEP1_RX_CTRL) & ~0b11) | USBFS_UEP_R_RES_ACK;

                    nice_return;
                }
                else
                {
                    // printf("OUT\r\n");
                    cbw CBW;
                    memcpy(&CBW, EP1_RX_BUF, sizeof(cbw));
                    len = USBFSD->RX_LEN;

                    if (!validCBW(&CBW) || !meaningfulCBW(&CBW))
                    {
                        printf("Invalid CBW\r\n");
                        USBFSD->UEP1_TX_LEN = 0;
                        //  The device shall STALL the Bulk-In pipe. Also, the device shall either STALL the Bulk-Out pipe or ... So we stall both
                        USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_STALL;
                        USBFSD->UEP1_RX_CTRL = ((USBFSD->UEP1_RX_CTRL) & ~0b11) | USBFS_UEP_R_RES_STALL;

                        nice_return;
                    }

                    cbw_tag = CBW.dCBWTag;
                    before_csw = 1;

                    // printf("CBCW:\r\n");

                    for (int i = 0; i < CBW.bCBWCBLength; i++)
                    {
                        // printf("%02X ", CBW.CBWCB[i]);
                    }
                    // printf("\r\n");

                    uint8_t opcode = CBW.CBWCB[0];
                    csw instaCSW;
                    instaCSW.dCSWSignature = CSWSignature;
                    instaCSW.dCSWTag = cbw_tag;
                    current_csw.dCSWSignature = CSWSignature;
                    current_csw.dCSWTag = cbw_tag;
                    uint16_t data_to_transfer;
                    uint64_t LBA;
                    // TODO: CONTROL checking
                    switch (opcode)
                    {
                    case INQUIRY_OP:
                        printf("Inquiry\r\n");
                        uint8_t evpd = CBW.CBWCB[1] & 0x01;
                        uint8_t page_code = CBW.CBWCB[2];
                        uint8_t alloc_len = MAX(5, ((CBW.CBWCB[3] << 8) | CBW.CBWCB[4]));

                        // No support for Vital Product Data
                        if (evpd != 0)
                        {
                            set_sense(0x05, 0x20, 0x00);
                            instaCSW.dCSWDataResidue = CBW.dCBWDataTransferLength;
                            instaCSW.bCSWStatus = 0x01;

                            memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                            USBFSD->UEP1_TX_LEN = sizeof(csw);
                            USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                            nice_return;
                        }

                        if (page_code != 0)
                        {
                            set_sense(0x05, 0x24, 0x00);
                            instaCSW.dCSWDataResidue = CBW.dCBWDataTransferLength;
                            instaCSW.bCSWStatus = 0x01;

                            memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                            USBFSD->UEP1_TX_LEN = sizeof(csw);
                            USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                            nice_return;
                        }

                        // If data_to_transfer > uint16_max 4.3.4.6

                        set_sense(0, 0, 0);

                        data_to_transfer = MIN(sizeof(inquiry_data), MIN(alloc_len, CBW.dCBWDataTransferLength));
                        current_csw.dCSWDataResidue = CBW.dCBWDataTransferLength - data_to_transfer;
                        current_csw.bCSWStatus = 0x00;

                        inquiry_data iq;
                        inquiry_data_init(&iq);
                        memcpy(EP1_TX_BUF, &iq, sizeof(inquiry_data));
                        USBFSD->UEP1_TX_LEN = data_to_transfer;
                        USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                        // Send CSW ok in next IN
                        nice_return;

                    case TEST_UNIT_READY_OP:
                        // printf("Test Unit Ready\r\n");
                        instaCSW.dCSWDataResidue = CBW.dCBWDataTransferLength; // Should be 0
                        instaCSW.bCSWStatus = 0x00;                            // When a storage medium is added respond accordingly

                        memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                        USBFSD->UEP1_TX_LEN = sizeof(csw);
                        USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                        nice_return;

                    case READ_CAPACITY_10_OP:
                        printf("Read Capacity (10)\r\n");

                        LBA = CBW.CBWCB[2] << 24 | CBW.CBWCB[3] << 16 | CBW.CBWCB[4] << 8 | CBW.CBWCB[5];
                        uint8_t pmi = CBW.CBWCB[8] & 0x01;

                        // No pmi=1 handling for now
                        if (LBA != 0 || pmi != 0)
                        {
                            set_sense(0x05, 0x24, 0x00);
                            instaCSW.dCSWDataResidue = CBW.dCBWDataTransferLength;
                            instaCSW.bCSWStatus = 0x01;

                            memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                            USBFSD->UEP1_TX_LEN = sizeof(csw);
                            USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                            nice_return;
                        }

                        set_sense(0, 0, 0);
                        data_to_transfer = MIN(sizeof(read_capacity_data), CBW.dCBWDataTransferLength);
                        current_csw.dCSWDataResidue = 0; // CBW.dCBWDataTransferLength - data_to_transfer
                        current_csw.bCSWStatus = 0x00;

                        read_capacity_data rcd;
                        read_capacity_data_init(&rcd);
                        memcpy(EP1_TX_BUF, &rcd, data_to_transfer);
                        USBFSD->UEP1_TX_LEN = data_to_transfer;
                        USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                        // Send CSW ok in next IN
                        nice_return;

                    case READ_10_OP:
                        printf("Read (10)\r\n");
                        uint8_t flags = CBW.CBWCB[1];
                        LBA = (CBW.CBWCB[2] << 24) | (CBW.CBWCB[3] << 16) | (CBW.CBWCB[4] << 8) | (CBW.CBWCB[5]);
                        __attribute__((unused)) uint8_t group_number = CBW.CBWCB[6] & 0b00011111;
                        uint16_t transfer_length = CBW.CBWCB[7] << 8 | CBW.CBWCB[8];

                        // Any protection code except 000 we don't support
                        if (flags & 0b11100000)
                        {
                            set_sense(0x05, 0x24, 0x00);
                            instaCSW.dCSWDataResidue = CBW.dCBWDataTransferLength;
                            instaCSW.bCSWStatus = 0x01;

                            memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                            USBFSD->UEP1_TX_LEN = sizeof(csw);
                            USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                            nice_return;
                        }

                        // Ignore DPO because no caching
                        // Ignore FUA and FUA_NV because no specific storage mediums for now

                        if (LBA + transfer_length > NUM_LBA)
                        {
                            set_sense(0x05, 0x21, 0x00);
                            instaCSW.dCSWDataResidue = CBW.dCBWDataTransferLength;
                            instaCSW.bCSWStatus = 0x01;

                            memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                            USBFSD->UEP1_TX_LEN = sizeof(csw);
                            USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                            nice_return;
                        }

                        // Ignore group number

                        if (transfer_length == 0)
                        {
                            instaCSW.dCSWDataResidue = 0;
                            instaCSW.bCSWStatus = 0x00;

                            memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                            USBFSD->UEP1_TX_LEN = sizeof(csw);
                            USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                            nice_return;
                        }
                        else
                        {
                            start_LBA = LBA;
                            total_bytes = transfer_length * LBA_LENGTH;
                            sent_bytes = 0;

                            set_sense(0, 0, 0);
                            data_to_transfer = MIN(sizeof(read_capacity_data), CBW.dCBWDataTransferLength);
                            current_csw.dCSWDataResidue = (uint32_t)(total_bytes - sent_bytes);
                            current_csw.bCSWStatus = 0x00;

                            // Data stage
                            uint16_t chunk = (total_bytes > 64) ? 64 : total_bytes;

                            memcpy(EP1_TX_BUF, disk[start_LBA], chunk);
                            USBFSD->UEP1_TX_LEN = chunk;
                            sent_bytes += chunk;

                            USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;
                            nice_return;
                        }

                    case MODE_SENSE_6_OP:
                        printf("Mode sense (6)\r\n");
                        __attribute__((unused)) uint8_t dbd = CBW.CBWCB[1] & 0b00001000;
                        uint8_t pc_pg = CBW.CBWCB[2];
                        uint8_t subpage_code = CBW.CBWCB[3];
                        uint8_t allocation_len = CBW.CBWCB[4];

                        // Ignore db because no block descriptors

                        // Ignore pc because no mode pages

                        // Error on any page code we don't support + exception
                        if (((pc_pg & 0x3F) != 0x3F || subpage_code != 0x00) && pc_pg != 0)
                        {
                            set_sense(0x05, 0x24, 0x00);
                            instaCSW.dCSWDataResidue = CBW.dCBWDataTransferLength;
                            instaCSW.bCSWStatus = 0x01;

                            memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                            USBFSD->UEP1_TX_LEN = sizeof(csw);
                            USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                            nice_return;
                        }

                        set_sense(0, 0, 0);
                        data_to_transfer = MIN(sizeof(mode_parameter_header_6), MIN(allocation_len, CBW.dCBWDataTransferLength));
                        // Short transfer!
                        current_csw.dCSWDataResidue = CBW.dCBWDataTransferLength - data_to_transfer;
                        current_csw.bCSWStatus = 0x00;

                        mode_parameter_header_6 mph6;
                        mode_parameter_header_6_init(&mph6);

                        memcpy(EP1_TX_BUF, &mph6, data_to_transfer);
                        USBFSD->UEP1_TX_LEN = data_to_transfer;

                        USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                        // Send CSW ok in next IN
                        nice_return;

                    case PREVENT_ALLOW_MEDIUM_REMOVAL_OP:
                        printf("Prevent allow medium removal\r\n");

                        // No handling for now
                        prevent_medium_removal = CBW.CBWCB[4] & 0b11;

                        set_sense(0, 0, 0);
                        instaCSW.dCSWDataResidue = 0;
                        instaCSW.bCSWStatus = 0x00;

                        memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                        USBFSD->UEP1_TX_LEN = sizeof(csw);
                        USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                        nice_return;

                    case WRITE_10_OP:
                        printf("Write (10)\r\n");
                        uint8_t flags_w = CBW.CBWCB[1];
                        LBA = (CBW.CBWCB[2] << 24) | (CBW.CBWCB[3] << 16) | (CBW.CBWCB[4] << 8) | (CBW.CBWCB[5]);
                        __attribute__((unused)) uint8_t group_number_w = CBW.CBWCB[6] & 0b00011111;
                        uint16_t transfer_length_w = (CBW.CBWCB[7] << 8) | (CBW.CBWCB[8]);

                        // Any protection code except 000 we don't support
                        if (flags_w & 0b11100000)
                        {
                            set_sense(0x05, 0x24, 0x00);
                            instaCSW.dCSWDataResidue = CBW.dCBWDataTransferLength;
                            instaCSW.bCSWStatus = 0x01;

                            memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                            USBFSD->UEP1_TX_LEN = sizeof(csw);
                            USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                            nice_return;
                        }

                        // Ignore DPO because no caching
                        // Ignore FUA and FUA_NV because no specific storage mediums for now

                        if (LBA + transfer_length_w > NUM_LBA)
                        {
                            set_sense(0x05, 0x21, 0x00);
                            instaCSW.dCSWDataResidue = CBW.dCBWDataTransferLength;
                            instaCSW.bCSWStatus = 0x01;

                            memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                            USBFSD->UEP1_TX_LEN = sizeof(csw);
                            USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                            nice_return;
                        }

                        // Ignore group number

                        if (transfer_length_w == 0)
                        {
                            instaCSW.dCSWDataResidue = 0;
                            instaCSW.bCSWStatus = 0x00;

                            memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                            USBFSD->UEP1_TX_LEN = sizeof(csw);
                            USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                            nice_return;
                        }
                        else
                        {
                            write_stage = 1;
                            start_LBA = LBA;
                            total_bytes = transfer_length_w * LBA_LENGTH;
                            received_bytes = 0;

                            set_sense(0, 0, 0);
                            // Changed later only on short transfers
                            current_csw.dCSWDataResidue = (uint32_t)(total_bytes - received_bytes);
                            current_csw.bCSWStatus = 0x00;

                            USBFSD->UEP1_RX_CTRL = ((USBFSD->UEP1_RX_CTRL) & ~0b11) | USBFS_UEP_R_RES_ACK;

                            nice_return;
                        }
                    // TODO: Host asks for sense data if something is wrong
                    case REQUEST_SENSE_OP:
                        printf("Request Sense\r\n");

                        nice_return;

                    default:
                        printf("Unsupported opcode: %02X\r\n", opcode);

                        nice_return;
                    }
                }
            }
            else if ((stflag & USBFS_UIS_TOKEN_MASK) == USBFS_UIS_TOKEN_IN)
            {
                // printf("IN\r\n");

                if (before_csw)
                {
                    if (total_bytes == sent_bytes)
                    {
                        before_csw = 0;
                        total_bytes = 0;
                        sent_bytes = 0;

                        memcpy(EP1_TX_BUF, &current_csw, sizeof(csw));
                        USBFSD->UEP1_TX_LEN = sizeof(csw);

                        USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                        nice_return;
                    }
                    else
                    {
                        //  Potentially slow
                        uint16_t chunk1 = (total_bytes - sent_bytes > 64) ? 64 : (total_bytes - sent_bytes);
                        uint16_t chunk2 = 0;
                        uint64_t curr_LBA = start_LBA + sent_bytes / LBA_LENGTH;
                        uint16_t off_in_block = sent_bytes % 512;

                        if (off_in_block + chunk1 > LBA_LENGTH)
                        {
                            chunk2 = off_in_block + chunk1 - LBA_LENGTH;
                            chunk1 -= chunk2;
                            printf("LBA border!\r\n");
                        }

                        printf("Reading from LBA %llu at offset %u %u bytes\r\n", curr_LBA, off_in_block, chunk1);

                        memcpy(EP1_TX_BUF, disk[curr_LBA] + off_in_block, chunk1);
                        if (curr_LBA < NUM_LBA - 1)
                            memcpy(EP1_TX_BUF + chunk1, disk[curr_LBA + 1], chunk2);
                        else
                            printf("READING TOO FAR\r\n");

                        USBFSD->UEP1_TX_LEN = chunk1 + chunk2;
                        sent_bytes += chunk1 + chunk2;
                        current_csw.dCSWDataResidue = total_bytes - sent_bytes;

                        USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                        nice_return;
                    }
                }

                USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_NAK;
            }
        }
    }

    nice_return;
}

int main(void)
{

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("SystemClk:%lu\r\n", SystemCoreClock);
    printf("ChipID:%08lu\r\n", DBGMCU_GetCHIPID());

    // Interrupt every 1s
    TIM1_INT_Init(3000 - 1, 48000 - 1);
    printf("TIM1 initialized\r\n");

    /* USBFS Device init, till setup */

    USBFS_RCC_Init();
    printf("USBFS clock config done\r\n");
    device_descriptor_init(&dd);
    config_descriptor_init(&cd);
    interface_descriptor_init(&id);
    endpoint_descriptor_init(&ed1_out, 1, 0);
    endpoint_descriptor_init(&ed1_in, 1, 1);

    memset(disk, 0, NUM_LBA * LBA_LENGTH);

    usb_min_init();

    printf("USBFS device init done\r\n");

    while (1)
    {

        if (tim1_update_flag)
        {
            tim1_update_flag = 0;
            printf(".\r\n");
        }

        if (usb_got)
        {
            usb_got = 0;
            // printf("\r\n");
        }
    }
}