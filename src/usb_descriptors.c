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
    .idVendor = 0x1234,  // Cokolwiek byle nie zero
    .idProduct = 0x5678, // Cokolwiek byle nie zero
    .bcdDevice = 0x0100,

    // Change iSerial when adding win
    .iManufacturer = 0,
    .iProduct = 0,
    .iSerialNumber = 0,
    .bNumConfigurations = 1,
};

static const config_descriptor cd = {
    .bLength = 9,
    .bDescriptorType = 0x02,
    .wTotalLength = sizeof(config_descriptor) + sizeof(interface_descriptor) + 2 * sizeof(endpoint_descriptor), // + (endpoint, and class or vendor specific descriptors
    .bNumInterfaces = 1,
    .bConfigurationValue = 1,
    .iConfiguration = 0, // No string
    .bmAttributes = 0x80,
    .bMaxPower = 50, // 100 mA
};

static const interface_descriptor id = {
    .bLength = 9,
    .bDescriptorType = 0x04,
    .bInterfaceNumber = 0,
    .bAlternateSetting = 0,
    .bNumEndpoints = 2, // EP1 IN and OUT
    .bInterfaceClass = 0x08,
    .bInterfaceSubClass = 0x06,
    .bInterfaceProtocol = 0x50,
    .iInterface = 0,
};

static const endpoint_descriptor ed1_in = {
    .bLength = 7,
    .bDescriptorType = 0x05,
    // In EP1
    .bEndpointAddress = ((1 & 0x01) << 7) | (1 & 0b1111),
    .bmAttributes = 0x02, // Bulk
    .wMaxPacketSize = 64,
    // Ignored for Bulk & Control Endpoints
    .bInterval = 0,
};

static const endpoint_descriptor ed1_out = {
    .bLength = 7,
    .bDescriptorType = 0x05,
    // Out EP1
    .bEndpointAddress = ((0 & 0x01) << 7) | (1 & 0b1111),
    .bmAttributes = 0x02, // Bulk
    .wMaxPacketSize = 64,
    // Ignored for Bulk & Control Endpoints
    .bInterval = 0,
};

static const endpoint_descriptor *const ep_descriptors[] = {
    &ed1_in,
    &ed1_out,
};

const device_descriptor *get_device_descriptor(void)
{
    return &dd;
}

const config_descriptor *get_configuration_descriptor(void)
{
    return &cd;
}

const interface_descriptor *get_interface_descriptor(void)
{
    return &id;
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
