#ifndef __X_VMEM_COALESCE_ALLOCATOR_H__
#define __X_VMEM_COALESCE_ALLOCATOR_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE 
#pragma once 
#endif

#include "xbase/x_allocator.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
	// This is a utility allocator that can manage a memory range and deal with allocations and deallocations purely
	// on a variable level. Nothing else is done here except managing a memory range.
	extern xalloc*	gCreateCoalesceUtilAllocator(xalloc* main_heap, xfsadexed* node_heap, void* mem_base, u64 mem_size, u32 size_min, u32 size_max, u32 size_step);

	// This is a virtual memory based allocator that is based on the coalesce strategy.
	// It can manage multiple 'regions' with a size that is provided by the user.
	extern xalloc*	gCreateVMemCoalesceBasedAllocator(xalloc* main_heap, xfsadexed* node_heap, xvmem* vmem, u64 region_mem_size, u32 size_min, u32 size_max, u32 size_step);
};

#endif	/// __X_VMEM_VFSA_ALLOCATOR_H__