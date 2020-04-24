#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_strategy_fsa_large.h"

#include "xunittest/xunittest.h"

using namespace xcore;

extern xalloc* gTestAllocator;

UNITTEST_SUITE_BEGIN(strategy_fsa_large)
{
    UNITTEST_FIXTURE(main)
    {
        static void*      sNodeData = nullptr;
        static xfsadexed* sNodeHeap = nullptr;

        UNITTEST_FIXTURE_SETUP()
        {
            const s32 sizeof_node  = 32;
            const s32 countof_node = 16384;
            sNodeData              = gTestAllocator->allocate(sizeof_node * countof_node);
            sNodeHeap              = gTestAllocator->construct<xfsadexed_array>(sNodeData, sizeof_node, countof_node);
        }

        UNITTEST_FIXTURE_TEARDOWN()
        {
            gTestAllocator->deallocate(sNodeData);
            gTestAllocator->destruct(sNodeHeap);
        }

        UNITTEST_TEST(create_then_release)
        {
            void*     mem_base  = (void*)0x10000000;
            u64 const mem_range = (u64)16 * 1024 * 1024 * 1024;
            u32 const pagesize  = 64 * 1024;
            u32 const allocsize = 64 * 1024;

            xalloc* fsa = create_alloc_fsa_large(gTestAllocator, sNodeHeap, mem_base, mem_range, pagesize, allocsize);

            fsa->release();
        }

        UNITTEST_TEST(create_then_alloc_then_release)
        {
            void*     mem_base  = (void*)0x10000000;
            u64 const mem_range = (u64)16 * 1024 * 1024 * 1024;
            u32 const pagesize  = 64 * 1024;
            u32 const allocsize = 64 * 1024;

            xalloc* fsa = create_alloc_fsa_large(gTestAllocator, sNodeHeap, mem_base, mem_range, pagesize, allocsize);

            void* p1 = fsa->allocate(40 * 1024, sizeof(void*));
            CHECK_NOT_NULL(p1);
            u32 const s1 = fsa->deallocate(p1);
            CHECK_EQUAL(64 * 1024, s1);

            fsa->release();
        }

        UNITTEST_TEST(create_then_alloc_free_many_64KB_then_release)
        {
            void*     mem_base  = (void*)0x10000000;
            u64 const mem_range = (u64)16 * 1024 * 1024 * 1024;
            u32 const pagesize  = 64 * 1024;
            u32 const allocsize = 64 * 1024;

            xalloc* fsa = create_alloc_fsa_large(gTestAllocator, sNodeHeap, mem_base, mem_range, pagesize, allocsize);

            // The behaviour of the allocator is that it has given us pointers relative to mem_base in an
            // incremental way using 'allocsize'.
            for (s32 i = 0; i < 255; ++i)
            {
                void* p1 = fsa->allocate(40 * 1024, sizeof(void*));
                CHECK_NOT_NULL(p1);
                void* pe = (void*)((u64)mem_base + i * allocsize);
                CHECK_EQUAL(pe, p1);
            }
            for (s32 i = 255; i < 1024; ++i)
            {
                void* p1 = fsa->allocate(40 * 1024, sizeof(void*));
                CHECK_NOT_NULL(p1);
                void* pe = (void*)((u64)mem_base + i * allocsize);
                CHECK_EQUAL(pe, p1);
            }
            for (s32 i = 0; i < 1024; ++i)
            {
                void*     p1 = (void*)((u64)mem_base + i * allocsize);
                u32 const s1 = fsa->deallocate(p1);
                CHECK_EQUAL(64 * 1024, s1);
            }

            fsa->release();
        }

        UNITTEST_TEST(create_then_alloc_free_many_128KB_then_release)
        {
            void*     mem_base  = (void*)0x10000000;
            u64 const mem_range = (u64)16 * 1024 * 1024 * 1024;
            u32 const pagesize  = 64 * 1024;
            u32 const allocsize = 128 * 1024;

            xalloc* fsa = create_alloc_fsa_large(gTestAllocator, sNodeHeap, mem_base, mem_range, pagesize, allocsize);

            // The behaviour of the allocator is that it has given us pointers relative to mem_base in an
            // incremental way using 'allocsize'.
            for (s32 i = 0; i < 127; ++i)
            {
                void* p1 = fsa->allocate(120 * 1024, sizeof(void*));
                CHECK_NOT_NULL(p1);
                void* pe = (void*)((u64)mem_base + i * allocsize);
                CHECK_EQUAL(pe, p1);
            }
            for (s32 i = 127; i < 1024; ++i)
            {
                void* p1 = fsa->allocate(120 * 1024, sizeof(void*));
                CHECK_NOT_NULL(p1);
                void* pe = (void*)((u64)mem_base + i * allocsize);
                CHECK_EQUAL(pe, p1);
            }

            for (s32 i = 0; i < 1024; ++i)
            {
                void*     p1 = (void*)((u64)mem_base + i * allocsize);
                u32 const s1 = fsa->deallocate(p1);
                CHECK_EQUAL(128 * 1024, s1);
            }

            fsa->release();
        }

        UNITTEST_TEST(create_then_alloc_free_many_256KB_then_release)
        {
            void*     mem_base  = (void*)0x10000000;
            u64 const mem_range = (u64)16 * 1024 * 1024 * 1024;
            u32 const pagesize  = 64 * 1024;
            u32 const allocsize = 256 * 1024;

            xalloc* fsa = create_alloc_fsa_large(gTestAllocator, sNodeHeap, mem_base, mem_range, pagesize, allocsize);

            // The behaviour of the allocator is that it has given us pointers relative to mem_base in an
            // incremental way using 'allocsize'.
            for (s32 i = 0; i < 127; ++i)
            {
                void* p1 = fsa->allocate(180 * 1024, sizeof(void*));
                CHECK_NOT_NULL(p1);
                void* pe = (void*)((u64)mem_base + i * allocsize);
                CHECK_EQUAL(pe, p1);
            }
            for (s32 i = 127; i < 1024; ++i)
            {
                void* p1 = fsa->allocate(180 * 1024, sizeof(void*));
                CHECK_NOT_NULL(p1);
                void* pe = (void*)((u64)mem_base + i * allocsize);
                CHECK_EQUAL(pe, p1);
            }

            for (s32 i = 0; i < 1024; ++i)
            {
                void*     p1 = (void*)((u64)mem_base + i * allocsize);
                u32 const s1 = fsa->deallocate(p1);
                CHECK_EQUAL(192 * 1024, s1);
            }

            fsa->release();
        }

        UNITTEST_TEST(create_then_alloc_free_many_512KB_then_release)
        {
            void*     mem_base  = (void*)0x10000000;
            u64 const mem_range = (u64)16 * 1024 * 1024 * 1024;
            u32 const pagesize  = 64 * 1024;
            u32 const allocsize = 512 * 1024;

            xalloc* fsa = create_alloc_fsa_large(gTestAllocator, sNodeHeap, mem_base, mem_range, pagesize, allocsize);

            // The behaviour of the allocator is that it has given us pointers relative to mem_base in an
            // incremental way using 'allocsize'.
            for (s32 i = 0; i < 127; ++i)
            {
                void* p1 = fsa->allocate(300 * 1024, sizeof(void*));
                CHECK_NOT_NULL(p1);
                void* pe = (void*)((u64)mem_base + i * allocsize);
                CHECK_EQUAL(pe, p1);
            }
            for (s32 i = 127; i < 1024; ++i)
            {
                void* p1 = fsa->allocate(300 * 1024, sizeof(void*));
                CHECK_NOT_NULL(p1);
                void* pe = (void*)((u64)mem_base + i * allocsize);
                CHECK_EQUAL(pe, p1);
            }

            for (s32 i = 0; i < 1024; ++i)
            {
                void*     p1 = (void*)((u64)mem_base + i * allocsize);
                u32 const s1 = fsa->deallocate(p1);
                CHECK_EQUAL(320 * 1024, s1);
            }

            fsa->release();
        }

        UNITTEST_TEST(create_then_alloc_free_many_1MB_then_release)
        {
            void*     mem_base  = (void*)0x10000000;
            u64 const mem_range = (u64)16 * 1024 * 1024 * 1024;
            u32 const pagesize  = 64 * 1024;
            u32 const allocsize = 1 * 1024 * 1024;

            xalloc* fsa = create_alloc_fsa_large(gTestAllocator, sNodeHeap, mem_base, mem_range, pagesize, allocsize);

            // The behaviour of the allocator is that it has given us pointers relative to mem_base in an
            // incremental way using 'allocsize'.
            for (s32 i = 0; i < 31; ++i)
            {
                void* p1 = fsa->allocate(800 * 1024, sizeof(void*));
                CHECK_NOT_NULL(p1);
                void* pe = (void*)((u64)mem_base + i * allocsize);
                CHECK_EQUAL(pe, p1);
            }
            for (s32 i = 31; i < 1024; ++i)
            {
                void* p1 = fsa->allocate(800 * 1024, sizeof(void*));
                CHECK_NOT_NULL(p1);
                void* pe = (void*)((u64)mem_base + i * allocsize);
                CHECK_EQUAL(pe, p1);
            }

            for (s32 i = 0; i < 1024; ++i)
            {
                void*     p1 = (void*)((u64)mem_base + i * allocsize);
                u32 const s1 = fsa->deallocate(p1);
                CHECK_EQUAL(832 * 1024, s1);
            }

            fsa->release();
        }

        UNITTEST_TEST(create_then_alloc_free_many_8MB_then_release)
        {
            void*     mem_base  = (void*)0x10000000;
            u64 const mem_range = (u64)16 * 1024 * 1024 * 1024;
            u32 const pagesize  = 64 * 1024;
            u32 const allocsize = 8 * 1024 * 1024;

            xalloc* fsa = create_alloc_fsa_large(gTestAllocator, sNodeHeap, mem_base, mem_range, pagesize, allocsize);

            // The behaviour of the allocator is that it has given us pointers relative to mem_base in an
            // incremental way using 'allocsize'.
            for (s32 i = 0; i < 31; ++i)
            {
                void* p1 = fsa->allocate(7 * 16 * 64 * 1024, sizeof(void*));
                CHECK_NOT_NULL(p1);
                void* pe = (void*)((u64)mem_base + (u64)i * (u64)allocsize);
                CHECK_EQUAL(pe, p1);
            }
            for (s32 i = 31; i < 1024; ++i)
            {
                void* p1 = fsa->allocate(7 * 16 * 64 * 1024, sizeof(void*));
                CHECK_NOT_NULL(p1);
                void* pe = (void*)((u64)mem_base + (u64)i * (u64)allocsize);
                CHECK_EQUAL(pe, p1);
            }

            for (s32 i = 0; i < 1024; ++i)
            {
                void*     p1 = (void*)((u64)mem_base + (u64)i * (u64)allocsize);
                u32 const s1 = fsa->deallocate(p1);
                CHECK_EQUAL(7 * 16 * 64 * 1024, s1);
            }

            fsa->release();
        }

        UNITTEST_TEST(create_then_alloc_free_many_32MB_then_release)
        {
            void*     mem_base  = (void*)0x10000000;
            u64 const mem_range = (u64)16 * 1024 * 1024 * 1024;
            u32 const pagesize  = 64 * 1024;
            u32 const allocsize = 32 * 1024 * 1024;

            xalloc* fsa = create_alloc_fsa_large(gTestAllocator, sNodeHeap, mem_base, mem_range, pagesize, allocsize);

            // The behaviour of the allocator is that it has given us pointers relative to mem_base in an
            // incremental way using 'allocsize'.
            for (s32 i = 0; i < 15; ++i)
            {
                void* p1 = fsa->allocate(25 * 16 * 64 * 1024, sizeof(void*));
                CHECK_NOT_NULL(p1);
                void* pe = (void*)((u64)mem_base + (u64)i * (u64)allocsize);
                CHECK_EQUAL(pe, p1);
            }
            for (s32 i = 15; i < 256; ++i)
            {
                void* p1 = fsa->allocate(25 * 16 * 64 * 1024, sizeof(void*));
                CHECK_NOT_NULL(p1);
                void* pe = (void*)((u64)mem_base + (u64)i * (u64)allocsize);
                CHECK_EQUAL(pe, p1);
            }

            for (s32 i = 0; i < 256; ++i)
            {
                void*     p1 = (void*)((u64)mem_base + (u64)i * (u64)allocsize);
                u32 const s1 = fsa->deallocate(p1);
                CHECK_EQUAL(25 * 16 * 64 * 1024, s1);
            }

            fsa->release();
        }
    }
}
UNITTEST_SUITE_END
