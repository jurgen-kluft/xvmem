#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_coalesce.h"

#include "xunittest/xunittest.h"

using namespace xcore;

extern xalloc* gSystemAllocator;

UNITTEST_SUITE_BEGIN(strategy_coalesce)
{
    UNITTEST_FIXTURE(main)
    {
		static void* sNodeData = nullptr;
		static xfsadexed* sNodeHeap = nullptr;

        UNITTEST_FIXTURE_SETUP()
		{
			const s32 sizeof_node = 32;
			const s32 countof_node = 16384;
			sNodeData = gSystemAllocator->allocate(sizeof_node * countof_node, 8);
			sNodeHeap = gSystemAllocator->construct<xfsadexed_list>(sNodeData, sizeof_node, countof_node);
		}

        UNITTEST_FIXTURE_TEARDOWN()
		{
			gSystemAllocator->deallocate(sNodeData);
		}

		UNITTEST_TEST(coalescee_init)
		{
			xcoalescee c;
			void* mem_base = (void*)0x00ff000000000000ULL;
			c.initialize(gSystemAllocator, sNodeHeap, mem_base, (u64)128 * 1024 * 1024 * 1024, 8*1024, 640 * 1024, 256);
			c.release();
		}

		UNITTEST_TEST(coalescee_alloc_dealloc_once)
		{
			xcoalescee c;
			void* mem_base = (void*)0x00ff000000000000ULL;
			c.initialize(gSystemAllocator, sNodeHeap, mem_base, (u64)128 * 1024 * 1024 * 1024, 8*1024, 640 * 1024, 256);

			void* p = c.allocate(10 * 1024, 8);
			c.deallocate(p);

			c.release();
		}

		UNITTEST_TEST(coalescee_alloc_dealloc_fixed_size_many)
		{
			xcoalescee c;
			void* mem_base = (void*)0x00ff000000000000ULL;
			c.initialize(gSystemAllocator, sNodeHeap, mem_base, (u64)128 * 1024 * 1024 * 1024, 8*1024, 640 * 1024, 256);

			const s32 cnt = 128;
			void* ptrs[cnt];
			for (s32 i=0; i<cnt; ++i)
			{
				ptrs[i] = c.allocate(10 * 1024, 8);
			}
			for (s32 i=0; i<cnt; ++i)
			{
				c.deallocate(ptrs[i]);
			}

			c.release();
		}

	}
}
UNITTEST_SUITE_END
