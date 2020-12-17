#ifndef _X_ALLOCATOR_STRATEGY_VIRTUAL_COMMIT_AND_DECOMMIT_REGIONS_H_
#define _X_ALLOCATOR_STRATEGY_VIRTUAL_COMMIT_AND_DECOMMIT_REGIONS_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class alloc_t;
    class xvmem;

    // Memory range (address_range) is divided into regions (region_size)
    // When we detect that a region is used we 'commit' the pages in that region
    // When we detect that a region is not-used we 'decommit' the pages in that region
    // |----X----X----X----X----X----X----X----X----X----X----X----X----|
    //
    // Every region has a counter and when during allocation we detect an intersection we
    // increment. During deallocation we decrement the counter.
    // When the counter changes from '0' to '1' we commit the region.
    // When the counter changes from '1' to '0' we decommit the region.

    alloc_t* create_page_vcd_regions(alloc_t* main_heap, alloc_t* allocator, xvmem* vmem, void* address_base, u64 address_range, u32 page_size, u32 region_size);

} // namespace xcore

#endif // _X_ALLOCATOR_STRATEGY_VIRTUAL_COMMIT_AND_DECOMMIT_REGIONS_H_