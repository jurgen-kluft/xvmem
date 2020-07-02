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
        static void*      sNodeData = nullptr;
        static xfsadexed* sNodeHeap = nullptr;

        UNITTEST_FIXTURE_SETUP()
        {
            const s32 sizeof_node  = 16;
            const s32 countof_node = 16384;
            sNodeData              = gTestAllocator->allocate(sizeof_node * countof_node);
            sNodeHeap              = gTestAllocator->construct<xfsadexed_array>(sNodeData, sizeof_node, countof_node);
        }

        UNITTEST_FIXTURE_TEARDOWN()
        {
            gTestAllocator->deallocate(sNodeData);
            gTestAllocator->destruct(sNodeHeap);
        }

        UNITTEST_TEST(coalescee_init)
        {
            xalloc* c = create_alloc_coalesce_direct(gTestAllocator, sNodeHeap, (void*)0, 512 * 1024 * 1024, 4 * 1024, 64 * 1024, 256, 4096);
            c->release();
        }

        UNITTEST_TEST(coalescee_alloc_dealloc_pairs)
        {
            xalloc* c = create_alloc_coalesce_direct(gTestAllocator, sNodeHeap, (void*)0, 512 * 1024 * 1024, 4 * 1024, 64 * 1024, 256, 4096);

            void* p1 = c->allocate(10 * 1024, 8);
            u32   s1 = c->deallocate(p1);
            CHECK_EQUAL(10 * 1024, s1);

            void* p2 = c->allocate(12 * 1024, 8);
            u32   s2 = c->deallocate(p2);
            CHECK_EQUAL(10 * 1024, s1);

            void* p3 = c->allocate(14 * 1024, 8);
            u32   s3 = c->deallocate(p3);
            CHECK_EQUAL(10 * 1024, s1);

            c->release();
        }

        UNITTEST_TEST(coalescee_alloc_dealloc_3_times)
        {
            xalloc* c = create_alloc_coalesce_direct(gTestAllocator, sNodeHeap, (void*)0, 512 * 1024 * 1024, 4 * 1024, 64 * 1024, 256, 4096);

            void* p1 = c->allocate(8 * 1024, 8);
            void* p2 = c->allocate(8 * 1024, 8);
            void* p3 = c->allocate(8 * 1024, 8);

            c->deallocate(p1);
            c->deallocate(p2);
            c->deallocate(p3);

            c->release();
        }

        UNITTEST_TEST(coalescee_alloc_dealloc_fixed_size_many)
        {
            xalloc* c = create_alloc_coalesce_direct(gTestAllocator, sNodeHeap, (void*)0, 512 * 1024 * 1024, 4 * 1024, 64 * 1024, 256, 4096);

            const s32 cnt = 32;
            void*     ptrs[cnt];

			s32 i;
            for (i = 0; i < 13; ++i)
            {
                ptrs[i] = c->allocate(10 * 1024, 8);
            }
            for (; i < cnt; ++i)
            {
                ptrs[i] = c->allocate(10 * 1024, 8);
            }
            i = 0;
            for (; i < (cnt - 1); ++i)
            {
                u32 const s1 = c->deallocate(ptrs[i]);
                CHECK_EQUAL(10 * 1024, s1);
            }
            for (; i < cnt; ++i)
            {
                u32 const s1 = c->deallocate(ptrs[i]);
                CHECK_EQUAL(10 * 1024, s1);
            }

            c->release();
        }
    }
}
UNITTEST_SUITE_END
