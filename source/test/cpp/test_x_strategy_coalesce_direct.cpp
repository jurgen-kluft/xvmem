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
			xcoalescestrat_direct::xinstance_t* c = xcoalescestrat_direct::create(gTestAllocator, sNodeHeap, 8*1024, 1024);
			xcoalescestrat_direct::destroy(c, gTestAllocator);
		}

		UNITTEST_TEST(coalescee_alloc_dealloc_pairs)
		{
			xcoalescestrat_direct::xinstance_t* c = xcoalescestrat_direct::create(gTestAllocator, sNodeHeap, 8*1024, 1024);

			void* p1 = xcoalescestrat_direct::allocate(c, 10 * 1024, 8);
			u32 s1 = xcoalescestrat_direct::deallocate(c, p1);
			CHECK_EQUAL(10 * 1024, s1);

			void* p2 = xcoalescestrat_direct::allocate(c, 12 * 1024, 8);
			u32 s2 = xcoalescestrat_direct::deallocate(c, p2);
			CHECK_EQUAL(10 * 1024, s1);

			void* p3 = xcoalescestrat_direct::allocate(c, 14 * 1024, 8);
			u32 s3 = xcoalescestrat_direct::deallocate(c, p3);
			CHECK_EQUAL(10 * 1024, s1);

			xcoalescestrat_direct::destroy(c, gTestAllocator);
		}

		UNITTEST_TEST(coalescee_alloc_dealloc_3_times)
		{
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
			xcoalescestrat_direct::xinstance_t* c = xcoalescestrat_direct::create(gTestAllocator, sNodeHeap, 8*1024, 1024);

			const s32 cnt = 32;
			void* ptrs[cnt];
			for (s32 i=0; i<cnt; ++i)
			{
				ptrs[i] = xcoalescestrat_direct::allocate(c, 10 * 1024, 8);
			}
			s32 i=0;
			for (; i<(cnt-1); ++i)
			{
				u32 const s1 = xcoalescestrat_direct::deallocate(c, ptrs[i]);
				CHECK_EQUAL(10 * 1024, s1);
			}
			for (; i<cnt; ++i)
			{
				u32 const s1 = xcoalescestrat_direct::deallocate(c, ptrs[i]);
				CHECK_EQUAL(10 * 1024, s1);
			}

			xcoalescestrat_direct::destroy(c, gTestAllocator);
		}

	}
}
UNITTEST_SUITE_END
