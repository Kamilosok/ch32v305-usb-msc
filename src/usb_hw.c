/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#include <usb_hw.h>

static __attribute__((aligned(4))) uint8_t ep0_buf[64];
static __attribute__((aligned(4))) uint8_t ep1_buf[128];

static uint8_t *const ep_rx_buffers[] = {
    ep0_buf,
    ep1_buf,
};

static uint8_t *const ep_tx_buffers[] = {
    ep0_buf,
    ep1_buf + 64,
};

uint8_t *usb_get_rx_buf(uint8_t ep)
{
    return ep_rx_buffers[ep];
}

uint8_t *usb_get_tx_buf(uint8_t ep)
{
    return ep_tx_buffers[ep];
}

void usb_set_tx_ep_res(uint8_t ep, uint8_t res)
{
    // The USBOTG_FS Control Registers are packed as 4-byte sequences
    __IO uint8_t *ctrl_reg = (__IO uint8_t *)((uint32_t)&USBFSD->UEP0_TX_CTRL + ep * 4);

    if (res <= USBFS_UEP_T_RES_STALL)
        *ctrl_reg = (*ctrl_reg & ~USBFS_UEP_T_RES_MASK) | res;
}

void usb_set_rx_ep_res(uint8_t ep, uint8_t res)
{
    // The USBOTG_FS Control Registers are packed as 4-byte sequences
    __IO uint8_t *ctrl_reg = (__IO uint8_t *)((uint32_t)&USBFSD->UEP0_RX_CTRL + ep * 4);

    if (res <= USBFS_UEP_R_RES_STALL)
        *ctrl_reg = (*ctrl_reg & ~USBFS_UEP_R_RES_MASK) | res;
}

void usb_tx_data_ep_res(const void *data, size_t data_size, uint8_t ep, uint8_t res)
{
    __IO uint8_t *len_reg = (__IO uint8_t *)((uint32_t)&USBFSD->UEP0_TX_LEN + ep * 4);

    if (data != NULL)
        memcpy(usb_get_tx_buf(ep), data, data_size);
    *len_reg = data_size;
    usb_set_tx_ep_res(ep, res);
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