/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <debug.h>

#include <usb_descriptors.h>
#include <usb_hal.h>
#include <msc.h>
#include <scsi.h>
#include <flash_storage.h>

// 'DEBUG' was taken
#ifndef DO_DEBUG
#define printf(...)
#endif

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

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

// Temporary, change enumeration to a state machine
uint8_t change_back = 0;

uint32_t first_invalid_lba = 0;

static volatile uint8_t usb_update_flag = 0;

[[gnu::interrupt]]
void USBFS_IRQHandler(void)
{
    usb_update_flag = 1;

    uint8_t intflag, stflag;

    intflag = USBFSD->INT_FG;
    stflag = USBFSD->INT_ST;

    uint8_t endp = stflag & USBFS_UIS_ENDP_MASK;

    if (change_back)
    {
        usb_set_tx_ep_res(0, USBFS_UEP_T_RES_NAK);
        usb_set_rx_ep_res(0, USBFS_UEP_R_RES_ACK);
        change_back = 0;
    }

    if (intflag & USBFS_UIF_TRANSFER)
    {
        if (endp == 0 || !configured)
        {
            // printf("EP0\r\n");
            setup_packet setup_p;
            // Data in this packet is little-endian, so we can simply copy
            memcpy(&setup_p, usb_get_rx_buf(0), sizeof(setup_packet));

            if ((stflag & USBFS_UIS_TOKEN_MASK) == USBFS_UIS_TOKEN_SETUP)
            {
                if (setup_p.bmRequestType == 0x80 && setup_p.bRequest == USB_GET_DESCRIPTOR && (setup_p.wValue >> 8) == USB_DESCR_TYP_DEVICE)
                {
                    usb_tx_data_ep_res(get_device_descriptor(), MIN(sizeof(device_descriptor), setup_p.wLength), 0, USBFS_UEP_T_RES_ACK);

                    printf("Device descriptor\r\n");
                }
                else if (setup_p.bmRequestType == 0x00 && setup_p.bRequest == USB_SET_ADDRESS)
                {
                    // Set AFTER setup stage
                    dev_addr = setup_p.wValue;
                    usb_tx_data_ep_res(NULL, 0, 0, USBFS_UEP_T_RES_ACK);
                    addr_stage = 1;
                    printf("Address setting\r\n");
                }
                else if (setup_p.bmRequestType == 0x80 && setup_p.bRequest == USB_GET_DESCRIPTOR && (setup_p.wValue >> 8) == USB_DESCR_TYP_QUALIF)
                {
                    // Full-Speed device must respond with a request error
                    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_STALL;
                    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_STALL;
                    change_back = 1;
                    printf("Qualifier\r\n");
                }
                else if (setup_p.bmRequestType == 0x80 && setup_p.bRequest == USB_GET_DESCRIPTOR && (setup_p.wValue >> 8) == USB_DESCR_TYP_CONFIG)
                {
                    usb_tx_data_ep_res(get_msc_descriptor_tree(), MIN(sizeof(msc_descriptor_tree), setup_p.wLength), 0, USBFS_UEP_T_RES_ACK);

                    config_stage = 1;
                    printf("%u CONFIG/TREE\r\n", send_len);
                }
                else if (setup_p.bmRequestType == 0x00 && setup_p.bRequest == USB_SET_CONFIGURATION)
                {
                    // We only have 1 configuration, so we ignore wValue
                    uint8_t config_value = setup_p.wValue;
                    configured = 1;

                    usb_tx_data_ep_res(NULL, 0, 0, USBFS_UEP_T_RES_ACK);

                    printf("%u SET CONFIG\r\n", config_value);
                }
                else if (setup_p.bmRequestType == 0xa1 && setup_p.bRequest == USB_GET_MAX_LUN)
                {
                    usb_tx_data_ep_res(get_max_LUN(), 1, 0, USBFS_UEP_T_RES_ACK);

                    printf("GET MAX LUN\r\n");
                }
                else
                {
                    printf("BAD\r\n");
                    printf("bmRequest %x bRequest %x wValue %x\r\n", setup_p.bmRequestType, setup_p.bRequest, setup_p.wValue);
                }
            }
            else if ((stflag & USBFS_UIS_TOKEN_MASK) == USBFS_UIS_TOKEN_IN)
            {
                if (addr_stage)
                {
                    addr_stage = 0;
                    USBFSD->DEV_ADDR = dev_addr;
                    usb_set_tx_ep_res(0, USBFS_UEP_T_RES_ACK);
                }
                else if (config_stage)
                {
                    config_stage = 0;
                    usb_set_tx_ep_res(0, USBFS_UEP_T_RES_ACK);
                    usb_set_rx_ep_res(0, USBFS_UEP_R_RES_ACK);
                }
                // printf("IN\r\n");
            }
            else if ((stflag & USBFS_UIS_TOKEN_MASK) == USBFS_UIS_TOKEN_OUT)
            {
                usb_set_rx_ep_res(0, USBFS_UEP_R_RES_ACK);
                // printf("OUT\r\n");
            }
        }
        else if (endp == 1)
        {
            // printf("EP1\r\n");
            if ((stflag & USBFS_UIS_TOKEN_MASK) == USBFS_UIS_TOKEN_OUT)
            {
                // printf("OUT\r\n");
                if (write_stage)
                {
                    uint16_t chunk = (total_bytes - received_bytes > EP1_IN_BUF_SIZE) ? EP1_IN_BUF_SIZE : (total_bytes - received_bytes);
                    uint32_t store_addr = start_LBA * LBA_LENGTH + received_bytes;

                    store_data(store_addr, usb_get_rx_buf(1), chunk);

                    received_bytes += chunk;

                    set_csw(total_bytes - received_bytes, CSW_STATUS_OK);

                    if (total_bytes == received_bytes)
                    {
                        write_stage = 0;
                        set_before_csw(0);
                        total_bytes = 0;
                        received_bytes = 0;

                        usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);
                    }
                    else
                    {
                        usb_set_rx_ep_res(1, USBFS_UEP_R_RES_ACK);
                    }

                    IRQ_return(USBFS_IRQn);
                }
                else // Proper BOT
                {
                    cbw CBW;
                    memcpy(&CBW, usb_get_rx_buf(1), sizeof(cbw));

                    if (!validCBW(&CBW))
                    {
                        printf("Invalid CBW\r\n");
                        // USBFSD->UEP1_TX_LEN = 0; // TODO: Remove?
                        //   The device shall STALL the Bulk-In pipe. Also, the device shall either STALL the Bulk-Out pipe or ... So we stall both
                        usb_set_tx_ep_res(1, USBFS_UEP_T_RES_STALL);
                        usb_set_rx_ep_res(1, USBFS_UEP_R_RES_STALL);

                        IRQ_return(USBFS_IRQn);
                    }

                    set_csw_tag(CBW.dCBWTag);

                    if (!meaningfulCBW(&CBW))
                    {
                        printf("Unmeaningful CBW\r\n");

                        // The response of a device to a CBW that is not meaningful is not specified
                        usb_set_tx_ep_res(1, USBFS_UEP_T_RES_STALL);
                        usb_set_rx_ep_res(1, USBFS_UEP_R_RES_STALL);
                    }

                    if (!command_supported(CBW.CBWCB[0]))
                    {
                        printf("Unsupported command: %02x\r\n", CBW.CBWCB[0]);

                        set_sense(0x05, 0x20, 0x00, 0);
                        set_field_pointer(0);

                        set_csw(CBW.dCBWDataTransferLength, CSW_STATUS_FAILED);

                        usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                        IRQ_return(USBFS_IRQn);
                    }

                    set_before_csw(1);

                    // Used in multiple commands, declared before the switch
                    uint64_t LBA;
                    uint16_t data_to_transfer, alloc_len, transfer_len;
                    uint8_t flags, control, opcode;

                    control = CBW.CBWCB[CBW.bCBWCBLength - 1];
                    uint8_t c_err = 0;

                    // No support for NACA
                    if ((control >> 2) & 0b1)
                    {
                        c_err = 1;
                        set_error_pointers(2, CBW.bCBWCBLength - 1);
                    }

                    // No support for LINK
                    if (control & 0b1)
                    {
                        c_err = 1;
                        set_error_pointers(0, CBW.bCBWCBLength - 1);
                    }

                    if (c_err)
                    {
                        printf("CONTROL error\r\n");

                        set_sense(0x05, 0x24, 0x00, 0);

                        set_csw(CBW.dCBWDataTransferLength, CSW_STATUS_FAILED);

                        usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                        IRQ_return(USBFS_IRQn);
                    }

                    opcode = CBW.CBWCB[0];
                    switch (opcode)
                    {
                    case INQUIRY_OP:
                        printf("Inquiry\r\n");
                        uint8_t evpd = CBW.CBWCB[1] & 0x01;
                        uint8_t page_code = CBW.CBWCB[2];
                        // Min size of response
                        alloc_len = MAX(5, ((CBW.CBWCB[3] << 8) | CBW.CBWCB[4]));

                        // No support for Vital Product Data
                        if (evpd != 0)
                        {
                            printf("EVPD\r\n");
                            set_sense(0x05, 0x24, 0x00, 0);
                            set_error_pointers(0, 1);

                            set_csw(CBW.dCBWDataTransferLength, CSW_STATUS_FAILED);

                            usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                            IRQ_return(USBFS_IRQn);
                        }

                        if (page_code != 0)
                        {
                            printf("PC\r\n");
                            set_sense(0x05, 0x24, 0x00, 0);
                            set_field_pointer(2);

                            set_csw(CBW.dCBWDataTransferLength, CSW_STATUS_FAILED);

                            usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                            IRQ_return(USBFS_IRQn);
                        }

                        // If data_to_transfer > uint16_max 4.3.4.6

                        set_sense(0, 0, 0, 0);

                        data_to_transfer = MIN(CBW.dCBWDataTransferLength, MIN(alloc_len, sizeof(inquiry_data)));
                        // Responses like this should have the residue as 0
                        set_csw(CBW.dCBWDataTransferLength - data_to_transfer, CSW_STATUS_OK);

                        usb_tx_data_ep_res(get_inquiry_data(), data_to_transfer, 1, USBFS_UEP_T_RES_ACK);

                        // Send CSW ok in next IN
                        IRQ_return(USBFS_IRQn);

                    case TEST_UNIT_READY_OP:
                        // printf("Test Unit Ready\r\n");

                        // Transfer should be 0, when a storage medium is added respond accordingly
                        set_csw(CBW.dCBWDataTransferLength, CSW_STATUS_OK);

                        usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                        IRQ_return(USBFS_IRQn);

                    case READ_CAPACITY_10_OP:
                        printf("Read Capacity (10)\r\n");

                        LBA = CBW.CBWCB[2] << 24 | CBW.CBWCB[3] << 16 | CBW.CBWCB[4] << 8 | CBW.CBWCB[5];
                        uint8_t pmi = CBW.CBWCB[8] & 0x01;

                        // No pmi=1 handling for now
                        if (pmi != 0 || LBA != 0)
                        {
                            set_sense(0x05, 0x24, 0x00, 0);
                            set_field_pointer(2);

                            set_csw(CBW.dCBWDataTransferLength, CSW_STATUS_FAILED);

                            usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                            IRQ_return(USBFS_IRQn);
                        }

                        set_sense(0, 0, 0, 0);
                        data_to_transfer = CBW.dCBWDataTransferLength;

                        set_csw(CBW.dCBWDataTransferLength - data_to_transfer, CSW_STATUS_OK);

                        usb_tx_data_ep_res(get_read_capacity_data(), data_to_transfer, 1, USBFS_UEP_T_RES_ACK);

                        // Send CSW ok in next IN
                        IRQ_return(USBFS_IRQn);

                    case READ_10_OP:
                        printf("Read (10)\r\n");
                        flags = CBW.CBWCB[1];
                        LBA = (CBW.CBWCB[2] << 24) | (CBW.CBWCB[3] << 16) | (CBW.CBWCB[4] << 8) | (CBW.CBWCB[5]);
                        __attribute__((unused)) uint8_t group_number = CBW.CBWCB[6] & 0b00011111;
                        transfer_len = CBW.CBWCB[7] << 8 | CBW.CBWCB[8];

                        // Any protection code except 000 we don't support
                        if (flags & 0b11100000)
                        {
                            set_sense(0x05, 0x24, 0x00, 0);
                            set_error_pointers(7, 1);

                            set_csw(CBW.dCBWDataTransferLength, CSW_STATUS_FAILED);

                            usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                            IRQ_return(USBFS_IRQn);
                        }

                        // Ignore DPO because no caching
                        // Ignore FUA and FUA_NV because no specific storage mediums for now

                        if (LBA + transfer_len > NUM_LBA)
                        {
                            set_sense(0x05, 0x21, 0x00, 0);
                            set_field_pointer(2);

                            if (LBA >= NUM_LBA)
                                first_invalid_lba = LBA;
                            else
                                first_invalid_lba = NUM_LBA;

                            set_csw(CBW.dCBWDataTransferLength, CSW_STATUS_FAILED);

                            usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                            IRQ_return(USBFS_IRQn);
                        }

                        // Ignore group number

                        if (transfer_len == 0)
                        {
                            set_csw(0, CSW_STATUS_OK);

                            usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                            IRQ_return(USBFS_IRQn);
                        }
                        else
                        {
                            start_LBA = LBA;
                            total_bytes = transfer_len * LBA_LENGTH;
                            sent_bytes = 0;

                            set_sense(0, 0, 0, 0);
                            data_to_transfer = CBW.dCBWDataTransferLength;

                            set_csw((uint32_t)(total_bytes - sent_bytes), CSW_STATUS_OK);

                            // Data stage
                            uint16_t chunk = (total_bytes > 64) ? 64 : total_bytes;

                            uint8_t *read_addr = (uint8_t *)(uintptr_t)(STORAGE_BASE + (STORAGE_PAGE_FIRST + start_LBA * 2) * FLASH_PAGE_SIZE);

                            memcpy(get_page_cache(), read_addr, FLASH_PAGE_SIZE);

                            usb_tx_data_ep_res(get_page_cache(), chunk, 1, USBFS_UEP_T_RES_ACK);

                            sent_bytes += chunk;

                            IRQ_return(USBFS_IRQn);
                        }

                    case MODE_SENSE_6_OP:
                        printf("Mode sense (6)\r\n");
                        __attribute__((unused)) uint8_t dbd = CBW.CBWCB[1] & 0b00001000;
                        uint8_t pc_pg = CBW.CBWCB[2];
                        uint8_t subpage_code = CBW.CBWCB[3];
                        alloc_len = CBW.CBWCB[4];

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

                            set_csw(CBW.dCBWDataTransferLength, CSW_STATUS_FAILED);

                            usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                            IRQ_return(USBFS_IRQn);
                        }

                        set_sense(0, 0, 0, 0);
                        data_to_transfer = MIN(CBW.dCBWDataTransferLength, MIN(alloc_len, sizeof(mode_parameter_header_6)));

                        // Intentional short transfer
                        set_csw(CBW.dCBWDataTransferLength - data_to_transfer, CSW_STATUS_OK);

                        usb_tx_data_ep_res(get_mode_parameter_header_6(), data_to_transfer, 1, USBFS_UEP_T_RES_ACK);

                        // Send CSW ok in next IN
                        IRQ_return(USBFS_IRQn);

                    case PREVENT_ALLOW_MEDIUM_REMOVAL_OP:
                        printf("Prevent allow medium removal\r\n");

                        // No handling for now
                        prevent_medium_removal = CBW.CBWCB[4] & 0b11;

                        set_sense(0, 0, 0, 0);
                        set_csw(0, CSW_STATUS_OK);

                        usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                        IRQ_return(USBFS_IRQn);

                    case WRITE_10_OP:
                        printf("Write (10)\r\n");
                        flags = CBW.CBWCB[1];
                        LBA = (CBW.CBWCB[2] << 24) | (CBW.CBWCB[3] << 16) | (CBW.CBWCB[4] << 8) | (CBW.CBWCB[5]);
                        __attribute__((unused)) uint8_t group_number_w = CBW.CBWCB[6] & 0b00011111;
                        transfer_len = (CBW.CBWCB[7] << 8) | (CBW.CBWCB[8]);

                        // Any protection code except 000 we don't support
                        if (flags & 0b11100000)
                        {
                            set_sense(0x05, 0x24, 0x00, 0);
                            set_error_pointers(7, 1);

                            set_csw(CBW.dCBWDataTransferLength, CSW_STATUS_FAILED);

                            usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                            IRQ_return(USBFS_IRQn);
                        }

                        // Ignore DPO because no caching
                        // Ignore FUA and FUA_NV because no specific storage mediums for now

                        if (LBA + transfer_len > NUM_LBA)
                        {
                            set_sense(0x05, 0x21, 0x00, 0);
                            set_field_pointer(2);

                            if (LBA >= NUM_LBA)
                                first_invalid_lba = LBA;
                            else
                                first_invalid_lba = NUM_LBA;

                            set_csw(CBW.dCBWDataTransferLength, CSW_STATUS_FAILED);

                            usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                            IRQ_return(USBFS_IRQn);
                        }

                        // Ignore group number

                        if (transfer_len == 0)
                        {
                            set_csw(0, CSW_STATUS_OK);

                            usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                            IRQ_return(USBFS_IRQn);
                        }
                        else
                        {
                            write_stage = 1;
                            start_LBA = LBA;
                            total_bytes = transfer_len * LBA_LENGTH;
                            received_bytes = 0;

                            set_sense(0, 0, 0, 0);

                            // Intentional short transfer
                            set_csw((uint32_t)(total_bytes - received_bytes), CSW_STATUS_OK);

                            usb_set_rx_ep_res(1, USBFS_UEP_R_RES_ACK);

                            IRQ_return(USBFS_IRQn);
                        }

                    case START_STOP_UNIT_OP:
                        printf("Start Stop Unit\r\n");
                        uint8_t immed = CBW.CBWCB[1] & 0b1;

                        if (immed)
                        {
                            set_sense(0, 0, 0, 0);
                            set_csw(0, CSW_STATUS_OK);

                            usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                            IRQ_return(USBFS_IRQn);
                        }

                        // ssu_flags
                        flags = CBW.CBWCB[4];

                        printf("SSU FLAGS: %hu\r\n", flags);

                        // Power condition > 0
                        if ((flags >> 4) > 0)
                        {
                            /* We don't support changing to o ACTIVE, IDLE, STANDBY or (o FORCE_IDLE_0 or FORCE_STANDBY_0)
                             * So everything is treated as an error
                             */
                            set_sense(0x05, 0x24, 0x00, 0);
                            set_error_pointers(7, 4);
                            set_csw(CBW.dCBWDataTransferLength, CSW_STATUS_FAILED);

                            usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                            IRQ_return(USBFS_IRQn);
                        }
                        // Process the START and LOEJ bits

                        // Ignore LOEJ because no medium for now

                        // Start
                        if (flags & 0b1)
                            ; // Active power condition + timers
                        else
                            ; // Stopped power condition + timers
                        set_sense(0, 0, 0, 0);

                        set_csw(0, CSW_STATUS_OK);

                        usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                        IRQ_return(USBFS_IRQn);

                    case READ_FORMAT_CAPACITIES:
                        printf("Read format capacities\r\n");
                        uint8_t lun = CBW.CBWCB[1] >> 4;

                        alloc_len = (CBW.CBWCB[7] << 8) + CBW.CBWCB[8];

                        if (lun > 0)
                        {
                            set_sense(0x05, 0x24, 0x00, 0);

                            set_csw(CBW.dCBWDataTransferLength, CSW_STATUS_FAILED);

                            usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                            IRQ_return(USBFS_IRQn);
                        }

                        set_sense(0, 0, 0, 0);

                        data_to_transfer = MIN(CBW.dCBWDataTransferLength, MIN(alloc_len, sizeof(capacity_list_header) + sizeof(maximum_capacity_header)));

                        set_csw(0, CSW_STATUS_OK);

                        memcpy(usb_get_tx_buf(1), get_capacity_list_header(), sizeof(capacity_list_header));

                        memcpy(usb_get_tx_buf(1) + sizeof(capacity_list_header), get_maximum_capacity_header(), sizeof(maximum_capacity_header));

                        // Data manipulation done earlier, maybe this should be done some other way...
                        usb_tx_data_ep_res(NULL, data_to_transfer, 1, USBFS_UEP_T_RES_ACK);

                        IRQ_return(USBFS_IRQn);

                    case REQUEST_SENSE_OP:
                        printf("Request Sense\r\n");

                        uint8_t desc = CBW.CBWCB[1] & 0b1;
                        alloc_len = CBW.CBWCB[4];
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
                        // ILLEGAL REQUEST (maybe define them later)
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

                        data_to_transfer = MIN(CBW.dCBWDataTransferLength, MIN(alloc_len, sizeof(fixed_sense_data)));

                        set_csw(CBW.dCBWDataTransferLength - data_to_transfer, CSW_STATUS_OK);

                        usb_tx_data_ep_res(&fsd, data_to_transfer, 1, USBFS_UEP_T_RES_ACK);

                        set_sense(0, 0, 0, 0);

                        IRQ_return(USBFS_IRQn);

                    default:
                        printf("Unsupported opcode: %02X\r\n", opcode);

                        IRQ_return(USBFS_IRQn);
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

                        usb_tx_data_ep_res(get_csw(), sizeof(csw), 1, USBFS_UEP_T_RES_ACK);

                        IRQ_return(USBFS_IRQn);
                    }
                    else
                    {
                        uint16_t chunk = (total_bytes - sent_bytes > EP1_OUT_BUF_SIZE) ? EP1_OUT_BUF_SIZE : (total_bytes - sent_bytes);

                        uint32_t retrieve_addr = start_LBA * LBA_LENGTH + sent_bytes;

                        retrieve_data(usb_get_tx_buf(1), retrieve_addr, chunk);

                        sent_bytes += chunk;
                        set_csw((uint32_t)(total_bytes - sent_bytes), CSW_STATUS_OK);

                        // Data manipulation done earlier
                        usb_tx_data_ep_res(NULL, chunk, 1, USBFS_UEP_T_RES_ACK);

                        IRQ_return(USBFS_IRQn);
                    }
                }

                usb_set_tx_ep_res(1, USBFS_UEP_T_RES_NAK);
            }
        }
    }

    IRQ_return(USBFS_IRQn);
}

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);
    msc_init();
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("SystemClk:%lu\r\n", SystemCoreClock);
    printf("ChipID:%08lu\r\n", DBGMCU_GetCHIPID());

    // Interrupt every 1s
    TIM1_INT_Init(3000 - 1, 48000 - 1);
    printf("TIM1 initialized\r\n");

    USBFS_RCC_Init();

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