/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#include <scsi.h>

static sense current_sense;

uint8_t command_supported(uint8_t command_op)
{
    switch (command_op)
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

void set_sense(uint8_t key, uint8_t asc, uint8_t ascq, uint8_t deferred)
{
    current_sense.sense_key = key;
    current_sense.asc = asc;
    current_sense.ascq = ascq;
    current_sense.deferred = deferred ? 1 : 0;
}

void set_error_pointers(uint8_t bit_pointer, uint16_t byte_pointer)
{

    current_sense.bit_pointer = bit_pointer & 0b111;
    current_sense.bpv = 1;
    current_sense.field_pointer = __builtin_bswap16(byte_pointer);
}

void set_field_pointer(uint16_t byte_pointer)
{
    current_sense.bpv = 0;
    current_sense.field_pointer = __builtin_bswap16(byte_pointer);
}

const sense *get_sense(void)
{
    return &current_sense;
}
