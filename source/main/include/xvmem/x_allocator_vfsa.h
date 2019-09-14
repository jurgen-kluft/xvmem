#ifndef __X_VIRTUAL_MEMORY_FSA_ALLOCATOR_H__
#define __X_VIRTUAL_MEMORY_FSA_ALLOCATOR_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE 
#pragma once 
#endif

#include "xbase/x_allocator.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
	// A fixed-size allocator (small allocator) using virtual memory.
	extern xfsalloc*		gCreateVirtualMemoryBasedFixedSizeAllocator(xalloc* main_allocator, xpage_alloc* page_allocator, u32 alloc_size);
};

#endif	/// __X_VIRTUAL_MEMORY_FSA_ALLOCATOR_H__

