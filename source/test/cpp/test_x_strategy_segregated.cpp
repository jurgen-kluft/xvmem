#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_segregated.h"

#include "xunittest/xunittest.h"

using namespace xcore;

extern alloc_t* gTestAllocator;

UNITTEST_SUITE_BEGIN(strategy_segregated)
{
    UNITTEST_FIXTURE(main)
    {
        static void*      sNodeData = nullptr;
        static fsadexed_t* sNodeHeap = nullptr;

        static const u64 sMemoryRange = (u64)128 * 1024 * 1024 * 1024;
        static const u64 sMinumSize   = (u64)512 * 1024;
        static const u64 sMaximumSize = (u64)32 * 1024 * 1024;
        static const u32 sPageSize    = (u64)64 * 1024;

        UNITTEST_FIXTURE_SETUP()
        {
            const u32 sizeof_node  = 32;
            const u32 countof_node = 16384;
            sNodeData              = gTestAllocator->allocate(sizeof_node * countof_node, 8);
            sNodeHeap              = gTestAllocator->construct<fsadexed_array_t>(sNodeData, sizeof_node, countof_node);
        }

        UNITTEST_FIXTURE_TEARDOWN()
        {
            gTestAllocator->deallocate(sNodeData);
            gTestAllocator->destruct(sNodeHeap);
        }

        UNITTEST_TEST(init)
        {
            void*   mem_base = (void*)0x00ff000000000000ULL;
            alloc_t* a        = create_alloc_segregated(gTestAllocator, sNodeHeap, mem_base, sMemoryRange, sMinumSize, sMaximumSize, sPageSize);
            a->release();
        }

        UNITTEST_TEST(alloc_dealloc_1)
        {
            void*   mem_base = (void*)0x00ff000000000000ULL;
            alloc_t* a        = create_alloc_segregated(gTestAllocator, sNodeHeap, mem_base, sMemoryRange, sMinumSize, sMaximumSize, sPageSize);

            void* p1 = a->allocate(sMinumSize, sPageSize);
            a->deallocate(p1);

            a->release();
        }

        UNITTEST_TEST(alloc_dealloc_many)
        {
            void*   mem_base = (void*)0x00ff000000000000ULL;
            alloc_t* a        = create_alloc_segregated(gTestAllocator, sNodeHeap, mem_base, sMemoryRange, sMinumSize, sMaximumSize, sPageSize);

			u32 size = sMinumSize;
			for (s32 i=0; i<6; ++i)
			{
				void* p1 = a->allocate(size, sPageSize);
				a->deallocate(p1);

				size = size * 2;
			}

            a->release();
        }

    }
}
UNITTEST_SUITE_END
