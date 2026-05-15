#pragma once
#include <stdint.h>

void pmm_init(uint64_t bi_v, uint64_t bi_p, uint64_t identity_PDPT_p);
void *pmm_allocate_frames(uint64_t count, uint64_t alignment);
void pmm_free_frames(void *start_address, uint64_t count);