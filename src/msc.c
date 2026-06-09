/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#include <msc.h>

#include <scsi.h>
#include <ch32v30x.h>

uint8_t before_csw = 0;
const uint8_t max_lun = 0;

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

const uint8_t *get_max_LUN()
{
    return (const uint8_t *)&max_lun;
}
