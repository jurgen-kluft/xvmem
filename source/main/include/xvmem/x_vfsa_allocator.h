#ifndef __X_VMEM_VFSA_ALLOCATOR_H__
#define __X_VMEM_VFSA_ALLOCATOR_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE 
#pragma once 
#endif

#include "xbase/x_allocator.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
	class xvpages_t;

	// A fixed-size allocator (small allocator) using virtual memory.
	extern xfsa*		gCreateVMemBasedFsa(xalloc* main_allocator, xvpages_t* vpages, u32 allocsize);

	// A fixed-size allocator (small allocator) using virtual memory constraint in such a way as to be
	// able to get a 32-bit index for every allocation.
	extern xfsadexed*	gCreateVMemBasedDexedFsa(xalloc* main_allocator, xvpages_t* vpages, u32 allocsize);
};

#endif	/// __X_VMEM_VFSA_ALLOCATOR_H__