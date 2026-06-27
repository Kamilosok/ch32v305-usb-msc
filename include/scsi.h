/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <inttypes.h>
#include <flash_storage.h>

#define INQUIRY_OP (0x12)
#define TEST_UNIT_READY_OP (0x00)
#define REQUEST_SENSE_OP (0x03)
#define READ_CAPACITY_10_OP (0x25)
#define READ_10_OP (0x28)
#define MODE_SENSE_6_OP (0x1A)
#define PREVENT_ALLOW_MEDIUM_REMOVAL_OP (0x1E)
#define WRITE_10_OP (0x2A)
#define START_STOP_UNIT_OP (0x1B)

typedef struct
{
    uint8_t sense_key;
    uint8_t asc;  // Additional Sense Code
    uint8_t ascq; // Additional Sense Code Qualifier
    uint8_t deferred;
    uint8_t bpv;
    uint8_t bit_pointer;
    uint16_t field_pointer;
} sense;

typedef struct __attribute__((packed))
{
    uint8_t response_code;
    uint8_t obsolete;
    uint8_t flags_key;
    uint8_t info[4];
    uint8_t add_sense_length;
    uint8_t cmd_spec_info[4];
    uint8_t asc;
    uint8_t ascq;
    uint8_t f_r_unit_code;
    uint8_t sense_key_spec[3];
} fixed_sense_data;

typedef struct __attribute__((packed))
{
    uint8_t peripheral;
    uint8_t rmb;
    uint8_t version;
    uint8_t resp_data_format;
    uint8_t additional_length;
    uint8_t flags1;
    uint8_t flags2;
    uint8_t flags3;
    uint8_t vendor[8];
    uint8_t product[16];
    uint8_t revision[4];
} inquiry_data;

typedef struct __attribute__((packed))
{
    uint32_t ret_lba;
    uint32_t block_length;
} read_capacity_data;

typedef struct __attribute__((packed))
{
    uint8_t mode_data_length;
    uint8_t medium_type;
    uint8_t device_specific_parameter;
    uint8_t block_descriptor_length;
} mode_parameter_header_6;

uint8_t command_supported(uint8_t command_op);

void set_sense(uint8_t key, uint8_t asc, uint8_t ascq, uint8_t deferred);

void set_error_pointers(uint8_t bit_pointer, uint16_t byte_pointer);

void set_field_pointer(uint16_t byte_pointer);

const sense *get_sense(void);

const inquiry_data *get_inquiry_data(void);

const read_capacity_data *get_read_capacity_data(void);

const mode_parameter_header_6 *get_mode_parameter_header_6(void);

// TODO: Add more of the SCSI logic here
