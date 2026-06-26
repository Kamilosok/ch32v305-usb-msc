/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#include <scsi.h>

static const inquiry_data iq = {
    .peripheral = 0x00,
    .rmb = 0x80,

    .version = (1 << 7), // Compliant with SPC-3, but we don't support a lot of it

    .resp_data_format = 0x02,
    .additional_length = 31,

    .flags1 = 0,
    .flags2 = 0, // Maybe look into QUE and CMDQUE
    .flags3 = 0,

    .vendor = "NONE    ",          // 8
    .product = "RISC-V SOC MSC  ", // 16
    .revision = "0.01",            // 4
};

static const read_capacity_data rcd = {
    // Big endian nums
    .ret_lba = __builtin_bswap32(NUM_LBA - 1),
    .block_length = __builtin_bswap32(LBA_LENGTH),
};

static const mode_parameter_header_6 mph6 = {
    // Only the header
    .mode_data_length = sizeof(mode_parameter_header_6) - 1,
    // Data medium?
    .medium_type = 0x00,
    .device_specific_parameter = 0x00,
    // No blocks
    .block_descriptor_length = 0,
};

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

const inquiry_data *get_inquiry_data(void)
{
    return &iq;
}

const read_capacity_data *get_read_capacity_data(void)
{
    return &rcd;
}

const mode_parameter_header_6 *get_mode_parameter_header_6(void)
{
    return &mph6;
}
