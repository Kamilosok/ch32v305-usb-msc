/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <inttypes.h>
#include <stdio.h>
#include <debug.h>
#include <string.h>
#include <usb_descriptors.h>

uint8_t *usb_get_rx_buf(uint8_t ep);

uint8_t *usb_get_tx_buf(uint8_t ep);

void usb_set_tx_ep_res(uint8_t ep, uint8_t res);

void usb_set_rx_ep_res(uint8_t ep, uint8_t res);

void usb_tx_data_ep_res(const void *data, size_t data_size, uint8_t ep, uint8_t res);

// These three functions are based on their counterparts from the EVT
void USBFS_MSC_INIT(void);
void USBFS_RCC_Init(void);
void TIM1_INT_Init(uint16_t arr, uint16_t psc);

// This function erases both the FLASH memory backend AND program instructions, softlocking the unit
void erase_FLASH();