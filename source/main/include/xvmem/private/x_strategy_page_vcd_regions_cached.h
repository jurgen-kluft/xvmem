#ifndef _X_ALLOCATOR_STRATEGY_VIRTUAL_COMMIT_AND_DECOMMIT_REGIONS_WITH_CACHING_H_
#define _X_ALLOCATOR_STRATEGY_VIRTUAL_COMMIT_AND_DECOMMIT_REGIONS_WITH_CACHING_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class xalloc;
    class xvmem;

    xalloc* create_page_vcd_regions_cached(xalloc* main_heap, xalloc* allocator, xvmem* vmem, void* address_base, u64 address_range, u32 page_size, u32 region_size, u32 num_regions_to_cache);

} // namespace xcore

#endif // _X_ALLOCATOR_STRATEGY_VIRTUAL_COMMIT_AND_DECOMMIT_REGIONS_WITH_CACHING_H_