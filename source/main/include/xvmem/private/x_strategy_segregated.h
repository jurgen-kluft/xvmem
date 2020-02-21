#ifndef _X_ALLOCATOR_STRATEGY_SEGREGATED_H_
#define _X_ALLOCATOR_STRATEGY_SEGREGATED_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class xalloc;
    class xfsadexed;

    namespace xsegregatedstrat
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

		xinstance_t* create(xalloc* main_alloc, xfsadexed* node_alloc, void* vmem_address, u64 vmem_space, u64 space_size, u32 allocsize_min, u32 allocsize_max, u32 allocsize_step, u32 page_size);
        void	   destroy(xinstance_t*);
		void*      allocate(xinstance_t*, u32 size, u32 alignment);
		void       deallocate(xinstance_t*, void* ptr);
    }
}

#endif // _X_ALLOCATOR_STRATEGY_SEGREGATED_H_