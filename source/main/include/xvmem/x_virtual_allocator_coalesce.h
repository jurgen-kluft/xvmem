#ifndef __X_VMEM_COALESCE_ALLOCATOR_H__
#define __X_VMEM_COALESCE_ALLOCATOR_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE 
#pragma once 
#endif

namespace xcore
{
	class xalloc;
	class xfsadexed;
	class xvmem;

	// This is a virtual memory based allocator that is based on the coalesce strategy.
	// It can manage multiple 'regions' with a size that is provided by the user.
	extern xalloc*	gCreateVMemCoalesceBasedAllocator(xalloc* main_heap, xfsadexed* node_heap, xvmem* vmem, u64 region_mem_size, u32 size_min, u32 size_max, u32 size_step);
};

#endif	/// __X_VMEM_VFSA_ALLOCATOR_H__