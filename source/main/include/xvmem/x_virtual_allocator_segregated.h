#ifndef __X_VMEM_SEGREGATED_VIRTUAL_ALLOCATOR_H__
#define __X_VMEM_SEGREGATED_VIRTUAL_ALLOCATOR_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class alloc_t;
    class fsa_t;
	class xvmem;

    // This is a virtual allocator that can manage a memory range and deal with large power-of-2 size allocations
    // e.g. 64KB, 128KB, 256KB, 512KB, 1MB, 2MB, 4MB, 8MB, 16MB, 32MB
    // Virtual memory is committed at the moment of allocation and decommitted at the moment of deallocation.
    extern alloc_t* gCreateVMemSegregatedAllocator(alloc_t* main_heap, fsa_t* node_heap, xvmem* vmem, u64 mem_range, u32 alloc_size_min, u32 alloc_size_max);

}; // namespace xcore

#endif /// __X_VMEM_SEGREGATED_VIRTUAL_ALLOCATOR_H__