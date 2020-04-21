#ifndef _X_ALLOCATOR_STRATEGY_FSA_LARGE_H_
#define _X_ALLOCATOR_STRATEGY_FSA_LARGE_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class xalloc;

    xalloc* create_alloc_fsa_large(xalloc* main_alloc, void* mem_address, u64 mem_range, u32 pagesize, u32 allocsize);

} // namespace xcore

#endif // _X_ALLOCATOR_STRATEGY_FSA_LARGE_H_