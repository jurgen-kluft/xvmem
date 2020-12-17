#ifndef _X_ALLOCATOR_STRATEGY_VIRTUAL_COMMIT_AND_DECOMMIT_DIRECT_H_
#define _X_ALLOCATOR_STRATEGY_VIRTUAL_COMMIT_AND_DECOMMIT_DIRECT_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class alloc_t;
    class xvmem;

    alloc_t* create_page_vcd_direct(alloc_t* main_heap, alloc_t* allocator, xvmem* vmem, u32 page_size);

} // namespace xcore

#endif // _X_ALLOCATOR_STRATEGY_VIRTUAL_COMMIT_AND_DECOMMIT_DIRECT_H_