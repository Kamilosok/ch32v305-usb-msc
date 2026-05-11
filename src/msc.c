/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#include <msc.h>

#include <scsi.h>
#include <ch32v30x.h>

uint8_t before_csw = 0;

// Maybe inline?
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

    // Check if the command is supported
    switch (CBW->CBWCB[0])
    {
    case INQUIRY_OP:
    case TEST_UNIT_READY_OP:
    case REQUEST_SENSE_OP:
    case READ_CAPACITY_10_OP:
    case READ_10_OP:
    case MODE_SENSE_6_OP:
    case PREVENT_ALLOW_MEDIUM_REMOVAL_OP:
    case WRITE_10_OP:
    case START_STOP_UNIT_OP:
        return 1;

    default:
        return 0;
    }
}
