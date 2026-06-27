/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <inttypes.h>
#include <ch32v30x.h>
#include <string.h>

#define CBWSignature (0x43425355)
#define CSWSignature (0x53425355)

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

typedef enum
{
    CSW_STATUS_OK = 0x00,
    CSW_STATUS_FAILED = 0x01,
    CSW_STATUS_PHASE_ERROR = 0x02,
} csw_status_t;

// TODO: Full bot logic here

void msc_init();

void set_csw(uint32_t residue, uint8_t status);

void set_csw_tag(uint32_t tag);

const csw *get_csw(void);

uint8_t get_cbw_tag(void);

void set_before_csw(uint8_t val);

uint8_t get_before_csw(void);

uint8_t validCBW(cbw *CBW);

uint8_t meaningfulCBW(cbw *CBW);

const uint8_t *get_max_LUN();