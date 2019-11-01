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

	// A virtual memory allocator (CPU memory only)
	// + Small Size Allocator (FSA)
	// + Coalesce Allocator, Heap 1 and Heap 2
	// + Segregated Allocator (Large Size)
	// + Giant Allocator
	extern xalloc*		gCreateVmAllocator(xalloc*);
};

#endif	// __X_ALLOCATOR_VIRTUAL_ALLOCATOR_H__