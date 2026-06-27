/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#include <msc.h>

#include <scsi.h>
static csw current_csw;

static uint8_t before_csw = 0;
static uint32_t cbw_tag = 0;

static const uint8_t max_lun = 0;

void msc_init(void)
{
    current_csw.dCSWSignature = CSWSignature;
}

void set_csw(uint32_t residue, uint8_t status)
{
    current_csw.dCSWDataResidue = residue;
    if (status <= CSW_STATUS_PHASE_ERROR)
        current_csw.bCSWStatus = status;

    // else some error
}

// Change to only affect CSW
void set_csw_tag(uint32_t tag)
{
    cbw_tag = tag;
    current_csw.dCSWTag = tag;
}

const csw *get_csw(void)
{
    return (const csw *)&current_csw;
}

uint8_t get_cbw_tag(void)
{
    return cbw_tag;
}

void set_before_csw(uint8_t val)
{
    before_csw = val;
}

uint8_t get_before_csw(void)
{
    return before_csw;
}

// out_field specifies which byte was erroneous
uint8_t validCBW(cbw *CBW)
{
    if (CBW->dCBWSignature != CBWSignature || USBFSD->RX_LEN != sizeof(cbw) || before_csw)
        return 0;
    return 1;
}

uint8_t meaningfulCBW(cbw *CBW)
{
    if (((CBW->bCBWLUN & 0xF0) != 0) || ((CBW->bCBWCBLength & 0xE0) != 0) || CBW->bCBWLUN != 0)
        return 0;

    return 1;
}

const uint8_t *get_max_LUN(void)
{
    return (const uint8_t *)&max_lun;
}
