/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <debug.h>

#include <usb_descriptors.h>
#include <msc.h>
#include <scsi.h>

// DEBUG is already taken
#ifndef DO_DEBUG
#define printf(...) \
    do              \
    {               \
    } while (0)
#endif

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define FLASH_PAGE_SIZE (256ul)
#define STORAGE_PAGE_FIRST (256ul)
#define STORAGE_PAGE_LAST (496ul)

#define NUM_LBA (0x0080ul)
#define PAGE_LENGTH (0x0100ul)
#define LBA_LENGTH (0x0200ul)

#define STORAGE_BASE (0x08000000ul + STORAGE_PAGE_FIRST * FLASH_PAGE_SIZE)
#define STORAGE_SIZE (NUM_LBA * PAGE_LENGTH)

#define ERASED_WORD (0xe339e339)

static __attribute__((aligned(4))) uint8_t page_cache[PAGE_LENGTH];

static __attribute__((aligned(4))) uint8_t ep0_buf[64];
static __attribute__((aligned(4))) uint8_t ep1_buf[128];

#define EP1_RX_BUF (ep1_buf)
#define EP1_TX_BUF (ep1_buf + 64)

#define nice_return                       \
    do                                    \
    {                                     \
        usb_update_flag = 1;              \
        NVIC_ClearPendingIRQ(USBFS_IRQn); \
        USBFSD->INT_FG = intflag;         \
        return;                           \
    } while (0)

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

static volatile uint8_t tim1_update_flag = 0;

[[gnu::interrupt]]
void TIM1_UP_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM1, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
        tim1_update_flag = 1;
    }
}

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

void USBFS_MSC_INIT(void)
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

static const device_descriptor dd = {
    .bLength = 18,
    .bDescriptorType = 0x01,
    .bcdUSB = 0x0200,

    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize = 64,
    .idVendor = 0,
    .idProduct = 0,
    .bcdDevice = 0x0100,

    // Change iSerial when adding win
    .iManufacturer = 0,
    .iProduct = 0,
    .iSerialNumber = 0,
    .bNumConfigurations = 1,
};

static const config_descriptor cd = {
    .bLength = 9,
    .bDescriptorType = 0x02,
    .wTotalLength = sizeof(config_descriptor) + sizeof(interface_descriptor) + 2 * sizeof(endpoint_descriptor), // + (endpoint, and class or vendor specific descriptors
    .bNumInterfaces = 1,
    .bConfigurationValue = 1,
    .iConfiguration = 0, // No string
    .bmAttributes = 0x80,
    .bMaxPower = 50, // 100 mA
};

static const interface_descriptor id = {
    .bLength = 9,
    .bDescriptorType = 0x04,
    .bInterfaceNumber = 0,
    .bAlternateSetting = 0,
    .bNumEndpoints = 2, // EP1 IN and OUT
    .bInterfaceClass = 0x08,
    .bInterfaceSubClass = 0x06,
    .bInterfaceProtocol = 0x50,
    .iInterface = 0,
};

// Bulk in only for now
static const endpoint_descriptor ed1_in = {
    .bLength = 7,
    .bDescriptorType = 0x05,
    // In EP1
    .bEndpointAddress = ((1 & 0x01) << 7) | (1 & 0b1111),
    .bmAttributes = 0x02, // Bulk
    .wMaxPacketSize = 64,
    // Ignored for Bulk & Control Endpoints
    .bInterval = 0,
};

static const endpoint_descriptor ed1_out = {
    .bLength = 7,
    .bDescriptorType = 0x05,
    // Out EP1
    .bEndpointAddress = ((0 & 0x01) << 7) | (1 & 0b1111),
    .bmAttributes = 0x02, // Bulk
    .wMaxPacketSize = 64,
    // Ignored for Bulk & Control Endpoints
    .bInterval = 0,
};

static const inquiry_data iq = {
    .peripheral = 0x00,
    .rmb = 0x80,

    .version = (1 << 7), // Compliant with SPC-3, but we don't support a lot of it

    .resp_data_format = 0x02,
    .additional_length = 31,

    .flags1 = 0,
    .flags2 = 0, // Maybe look into QUE and CMDQUE
    .flags3 = 0,

    .vendor = "NONE    ",          // 8
    .product = "RISC-V SOC MSC  ", // 16
    .revision = "0.01",            // 4
};

static const mode_parameter_header_6 mph6 = {
    // Only the header
    .mode_data_length = sizeof(mode_parameter_header_6) - 1,
    // Data medium?
    .medium_type = 0x00,
    .device_specific_parameter = 0x00,
    // No blocks
    .block_descriptor_length = 0,
};

static const read_capacity_data rcd = {
    // Big endian nums
    .ret_lba = __builtin_bswap32(NUM_LBA - 1),
    .block_length = __builtin_bswap32(LBA_LENGTH),
};

uint64_t total_bytes = 0;
uint64_t sent_bytes = 0;
uint64_t received_bytes = 0;
uint64_t start_LBA = 0;

uint8_t dev_addr = 0;
uint8_t send_len = sizeof(device_descriptor);
// Maybe change this to an enum later
uint8_t addr_stage = 0;
uint8_t config_stage = 0;
uint8_t configured = 0;
uint8_t bot_stage = 0;
uint8_t write_stage = 0;
uint8_t prevent_medium_removal;

uint32_t first_invalid_lba = 0;

csw current_csw;

static volatile uint8_t usb_update_flag = 0;

[[gnu::interrupt]]
void USBFS_IRQHandler(void)
{
    uint16_t __attribute__((unused)) len, wValue, __attribute__((unused)) wIndex, wLength;
    uint8_t bmRequestType, bRequest;
    uint8_t intflag, stflag, __attribute__((unused)) errflag = 0;

    intflag = USBFSD->INT_FG;
    stflag = USBFSD->INT_ST;

    uint8_t endp = stflag & USBFS_UIS_ENDP_MASK;

    if (intflag & USBFS_UIF_TRANSFER)
    {
        if (endp == 0 || !configured)
        {
            // printf("EP0\r\n");
            bmRequestType = ep0_buf[0];
            bRequest = ep0_buf[1];
            wValue = (ep0_buf[3] << 8) + ep0_buf[2];
            wIndex = (ep0_buf[5] << 8) + ep0_buf[4];
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
                else if (bmRequestType == 0xa1 && bRequest == USB_GET_MAX_LUN)
                {
                    USBFSD->UEP0_TX_LEN = 1;
                    memcpy(ep0_buf, get_max_LUN(), 1);
                    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                }
                else
                {
                    printf("BAD\r\n");
                    printf("bmRequest %x bRequest %x wValue %x\r\n", bmRequestType, bRequest, wValue);
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
                    uint32_t curr_PAGE = 2 * start_LBA + received_bytes / PAGE_LENGTH;
                    uint16_t off_in_page = received_bytes % PAGE_LENGTH;
                    uint32_t page_addr = STORAGE_BASE + (STORAGE_PAGE_FIRST + curr_PAGE) * PAGE_LENGTH;

                    if (off_in_page + chunk1 >= PAGE_LENGTH)
                    {
                        chunk2 = off_in_page + chunk1 - PAGE_LENGTH;
                        chunk1 -= chunk2;

                        memcpy(page_cache + off_in_page, EP1_RX_BUF, chunk1);

                        __disable_irq();
                        FLASH_Unlock_Fast();
                        FLASH_ErasePage_Fast(page_addr);
                        FLASH_ProgramPage_Fast(page_addr, (uint32_t *)page_cache);
                        FLASH_Lock_Fast();
                        __enable_irq();

                        memcpy(page_cache, EP1_RX_BUF + chunk1, chunk2);
                    }
                    else
                    {
                        memcpy(page_cache + off_in_page, EP1_RX_BUF, chunk1);
                    }

                    received_bytes += chunk1 + chunk2;
                    current_csw.dCSWDataResidue = total_bytes - received_bytes;

                    if (total_bytes == received_bytes)
                    {
                        if ((received_bytes % PAGE_LENGTH) != 0)
                        {
                            // Get the page data
                            __attribute__((aligned(4))) uint8_t prev_page[PAGE_LENGTH];
                            memcpy(prev_page, (uint8_t *)page_addr, PAGE_LENGTH);

                            // Update it by the chunk
                            memcpy(prev_page, page_cache, off_in_page + chunk1);
                            __disable_irq();
                            FLASH_Unlock_Fast();
                            FLASH_ErasePage_Fast(page_addr);
                            FLASH_ProgramPage_Fast(page_addr, (uint32_t *)prev_page);
                            FLASH_Lock_Fast();
                            __enable_irq();
                        }

                        write_stage = 0;
                        set_before_csw(0);
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

                    if (!validCBW(&CBW))
                    {
                        printf("Invalid CBW\r\n");
                        USBFSD->UEP1_TX_LEN = 0;
                        //  The device shall STALL the Bulk-In pipe. Also, the device shall either STALL the Bulk-Out pipe or ... So we stall both
                        USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_STALL;
                        USBFSD->UEP1_RX_CTRL = ((USBFSD->UEP1_RX_CTRL) & ~0b11) | USBFS_UEP_R_RES_STALL;

                        nice_return;
                    }

                    set_cbw_tag(CBW.dCBWTag);
                    csw instaCSW;
                    instaCSW.dCSWSignature = CSWSignature;
                    instaCSW.dCSWTag = get_cbw_tag();

                    if (!meaningfulCBW(&CBW))
                    {
                        printf("Unmeaningful CBW\r\n");

                        // The response of a device to a CBW that is not meaningful is not specified
                        USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_STALL;
                        USBFSD->UEP1_RX_CTRL = ((USBFSD->UEP1_RX_CTRL) & ~0b11) | USBFS_UEP_R_RES_STALL;
                    }

                    if (!command_supported(CBW.CBWCB[0]))
                    {
                        set_sense(0x05, 0x20, 0x00, 0);
                        set_field_pointer(0);

                        instaCSW.dCSWDataResidue = CBW.dCBWDataTransferLength;
                        instaCSW.bCSWStatus = 0x01;

                        memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                        USBFSD->UEP1_TX_LEN = sizeof(csw);
                        USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                        nice_return;
                    }

                    set_before_csw(1);

                    uint8_t opcode = CBW.CBWCB[0];
                    current_csw.dCSWSignature = CSWSignature;
                    current_csw.dCSWTag = get_cbw_tag();
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
                            set_sense(0x05, 0x20, 0x00, 0);
                            set_error_pointers(0, 1);
                            instaCSW.dCSWDataResidue = CBW.dCBWDataTransferLength;
                            instaCSW.bCSWStatus = 0x01;

                            memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                            USBFSD->UEP1_TX_LEN = sizeof(csw);
                            USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                            nice_return;
                        }

                        if (page_code != 0)
                        {
                            set_sense(0x05, 0x24, 0x00, 0);
                            set_field_pointer(2);
                            instaCSW.dCSWDataResidue = CBW.dCBWDataTransferLength;
                            instaCSW.bCSWStatus = 0x01;

                            memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                            USBFSD->UEP1_TX_LEN = sizeof(csw);
                            USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                            nice_return;
                        }

                        // If data_to_transfer > uint16_max 4.3.4.6

                        set_sense(0, 0, 0, 0);

                        data_to_transfer = MIN(sizeof(inquiry_data), MIN(alloc_len, CBW.dCBWDataTransferLength));
                        current_csw.dCSWDataResidue = CBW.dCBWDataTransferLength - data_to_transfer;
                        current_csw.bCSWStatus = 0x00;

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
                        if (pmi != 0 || LBA != 0)
                        {
                            set_sense(0x05, 0x24, 0x00, 0);
                            set_field_pointer(2);

                            instaCSW.dCSWDataResidue = CBW.dCBWDataTransferLength;
                            instaCSW.bCSWStatus = 0x01;

                            memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                            USBFSD->UEP1_TX_LEN = sizeof(csw);
                            USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                            nice_return;
                        }

                        set_sense(0, 0, 0, 0);
                        data_to_transfer = CBW.dCBWDataTransferLength;
                        current_csw.dCSWDataResidue = 0; // CBW.dCBWDataTransferLength - data_to_transfer
                        current_csw.bCSWStatus = 0x00;

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
                            set_sense(0x05, 0x24, 0x00, 0);
                            set_error_pointers(7, 1);
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
                            set_sense(0x05, 0x21, 0x00, 0);
                            set_field_pointer(2);

                            if (LBA >= NUM_LBA)
                                first_invalid_lba = LBA;
                            else
                                first_invalid_lba = NUM_LBA;

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

                            set_sense(0, 0, 0, 0);
                            data_to_transfer = CBW.dCBWDataTransferLength;
                            current_csw.dCSWDataResidue = (uint32_t)(total_bytes - sent_bytes);
                            current_csw.bCSWStatus = 0x00;

                            // Data stage
                            uint16_t chunk = (total_bytes > 64) ? 64 : total_bytes;

                            uint8_t *read_addr = (uint8_t *)(uintptr_t)(STORAGE_BASE + (STORAGE_PAGE_FIRST + start_LBA * 2) * PAGE_LENGTH);

                            memcpy(page_cache, read_addr, PAGE_LENGTH);

                            memcpy(EP1_TX_BUF, page_cache, chunk);
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

                        uint8_t page_error = 0;
                        if (pc_pg != 0)
                        {
                            if (subpage_code != 0x00)
                            {
                                set_field_pointer(3);
                                page_error = 1;
                            }

                            if ((pc_pg & 0x3F) != 0x3F)
                            {
                                page_error = 1;
                                set_field_pointer(2);
                            }
                        }
                        // Error on any page code we don't support + exception (Potential conflict with more pages)
                        if (page_error)
                        {
                            set_sense(0x05, 0x24, 0x00, 0);

                            instaCSW.dCSWDataResidue = CBW.dCBWDataTransferLength;
                            instaCSW.bCSWStatus = 0x01;

                            memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                            USBFSD->UEP1_TX_LEN = sizeof(csw);
                            USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                            nice_return;
                        }

                        set_sense(0, 0, 0, 0);
                        data_to_transfer = MIN(sizeof(mode_parameter_header_6), MIN(allocation_len, CBW.dCBWDataTransferLength));
                        // Short transfer!
                        current_csw.dCSWDataResidue = CBW.dCBWDataTransferLength - data_to_transfer;
                        current_csw.bCSWStatus = 0x00;

                        memcpy(EP1_TX_BUF, &mph6, data_to_transfer);
                        USBFSD->UEP1_TX_LEN = data_to_transfer;

                        USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                        // Send CSW ok in next IN
                        nice_return;

                    case PREVENT_ALLOW_MEDIUM_REMOVAL_OP:
                        printf("Prevent allow medium removal\r\n");

                        // No handling for now
                        prevent_medium_removal = CBW.CBWCB[4] & 0b11;

                        set_sense(0, 0, 0, 0);
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
                            set_sense(0x05, 0x24, 0x00, 0);
                            set_error_pointers(7, 1);
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
                            set_sense(0x05, 0x21, 0x00, 0);
                            set_field_pointer(2);

                            if (LBA >= NUM_LBA)
                                first_invalid_lba = LBA;
                            else
                                first_invalid_lba = NUM_LBA;

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

                            set_sense(0, 0, 0, 0);
                            // Changed later only on short transfers
                            current_csw.dCSWDataResidue = (uint32_t)(total_bytes - received_bytes);
                            current_csw.bCSWStatus = 0x00;

                            USBFSD->UEP1_RX_CTRL = ((USBFSD->UEP1_RX_CTRL) & ~0b11) | USBFS_UEP_R_RES_ACK;

                            nice_return;
                        }

                    case START_STOP_UNIT_OP:
                        printf("Start Stop Unit\r\n");
                        uint8_t immed = CBW.CBWCB[1] & 0b1;

                        if (immed)
                        {
                            set_sense(0, 0, 0, 0);
                            instaCSW.dCSWDataResidue = 0;
                            instaCSW.bCSWStatus = 0x00;

                            memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                            USBFSD->UEP1_TX_LEN = sizeof(csw);
                            USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                            nice_return;
                        }

                        uint8_t ssu_flags = CBW.CBWCB[4];

                        printf("SSU FLAGS: %hu\r\n", ssu_flags);

                        // Power condition > 0
                        if ((ssu_flags >> 4) > 0)
                        {
                            /* We don't support changing to o ACTIVE, IDLE, STANDBY or (o FORCE_IDLE_0 or FORCE_STANDBY_0)
                             * So everything is treated as an error
                             */
                            set_sense(0x05, 0x24, 0x00, 0);
                            set_error_pointers(7, 4);
                            instaCSW.dCSWDataResidue = CBW.dCBWDataTransferLength;
                            instaCSW.bCSWStatus = 0x01;

                            memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                            USBFSD->UEP1_TX_LEN = sizeof(csw);
                            USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                            nice_return;
                        }
                        // Process the START and LOEJ bits

                        // Ignore LOEJ because no medium for now

                        // Start
                        if (ssu_flags & 0b1)
                            ; // Active power condition + timers
                        else
                            ; // Stopped power condition + timers
                        set_sense(0, 0, 0, 0);
                        instaCSW.dCSWDataResidue = 0;
                        instaCSW.bCSWStatus = 0x00;

                        memcpy(EP1_TX_BUF, &instaCSW, sizeof(csw));
                        USBFSD->UEP1_TX_LEN = sizeof(csw);
                        USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

                        nice_return;

                    case REQUEST_SENSE_OP:
                        printf("Request Sense\r\n");

                        uint8_t desc = CBW.CBWCB[1] & 0b1;
                        // We don't support descriptor format sense data, so just send fixed sense data
                        if (desc)
                            printf("Unsupported descriptor format sense data!\r\n");

                        fixed_sense_data fsd;

                        const sense *sense = get_sense();

                        // Valid
                        fsd.response_code = 0b10000000;

                        if (sense->deferred)
                            fsd.response_code |= 0x71;
                        else
                            fsd.response_code |= 0x70;

                        // Ignore filemarks - we don't have them, ignore EOM and ILI - no rules to report
                        fsd.flags_key = sense->sense_key;
                        fsd.asc = sense->asc;
                        fsd.ascq = sense->ascq;

                        /* The only commands with any INFORMATION field interaction we support are
                         * READ(10) and WRITE(10), so this is the only situation for writing to this field
                         */
                        if (first_invalid_lba)
                        {
                            uint32_t v = __builtin_bswap32(first_invalid_lba);
                            memcpy(fsd.info, &v, 4);
                            first_invalid_lba = 0;
                        }

                        // No additional sense bytes
                        fsd.add_sense_length = 10;

                        // No commands which need cmd_spec_info supported
                        fsd.cmd_spec_info[0] = 0;
                        fsd.cmd_spec_info[1] = 0;
                        fsd.cmd_spec_info[2] = 0;
                        fsd.cmd_spec_info[3] = 0;

                        // No VPD
                        fsd.f_r_unit_code = 0;

                        switch (sense->sense_key)
                        {
                        // ILLEGAL REQUEST (maybe define them laetr)
                        case 0x05:
                            // SKSV 1 - valid, C/D 1 - no payload error handling for now
                            fsd.sense_key_spec[0] = 0b11000000;

                            // If bit pointer exists BPV 1 - valid
                            if (sense->bpv)
                            {
                                fsd.sense_key_spec[0] |= 0b1000;
                                fsd.sense_key_spec[0] |= sense->bit_pointer;
                            }

                            uint16_t v = sense->field_pointer;
                            memcpy(&fsd.sense_key_spec[1], &v, 2);

                            break;

                        case 0x00:
                            fsd.sense_key_spec[0] = 0b10000000;
                            // Our transactions always finish immediately
                            fsd.sense_key_spec[1] = 0xFF;
                            fsd.sense_key_spec[2] = 0xFF;
                            break;

                        default:
                            // SKSV - 0
                            fsd.sense_key_spec[0] = 0;
                            fsd.sense_key_spec[1] = 0;
                            fsd.sense_key_spec[2] = 0;
                        }

                        current_csw.dCSWDataResidue = 0;
                        current_csw.bCSWStatus = 0x00;

                        memcpy(EP1_TX_BUF, &fsd, sizeof(fixed_sense_data));
                        USBFSD->UEP1_TX_LEN = sizeof(fixed_sense_data);

                        USBFSD->UEP1_TX_CTRL = ((USBFSD->UEP1_TX_CTRL) & ~0b11) | USBFS_UEP_T_RES_ACK;

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

                if (get_before_csw())
                {
                    if (total_bytes == sent_bytes)
                    {
                        set_before_csw(0);
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
                        uint64_t curr_PAGE = start_LBA * 2 + sent_bytes / PAGE_LENGTH;
                        uint16_t off_in_page = sent_bytes % PAGE_LENGTH;

                        if (off_in_page + chunk1 >= PAGE_LENGTH)
                        {
                            chunk2 = off_in_page + chunk1 - PAGE_LENGTH;
                            chunk1 -= chunk2;

                            memcpy(EP1_TX_BUF, page_cache + off_in_page, chunk1);

                            memcpy(page_cache, (uint8_t *)(uintptr_t)(STORAGE_BASE + (STORAGE_PAGE_FIRST + curr_PAGE + 1) * PAGE_LENGTH), PAGE_LENGTH);

                            memcpy(EP1_TX_BUF + chunk1, page_cache, chunk2);
                        }
                        else
                        {
                            memcpy(EP1_TX_BUF, page_cache + off_in_page, chunk1);
                        }

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

    memset(page_cache, 0, PAGE_LENGTH);

    // Nuclear option
    /*
    FLASH_Unlock();

    FLASH_EraseAllPages();
    */

    USBFS_MSC_INIT();

    printf("USBFS device init done\r\n");

    while (1)
    {

        if (tim1_update_flag)
        {
            tim1_update_flag = 0;
            printf(".\r\n");
        }

        if (usb_update_flag)
        {
            usb_update_flag = 0;
            // printf("\r\n");
        }
    }
}