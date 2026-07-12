/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <inttypes.h>
#include <string.h>
#include <usb_defs.h>

#define LAST_EP (1)
#define EP0_BUF_SIZE (64)
#define EP1_IN_BUF_SIZE (64)
#define EP1_OUT_BUF_SIZE (64)

const device_descriptor *get_device_descriptor(void);

const config_descriptor *get_configuration_descriptor(void);

const interface_descriptor *get_interface_descriptor(void);

const endpoint_descriptor *get_endpoint_descriptor(uint8_t ep, uint8_t in);

const msc_descriptor_tree *get_msc_descriptor_tree();