#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_coalesce.h"

#include "xunittest/xunittest.h"

using namespace xcore;

extern xalloc* gTestAllocator;

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
			xcoalescestrat::xinstance_t* c = xcoalescestrat::create(gTestAllocator, sNodeHeap, mem_base, (u64)128 * 1024 * 1024 * 1024, 8*1024, 640 * 1024, 256);
			xcoalescestrat::destroy(c);
		}

		UNITTEST_TEST(coalescee_alloc_dealloc_pairs)
		{
			void* mem_base = (void*)0x00ff000000000000ULL;
			xcoalescestrat::xinstance_t* c = xcoalescestrat::create(gTestAllocator, sNodeHeap, mem_base, (u64)128 * 1024 * 1024 * 1024, 8*1024, 640 * 1024, 256);

			void* p1 = xcoalescestrat::allocate(c, 10 * 1024, 8);
			xcoalescestrat::deallocate(c, p1);

			void* p2 = xcoalescestrat::allocate(c, 10 * 1024, 8);
			xcoalescestrat::deallocate(c, p2);

			void* p3 = xcoalescestrat::allocate(c, 10 * 1024, 8);
			xcoalescestrat::deallocate(c, p3);

			xcoalescestrat::destroy(c);
		}

		UNITTEST_TEST(coalescee_alloc_dealloc_3_times)
		{
			void* mem_base = (void*)0x00ff000000000000ULL;
			xcoalescestrat::xinstance_t* c = xcoalescestrat::create(gTestAllocator, sNodeHeap, mem_base, (u64)128 * 1024 * 1024 * 1024, 8*1024, 640 * 1024, 256);

			void* p1 = xcoalescestrat::allocate(c, 8 * 1024, 8);
			void* p2 = xcoalescestrat::allocate(c, 8 * 1024, 8);
			void* p3 = xcoalescestrat::allocate(c, 8 * 1024, 8);

			xcoalescestrat::deallocate(c, p1);
			xcoalescestrat::deallocate(c, p2);
			xcoalescestrat::deallocate(c, p3);

			xcoalescestrat::destroy(c);
		}

		UNITTEST_TEST(coalescee_alloc_dealloc_fixed_size_many)
		{
			void* mem_base = (void*)0x00ff000000000000ULL;
			xcoalescestrat::xinstance_t* c = xcoalescestrat::create(gTestAllocator, sNodeHeap, mem_base, (u64)128 * 1024 * 1024 * 1024, 8*1024, 640 * 1024, 256);

			const s32 cnt = 128;
			void* ptrs[cnt];
			for (s32 i=0; i<cnt; ++i)
			{
				ptrs[i] = xcoalescestrat::allocate(c, 10 * 1024, 8);
			}
			for (s32 i=0; i<cnt; ++i)
			{
				xcoalescestrat::deallocate(c, ptrs[i]);
			}

			xcoalescestrat::destroy(c);
		}

	}
}
UNITTEST_SUITE_END
