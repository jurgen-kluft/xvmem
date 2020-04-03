#ifndef _X_ALLOCATOR_STRATEGY_FSA_LARGE_H_
#define _X_ALLOCATOR_STRATEGY_FSA_LARGE_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class xalloc;

    namespace xfsa_large
    {
        struct xinstance_t;

        // Memory Range is divided into Spaces
        //   A Space has an index to the Level that owns that Space
        //   A Level can own multiple spaces and are in a linked-list when not full
        //
        // Space:
        //   Can not be larger than 4 GB!
        //
        // Allocate:
        //   The size-request is transformed into the associated Level
        //   When the linked-list of spaces of that Level is empty we allocate a new space
        //   We take the linked list head node (space) and allocate from that space
        //
        // Deallocate:
        //   From the raw pointer we can compute the index of the space that it is part of.
        //   Knowing the space we can get the level index and the level.
        //   level->deallocate is called

        xinstance_t* create(xalloc* main_alloc, void* mem_address, u64 mem_range, u32 pagesize, u32 allocsize);
        void         destroy(xinstance_t*);
        void*        allocate(xinstance_t*, u32 size, u32 alignment);
        u32          deallocate(xinstance_t*, void* ptr);
    } // namespace xfsa_large
} // namespace xcore

#endif // _X_ALLOCATOR_STRATEGY_FSA_LARGE_H_