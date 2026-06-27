/*
 * Copyright (c) 2026 Kamil Zdancewicz
 * SPDX-License-Identifier: MIT
 */

#include <flash_storage.h>
#include <debug.h>

static __attribute__((aligned(4))) uint8_t page_cache[FLASH_PAGE_SIZE];
// Impossible page
static uint32_t dirty_page = PAGES_CLEAN;
static uint16_t dirty_bytes;

static uint8_t save_page(uint16_t page_num)
{
    if (page_num <= STORAGE_PAGE_LAST - STORAGE_PAGE_FIRST)
    {
        uint32_t page_addr = STORAGE_BASE + (STORAGE_PAGE_FIRST + page_num) * FLASH_PAGE_SIZE;

        __disable_irq();
        FLASH_Unlock_Fast();
        FLASH_ErasePage_Fast(page_addr);
        FLASH_ProgramPage_Fast(page_addr, (uint32_t *)page_cache);
        FLASH_Lock_Fast();
        __enable_irq();

        return 0;
    }

    return 1;
}

uint8_t *get_page_cache(void)
{
    return page_cache;
}

void sync_cache(void)
{
    if (dirty_bytes > 0)
    {
        uint32_t dirty_page_addr = STORAGE_BASE + (STORAGE_PAGE_FIRST + dirty_page) * FLASH_PAGE_SIZE;

        memcpy(page_cache + dirty_bytes, (uint8_t *)(dirty_page_addr + dirty_bytes), (FLASH_PAGE_SIZE - dirty_bytes));

        save_page(dirty_page);
    }
}

uint8_t store_data(uint32_t dest_addr, const uint8_t *src_buf, uint32_t data_len)
{
    uint32_t processed_bytes = 0;
    uint32_t curr_page = dest_addr / FLASH_PAGE_SIZE;
    uint16_t off_in_page = dest_addr % FLASH_PAGE_SIZE;

    uint32_t page_addr = STORAGE_BASE + (STORAGE_PAGE_FIRST + curr_page) * FLASH_PAGE_SIZE;

    uint16_t chunk;

    // Head
    if (off_in_page > 0)
    {
        chunk = FLASH_PAGE_SIZE - off_in_page;

        if (chunk > data_len)
        {
            chunk = data_len;
        }

        if (dirty_page == curr_page)
        {
            memcpy(page_cache + off_in_page, src_buf, chunk);
            dirty_bytes = off_in_page + chunk;
        }
        else
        {
            sync_cache();

            // To not lose data when writing to a middle of a new page
            memcpy(page_cache, (uint8_t *)page_addr, off_in_page);
            memcpy(page_cache + off_in_page, src_buf, chunk);
            dirty_bytes = off_in_page + chunk;
            dirty_page = curr_page;
        }

        if (off_in_page + chunk == FLASH_PAGE_SIZE)
        {
            if (save_page(curr_page))
            {
                return 1;
            }

            dirty_page = PAGES_CLEAN;
            dirty_bytes = 0;
            curr_page += 1;
        }

        processed_bytes += chunk;
    }

    // Body
    while (data_len - processed_bytes > FLASH_PAGE_SIZE)
    {
        memcpy(page_cache, src_buf + processed_bytes, FLASH_PAGE_SIZE);
        if (save_page(curr_page))
        {
            return 1;
        }
        curr_page += 1;

        processed_bytes += FLASH_PAGE_SIZE;
    }

    // Tail
    if (data_len - processed_bytes > 0)
    {
        memcpy(page_cache, src_buf + processed_bytes, (data_len - processed_bytes));
        dirty_page = curr_page;
        dirty_bytes = data_len - processed_bytes;
    }

    return 0;
}

uint8_t retrieve_data(uint8_t *out_buf, uint32_t src_addr, uint32_t data_len)
{
    uint32_t curr_page = src_addr / FLASH_PAGE_SIZE;
    uint32_t last_page = (src_addr + data_len) / FLASH_PAGE_SIZE;

    if (last_page > STORAGE_PAGE_LAST - STORAGE_PAGE_FIRST || curr_page > STORAGE_PAGE_LAST - STORAGE_PAGE_FIRST)
    {
        return 1;
    }

    if (curr_page <= dirty_page && dirty_page <= last_page)
    {
        sync_cache();
    }

    uint16_t off_in_page = src_addr % FLASH_PAGE_SIZE;
    uint32_t page_addr = STORAGE_BASE + (STORAGE_PAGE_FIRST + curr_page) * FLASH_PAGE_SIZE + off_in_page;

    memcpy(out_buf, (uint8_t *)page_addr, data_len);

    return 0;
}
