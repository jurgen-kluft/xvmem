#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_coalesce_direct.h"

#include "xunittest/xunittest.h"

using namespace xcore;

extern xalloc* gTestAllocator;

UNITTEST_SUITE_BEGIN(strategy_coalesce_direct)
{
    UNITTEST_FIXTURE(main)
    {
		static void* sNodeData = nullptr;
		static xfsadexed* sNodeHeap = nullptr;

        UNITTEST_FIXTURE_SETUP()
		{
			const s32 sizeof_node = 16;
			const s32 countof_node = 16384;
			sNodeData = gTestAllocator->allocate(sizeof_node * countof_node);
			sNodeHeap = gTestAllocator->construct<xfsadexed_array>(sNodeData, sizeof_node, countof_node);
		}

        UNITTEST_FIXTURE_TEARDOWN()
		{
			gTestAllocator->deallocate(sNodeData);
			gTestAllocator->destruct(sNodeHeap);
		}

		UNITTEST_TEST(coalescee_init)
		{
			void* mem_base = (void*)0x00ff000000000000ULL;
			xcoalescestrat_direct::xinstance_t* c = xcoalescestrat_direct::create(gTestAllocator, sNodeHeap, 8*1024, 1024);
			xcoalescestrat_direct::destroy(c, gTestAllocator);
		}

		UNITTEST_TEST(coalescee_alloc_dealloc_pairs)
		{
			void* mem_base = (void*)0x00ff000000000000ULL;
			xcoalescestrat_direct::xinstance_t* c = xcoalescestrat_direct::create(gTestAllocator, sNodeHeap, 8*1024, 1024);

			void* p1 = xcoalescestrat_direct::allocate(c, 10 * 1024, 8);
			xcoalescestrat_direct::deallocate(c, p1);

			void* p2 = xcoalescestrat_direct::allocate(c, 10 * 1024, 8);
			xcoalescestrat_direct::deallocate(c, p2);

			void* p3 = xcoalescestrat_direct::allocate(c, 10 * 1024, 8);
			xcoalescestrat_direct::deallocate(c, p3);

			xcoalescestrat_direct::destroy(c, gTestAllocator);
		}

		UNITTEST_TEST(coalescee_alloc_dealloc_3_times)
		{
			void* mem_base = (void*)0x00ff000000000000ULL;
			xcoalescestrat_direct::xinstance_t* c = xcoalescestrat_direct::create(gTestAllocator, sNodeHeap, 8*1024, 1024);

			void* p1 = xcoalescestrat_direct::allocate(c, 8 * 1024, 8);
			void* p2 = xcoalescestrat_direct::allocate(c, 8 * 1024, 8);
			void* p3 = xcoalescestrat_direct::allocate(c, 8 * 1024, 8);

			xcoalescestrat_direct::deallocate(c, p1);
			xcoalescestrat_direct::deallocate(c, p2);
			xcoalescestrat_direct::deallocate(c, p3);

			xcoalescestrat_direct::destroy(c, gTestAllocator);
		}

		UNITTEST_TEST(coalescee_alloc_dealloc_fixed_size_many)
		{
			void* mem_base = (void*)0x00ff000000000000ULL;
			xcoalescestrat_direct::xinstance_t* c = xcoalescestrat_direct::create(gTestAllocator, sNodeHeap, 8*1024, 1024);

			const s32 cnt = 128;
			void* ptrs[cnt];
			for (s32 i=0; i<cnt; ++i)
			{
				ptrs[i] = xcoalescestrat_direct::allocate(c, 10 * 1024, 8);
			}
			for (s32 i=0; i<cnt; ++i)
			{
				xcoalescestrat_direct::deallocate(c, ptrs[i]);
			}

			xcoalescestrat_direct::destroy(c, gTestAllocator);
		}

	}
}
UNITTEST_SUITE_END
