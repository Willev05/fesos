#pragma once
#include <stdint.h>
#include "../kernel/boot_info.h"

void pmm_init(boot_info *bi, uint64_t bi_p);
void *pmm_allocate_frames(uint64_t count, uint64_t alignment);
void pmm_free_frames(void *start_address, uint64_t count);