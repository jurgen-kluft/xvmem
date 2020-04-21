#ifndef _X_ALLOCATOR_STRATEGY_PAGE_COMMIT_AND_DECOMMIT_H_
#define _X_ALLOCATOR_STRATEGY_PAGE_COMMIT_AND_DECOMMIT_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class xalloc;
    class xfsadexed;

    // Memory range (address_range) is divided into regions (region_size)
    // When we detect that a region is used we 'commit' the pages in that region
    // When we detect that a region is not-used we 'decommit' the pages in that region
    // |----X----X----X----X----X----X----X----X----X----X----X----X----|
    //
    // We can manage this for example with bits:
    // 8  bits = 512 KB
    // 16 bits = 1 MB
    // 32 bits = 2 MB
    // | u16 | u16 | u16 | u16 | u16 | u16 | u16 | u16 | u16 | u16 | u16 |

    xalloc* create_page_cad(xalloc* main_heap, xalloc* allocator, void* address_base, u64 address_range, u32 region_size);

} // namespace xcore

#endif // _X_ALLOCATOR_STRATEGY_PAGE_COMMIT_AND_DECOMMIT_H_