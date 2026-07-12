/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#include <usb_descriptors.h>

static const device_descriptor dd = {
    .bLength = 18,
    .bDescriptorType = 0x01,
    .bcdUSB = 0x0200,

    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize = 64,
    .idVendor = 0x0,
    .idProduct = 0x0,
    .bcdDevice = 0x0100,

    .iManufacturer = 0,
    .iProduct = 0,
    .iSerialNumber = 0,
    .bNumConfigurations = 1,
};

// The specific descriptor tree for my MSC device, though open for expansion in the future
static const msc_descriptor_tree msc_dt = {
    .config = {
        .bLength = 9,
        .bDescriptorType = 0x02,
        .wTotalLength = sizeof(msc_descriptor_tree),
        .bNumInterfaces = 1,
        .bConfigurationValue = 1,
        .iConfiguration = 0,
        .bmAttributes = 0x80,
        .bMaxPower = 50,
    },
    .interface = {
        .bLength = 9,
        .bDescriptorType = 0x04,
        .bInterfaceNumber = 0,
        .bAlternateSetting = 0,
        .bNumEndpoints = 2,
        .bInterfaceClass = 0x08,
        .bInterfaceSubClass = 0x06,
        .bInterfaceProtocol = 0x50,
        .iInterface = 0,
    },
    .ep1_out = {
        .bLength = 7,
        .bDescriptorType = 0x05,
        .bEndpointAddress = 0x01,
        .bmAttributes = 0x02,
        .wMaxPacketSize = 64,
        .bInterval = 0,
    },
    .ep1_in = {
        .bLength = 7,
        .bDescriptorType = 0x05,
        .bEndpointAddress = 0x81,
        .bmAttributes = 0x02,
        .wMaxPacketSize = 64,
        .bInterval = 0,
    },
};

static const endpoint_descriptor *const ep_descriptors[] = {
    &msc_dt.ep1_in,
    &msc_dt.ep1_out,
};

const device_descriptor *get_device_descriptor(void)
{
    return &dd;
}

const config_descriptor *get_configuration_descriptor(void)
{
    return &msc_dt.config;
}

const interface_descriptor *get_interface_descriptor(void)
{
    return &msc_dt.interface;
}

const endpoint_descriptor *get_endpoint_descriptor(uint8_t ep, uint8_t in)
{
    if (ep <= LAST_EP)
    {
        return ep_descriptors[2 * (ep - 1) + (1 - in)];
    }
    else
        return NULL;
}

const msc_descriptor_tree *get_msc_descriptor_tree()
{
    return &msc_dt;
}
