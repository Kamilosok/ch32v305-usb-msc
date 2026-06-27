/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>
#include <string.h>
#include <ch32v30x_flash.h>

// All flash operations are done in FAST mode for SRAM saving
#define FLASH_PAGE_SIZE (256ul)
#define STORAGE_PAGE_FIRST (256ul)
#define STORAGE_PAGE_LAST (496ul)
// Impossible to reach page
#define PAGES_CLEAN (NUM_LBA * 2 + 1)

#define NUM_LBA (0x0080ul)

#define LBA_LENGTH (0x0200ul)

#define STORAGE_BASE (0x08000000ul + STORAGE_PAGE_FIRST * FLASH_PAGE_SIZE)
#define STORAGE_SIZE (NUM_LBA * FLASH_PAGE_SIZE)

#define ERASED_WORD (0xe339e339)

uint8_t *get_page_cache(void);

void sync_cache(void);

uint8_t store_data(uint32_t dest_addr, const uint8_t *src_buf, uint32_t data_len);

uint8_t retrieve_data(uint8_t *out_buf, uint32_t src_addr, uint32_t data_len);