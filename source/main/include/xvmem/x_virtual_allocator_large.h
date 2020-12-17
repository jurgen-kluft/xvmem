#ifndef __X_VMEM_LARGE_ALLOCATOR_H__
#define __X_VMEM_LARGE_ALLOCATOR_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE 
#pragma once 
#endif

#include "xbase/x_allocator.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
	// This is a virtual allocator that can manage a memory range and deal with large (>32 MiB) allocations and deallocations
	extern alloc_t*	gCreateVMemLargeAllocator(alloc_t* internal_heap, fsadexed_t* node_heap, xvmem* vmem, u64 vmem_range, u32 alloc_size_min, u32 alloc_size_max);
};

#endif	/// __X_VMEM_LARGE_ALLOCATOR_H__