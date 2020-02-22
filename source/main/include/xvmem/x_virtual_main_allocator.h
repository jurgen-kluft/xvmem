#ifndef __X_ALLOCATOR_VIRTUAL_ALLOCATOR_H__
#define __X_ALLOCATOR_VIRTUAL_ALLOCATOR_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE 
#pragma once 
#endif

namespace xcore
{
	// Forward declares
	class xalloc;
	class xvmem;

	// A virtual memory allocator, suitable for CPU as well as GPU memory
	// + Small Size Allocator
	// + Coalesce Allocator
	// + Segregated Allocator
	// + Large Allocator
	extern xalloc*		gCreateVmAllocator(xalloc* main_heap, xvmem* vmem);
};

#endif	// __X_ALLOCATOR_VIRTUAL_ALLOCATOR_H__