/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#include <scsi.h>

static sense current_sense;

void set_sense(uint8_t key, uint8_t asc, uint8_t ascq)
{
    current_sense.sense_key = key;
    current_sense.asc = asc;
    current_sense.ascq = ascq;
}

const sense *get_sense(void)
{
    return &current_sense;
}
