#ifndef __X_VMEM_VIRTUAL_ALLOCATOR_SMALL_H__
#define __X_VMEM_VIRTUAL_ALLOCATOR_SMALL_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
	class alloc_t;
	class fsadexed_t;
	class xvmem;
	struct xpages_t;

    // A fixed-size allocator (small allocator) using virtual memory constraint in such a way as to be
    // able to get a 32-bit index for every allocation.
    // NOTE: index == 0xFFFFFFFF is NIL (e.g. nullptr)
    fsadexed_t* gCreateVMemBasedDexedFsa(alloc_t* main_allocator, xpages_t* vpages, u32 allocsize);
	fsadexed_t* gCreateVMemBasedDexedFsa(alloc_t* main_allocator, xvmem* vmem, u64 mem_range, u32 allocsize);

}; // namespace xcore

#endif /// __X_VMEM_VIRTUAL_ALLOCATOR_SMALL_H__