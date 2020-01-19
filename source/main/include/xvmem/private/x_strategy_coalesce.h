#ifndef _X_ALLOCATOR_COALESCE_H_
#define _X_ALLOCATOR_COALESCE_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
	class xalloc;
	class xfsadexed;
	struct xcoalescee;

	xcoalescee* create(xalloc* main_heap, xfsadexed* node_heap, void* mem_addr, u64 mem_size, u32 size_min, u32 size_max, u32 size_step);
    void	   destroy(xcoalescee*);
	void*      allocate(xcoalescee*, u32 size, u32 alignment);
	u32        deallocate(xcoalescee*, void* ptr);

} // namespace xcore

#endif // _X_ALLOCATOR_COALESCE_H_