/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#include <msc.h>

#include <scsi.h>
static csw current_csw;

static msc_bot_state_t msc_state = BOT_STATE_IDLE;
static uint32_t cbw_tag = 0;

static const uint8_t max_lun = 0;

void msc_init(void)
{
    current_csw.dCSWSignature = CSWSignature;
}

void set_csw(uint32_t residue, uint8_t status)
{
    current_csw.dCSWDataResidue = residue;
    if (status < CSW_STATUS_LAST)
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

// out_field specifies which byte was erroneous
uint8_t validCBW(cbw *CBW)
{
    if (CBW->dCBWSignature != CBWSignature || USBFSD->RX_LEN != sizeof(cbw) || msc_state != BOT_STATE_IDLE)
    {
        return 0;
    }
    return 1;
}

uint8_t meaningfulCBW(cbw *CBW)
{
    if (((CBW->bCBWLUN & 0xF0) != 0) || ((CBW->bCBWCBLength & 0xE0) != 0) || CBW->bCBWLUN != 0)
    {
        return 0;
    }

    return 1;
}

const uint8_t *get_max_LUN(void)
{
    return (const uint8_t *)&max_lun;
}

uint8_t get_msc_state(void)
{
    return msc_state;
}

void set_msc_state(uint8_t state)
{
    if (state < BOT_STATE_LAST)
    {
        msc_state = state;
    }
}
