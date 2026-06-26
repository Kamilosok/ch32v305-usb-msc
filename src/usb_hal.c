/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#include <usb_hal.h>

static __attribute__((aligned(4))) uint8_t ep0_buf[EP0_BUF_SIZE];
static __attribute__((aligned(4))) uint8_t ep1_buf[EP1_IN_BUF_SIZE + EP1_OUT_BUF_SIZE];

static uint8_t *const ep_rx_buffers[] = {
    ep0_buf,
    ep1_buf,
};

static uint8_t *const ep_tx_buffers[] = {
    ep0_buf,
    ep1_buf + EP1_IN_BUF_SIZE,
};

uint8_t *usb_get_rx_buf(uint8_t ep)
{
    if (ep <= LAST_EP)
    {
        return ep_rx_buffers[ep];
    }
    else
        return NULL;
}

uint8_t *usb_get_tx_buf(uint8_t ep)
{
    if (ep <= LAST_EP)
    {
        return ep_tx_buffers[ep];
    }
    else
        return NULL;
}

void usb_set_tx_ep_res(uint8_t ep, uint8_t res)
{
    if (ep <= LAST_EP)
    {
        // The USBOTG_FS Control Registers are packed as 4-byte sequences
        __IO uint8_t *ctrl_reg = (__IO uint8_t *)((uint32_t)&USBFSD->UEP0_TX_CTRL + ep * 4);

        if (res <= USBFS_UEP_T_RES_STALL)
        {
            *ctrl_reg = (*ctrl_reg & ~USBFS_UEP_T_RES_MASK) | res;
        }
    }
}

void usb_set_rx_ep_res(uint8_t ep, uint8_t res)
{
    if (ep <= LAST_EP)
    {
        // The USBOTG_FS Control Registers are packed as 4-byte sequences
        __IO uint8_t *ctrl_reg = (__IO uint8_t *)((uint32_t)&USBFSD->UEP0_RX_CTRL + ep * 4);

        if (res <= USBFS_UEP_R_RES_STALL)
        {
            *ctrl_reg = (*ctrl_reg & ~USBFS_UEP_R_RES_MASK) | res;
        }
    }
}

void usb_tx_data_ep_res(const void *data, size_t data_size, uint8_t ep, uint8_t res)
{
    if (ep <= LAST_EP)
    {
        __IO uint8_t *len_reg = (__IO uint8_t *)((uint32_t)&USBFSD->UEP0_TX_LEN + ep * 4);
        *len_reg = data_size;
    }

    if (data != NULL)
    {
        memcpy(usb_get_tx_buf(ep), data, data_size);
    }

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

void TIM1_INT_Init(uint16_t arr, uint16_t psc)
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

void erase_FLASH()
{
    FLASH_Unlock();

    FLASH_EraseAllPages();
}