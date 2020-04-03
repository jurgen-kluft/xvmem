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

        UNITTEST_FIXTURE_SETUP() {}

        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(create_then_release)
        {
            void*     mem_base  = (void*)0x10000000;
            u64 const mem_range = (u64)16 * 1024 * 1024 * 1024;
			u32 const pagesize = 64 * 1024;
            u32 const allocsize = 64 * 1024;

            xfsa_large::xinstance_t* fsa = xfsa_large::create(gTestAllocator, mem_base, mem_range, pagesize, allocsize);

            xfsa_large::destroy(fsa);
        }

        UNITTEST_TEST(create_then_alloc_then_release)
        {
            void*     mem_base  = (void*)0x10000000;
            u64 const mem_range = (u64)16 * 1024 * 1024 * 1024;
			u32 const pagesize = 64 * 1024;
            u32 const allocsize = 64 * 1024;

            xfsa_large::xinstance_t* fsa = xfsa_large::create(gTestAllocator, mem_base, mem_range, pagesize, allocsize);

			void* p1 = xfsa_large::allocate(fsa, 40*1024, sizeof(void*));
			CHECK_NOT_NULL(p1);
			u32 const s1 = xfsa_large::deallocate(fsa, p1);
			CHECK_EQUAL(64*1024, s1);

            xfsa_large::destroy(fsa);
        }

        UNITTEST_TEST(create_then_alloc_free_many_64KB_then_release)
        {
            void*     mem_base  = (void*)0x10000000;
            u64 const mem_range = (u64)16 * 1024 * 1024 * 1024;
			u32 const pagesize = 64 * 1024;
            u32 const allocsize = 64 * 1024;

            xfsa_large::xinstance_t* fsa = xfsa_large::create(gTestAllocator, mem_base, mem_range, pagesize, allocsize);

			// The behaviour of the allocator is that it has given us pointers relative to mem_base in an
			// incremental way using 'allocsize'.
            for (s32 i = 0; i < 255; ++i)
            {
				void* p1 = xfsa_large::allocate(fsa, 40*1024, sizeof(void*));
				CHECK_NOT_NULL(p1);
				void* pe = (void*)((u64)mem_base + i * allocsize);
				CHECK_EQUAL(pe, p1);
            }
            for (s32 i = 255; i < 1024; ++i)
            {
				void* p1 = xfsa_large::allocate(fsa, 40*1024, sizeof(void*));
				CHECK_NOT_NULL(p1);
				void* pe = (void*)((u64)mem_base + i * allocsize);
				CHECK_EQUAL(pe, p1);
            }
            for (s32 i = 0; i < 1024; ++i)
            {
				void* p1 = (void*)((u64)mem_base + i * allocsize);
				u32 const s1 = xfsa_large::deallocate(fsa, p1);
				CHECK_EQUAL(64*1024, s1);
            }

            xfsa_large::destroy(fsa);
        }

		UNITTEST_TEST(create_then_alloc_free_many_128KB_then_release)
        {
            void*     mem_base  = (void*)0x10000000;
            u64 const mem_range = (u64)16 * 1024 * 1024 * 1024;
			u32 const pagesize = 64 * 1024;
            u32 const allocsize = 128 * 1024;

            xfsa_large::xinstance_t* fsa = xfsa_large::create(gTestAllocator, mem_base, mem_range, pagesize, allocsize);

			// The behaviour of the allocator is that it has given us pointers relative to mem_base in an
			// incremental way using 'allocsize'.
            for (s32 i = 0; i < 127; ++i)
            {
				void* p1 = xfsa_large::allocate(fsa, 120*1024, sizeof(void*));
				CHECK_NOT_NULL(p1);
				void* pe = (void*)((u64)mem_base + i * allocsize);
				CHECK_EQUAL(pe, p1);
            }
            for (s32 i = 127; i < 1024; ++i)
            {
				void* p1 = xfsa_large::allocate(fsa, 120*1024, sizeof(void*));
				CHECK_NOT_NULL(p1);
				void* pe = (void*)((u64)mem_base + i * allocsize);
				CHECK_EQUAL(pe, p1);
            }

			for (s32 i = 0; i < 1024; ++i)
            {
				void* p1 = (void*)((u64)mem_base + i * allocsize);
				u32 const s1 = xfsa_large::deallocate(fsa, p1);
				CHECK_EQUAL(128*1024, s1);
            }

            xfsa_large::destroy(fsa);
        }

		UNITTEST_TEST(create_then_alloc_free_many_256KB_then_release)
        {
            void*     mem_base  = (void*)0x10000000;
            u64 const mem_range = (u64)16 * 1024 * 1024 * 1024;
			u32 const pagesize = 64 * 1024;
            u32 const allocsize = 256 * 1024;

            xfsa_large::xinstance_t* fsa = xfsa_large::create(gTestAllocator, mem_base, mem_range, pagesize, allocsize);

			// The behaviour of the allocator is that it has given us pointers relative to mem_base in an
			// incremental way using 'allocsize'.
            for (s32 i = 0; i < 127; ++i)
            {
				void* p1 = xfsa_large::allocate(fsa, 180*1024, sizeof(void*));
				CHECK_NOT_NULL(p1);
				void* pe = (void*)((u64)mem_base + i * allocsize);
				CHECK_EQUAL(pe, p1);
            }
            for (s32 i = 127; i < 1024; ++i)
            {
				void* p1 = xfsa_large::allocate(fsa, 180*1024, sizeof(void*));
				CHECK_NOT_NULL(p1);
				void* pe = (void*)((u64)mem_base + i * allocsize);
				CHECK_EQUAL(pe, p1);
            }

			for (s32 i = 0; i < 1024; ++i)
            {
				void* p1 = (void*)((u64)mem_base + i * allocsize);
				u32 const s1 = xfsa_large::deallocate(fsa, p1);
				CHECK_EQUAL(192*1024, s1);
            }

            xfsa_large::destroy(fsa);
        }

		UNITTEST_TEST(create_then_alloc_free_many_512KB_then_release)
        {
            void*     mem_base  = (void*)0x10000000;
            u64 const mem_range = (u64)16 * 1024 * 1024 * 1024;
			u32 const pagesize = 64 * 1024;
            u32 const allocsize = 512 * 1024;

            xfsa_large::xinstance_t* fsa = xfsa_large::create(gTestAllocator, mem_base, mem_range, pagesize, allocsize);

			// The behaviour of the allocator is that it has given us pointers relative to mem_base in an
			// incremental way using 'allocsize'.
            for (s32 i = 0; i < 127; ++i)
            {
				void* p1 = xfsa_large::allocate(fsa, 300*1024, sizeof(void*));
				CHECK_NOT_NULL(p1);
				void* pe = (void*)((u64)mem_base + i * allocsize);
				CHECK_EQUAL(pe, p1);
            }
            for (s32 i = 127; i < 1024; ++i)
            {
				void* p1 = xfsa_large::allocate(fsa, 300*1024, sizeof(void*));
				CHECK_NOT_NULL(p1);
				void* pe = (void*)((u64)mem_base + i * allocsize);
				CHECK_EQUAL(pe, p1);
            }

			for (s32 i = 0; i < 1024; ++i)
            {
				void* p1 = (void*)((u64)mem_base + i * allocsize);
				u32 const s1 = xfsa_large::deallocate(fsa, p1);
				CHECK_EQUAL(320*1024, s1);
            }

            xfsa_large::destroy(fsa);
        }

		UNITTEST_TEST(create_then_alloc_free_many_1MB_then_release)
        {
            void*     mem_base  = (void*)0x10000000;
            u64 const mem_range = (u64)16 * 1024 * 1024 * 1024;
			u32 const pagesize = 64 * 1024;
            u32 const allocsize = 1 * 1024 * 1024;

            xfsa_large::xinstance_t* fsa = xfsa_large::create(gTestAllocator, mem_base, mem_range, pagesize, allocsize);

			// The behaviour of the allocator is that it has given us pointers relative to mem_base in an
			// incremental way using 'allocsize'.
            for (s32 i = 0; i < 31; ++i)
            {
				void* p1 = xfsa_large::allocate(fsa, 800*1024, sizeof(void*));
				CHECK_NOT_NULL(p1);
				void* pe = (void*)((u64)mem_base + i * allocsize);
				CHECK_EQUAL(pe, p1);
            }
            for (s32 i = 31; i < 1024; ++i)
            {
				void* p1 = xfsa_large::allocate(fsa, 800*1024, sizeof(void*));
				CHECK_NOT_NULL(p1);
				void* pe = (void*)((u64)mem_base + i * allocsize);
				CHECK_EQUAL(pe, p1);
            }

			for (s32 i = 0; i < 1024; ++i)
            {
				void* p1 = (void*)((u64)mem_base + i * allocsize);
				u32 const s1 = xfsa_large::deallocate(fsa, p1);
				CHECK_EQUAL(832*1024, s1);
            }

            xfsa_large::destroy(fsa);
        }

	}
}
UNITTEST_SUITE_END
