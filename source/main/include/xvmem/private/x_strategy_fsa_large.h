#ifndef _X_ALLOCATOR_STRATEGY_FSA_LARGE_H_
#define _X_ALLOCATOR_STRATEGY_FSA_LARGE_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class alloc_t;
    class fsa_t;

    alloc_t* create_alloc_fsa_large(alloc_t* main_alloc, fsa_t* node_alloc, void* mem_address, u64 mem_range, u32 pagesize, u32 allocsize);

    class xfsa_large_utils
    {
    public:
        static u32  allocsize_to_bwidth(u32 allocsize, u32 pagesize);
        static u32  allocsize_to_bits(u32 allocsize, u32 pagesize, u32 bw, u32 wi);
        static u32  bits_to_allocsize(u32 b, u32 w, u32 pagesize);
        static u64  allocsize_to_blockrange(u32 allocsize, u32 pagesize);
        static bool has_empty_slot(u32 slot, u32 ws);
        static u32  get_empty_slot(u32 slot, u32 ws);
        static u32  set_slot_empty(u32 slot, u32 ws);
        static u32  set_slot_occupied(u32 slot, u32 ws);
        static u32  get_slot_value(u32 slot, u32 bw, u32 ws);
        static u32  clr_slot_value(u32 slot, u32 bw, u32 ws);
        static u32  set_slot_value(u32 slot, u32 bw, u32 ws, u32 ab);
        static u32  get_slot_mask(u32 slot, u32 bw, u32 ws);
    };

} // namespace xcore

#endif // _X_ALLOCATOR_STRATEGY_FSA_LARGE_H_