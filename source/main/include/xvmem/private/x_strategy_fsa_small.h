#ifndef _X_ALLOCATOR_FSA_STRATEGY_H_
#define _X_ALLOCATOR_FSA_STRATEGY_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
	class alloc_t;
	class xvmem;

    alloc_t* create_alloc_fsa(alloc_t* main_heap, xvmem* vmem, u64 mem_range, void*& mem_base);
} // namespace xcore

#endif // _X_ALLOCATOR_FSA_STRATEGY_H_