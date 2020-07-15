#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_page_vcd_regions_cached.h"
#include "xvmem/x_virtual_memory.h"

#include "xunittest/xunittest.h"

using namespace xcore;

extern xalloc* gTestAllocator;

UNITTEST_SUITE_BEGIN(strategy_page_vcd_regions_cached)
{
    class xvmem_test_vcd : public xvmem
    {
    public:
        xalloc* m_main_allocator;
        void*   m_mem_address;
        u64     m_mem_range;
        u32     m_page_size;
        u32     m_num_pages;
        u32     m_commit_cnt;
        u32     m_decommit_cnt;

        void init(xalloc* main_allocator, u64 mem_range, u32 page_size)
        {
            m_main_allocator = main_allocator;
            m_page_size      = page_size;
            m_num_pages      = (u32)(mem_range / m_page_size);
            xbyte* mem       = (xbyte*)main_allocator->allocate(m_page_size * m_num_pages, sizeof(void*));
            m_mem_address    = mem;
            m_commit_cnt     = 0;
            m_decommit_cnt   = 0;
        }

        void exit() { m_main_allocator->deallocate(m_mem_address); }

        virtual bool initialize(u32 pagesize) { return true; }
        virtual bool reserve(u64 address_range, u32& page_size, u32 attributes, void*& baseptr)
        {
            page_size = m_page_size;
            baseptr   = m_mem_address;
            return true;
        }

        virtual bool release(void* baseptr, u64 address_range) { return true; }
        virtual bool commit(void* address, u32 page_size, u32 page_count)
        {
            m_commit_cnt++;
            return true;
        }
        virtual bool decommit(void* address, u32 page_size, u32 page_count)
        {
            m_decommit_cnt++;
            return true;
        }
    };

    class xalloc_test : public xalloc
    {
    public:
        void* m_mem_base;
        u32   m_page_size;
        u64   m_mem_range;
        u64   m_mem_cursor;
        u32   m_alloc_count;
        u32   m_dealloc_count;

		void* m_allocation_ptrs[256];
		u32 m_allocation_sizes[256];

        void reset(void* mem_base, u32 page_size)
        {
            m_mem_base      = mem_base;
            m_page_size     = page_size;
            m_mem_range     = (u64)32 * 1024 * 1024 * 1024;
            m_mem_cursor    = 0;
            m_alloc_count   = 0;
            m_dealloc_count = 0;
			x_memset(m_allocation_ptrs, 0, sizeof(void*) * 256);
			x_memset(m_allocation_sizes, 0, sizeof(u32) * 256);
        }

        virtual void* v_allocate(u32 size, u32 alignment)
        {
			u64 const mem_cursor = m_mem_cursor;
            m_alloc_count += 1;
            m_mem_cursor += (size + (m_page_size - 1)) & ~(m_page_size - 1);
            void* ptr = x_advance_ptr(m_mem_base, mem_cursor);

			s32 i = 0; 
			while (m_allocation_ptrs[i] != nullptr)
			{
				ASSERT(i < 256);
			}
			m_allocation_ptrs[i] = ptr;
			m_allocation_sizes[i] = size;

			return ptr;
        }

        virtual u32 v_deallocate(void* ptr)
        {
            m_dealloc_count += 1;

			s32 i = 0; 
			while (m_allocation_ptrs[i] != ptr)
			{
				ASSERT(i < 256);
			}
			u32 const size = m_allocation_sizes[i];
			m_allocation_sizes[i] = 0;
			m_allocation_ptrs[i] = nullptr;
            return size;
        }

        virtual void v_release() {}
    };

    UNITTEST_FIXTURE(main)
    {
        xvmem_test_vcd vmem;
        xalloc_test    alloc_test;
        void* const    mem_base  = (void*)0x10000000;
        u64 const      mem_range = (u64)32 * 1024 * 1024 * 1024;
        u32 const      page_size = 64 * 1024;

        UNITTEST_FIXTURE_SETUP()
        {
            alloc_test.reset(mem_base, page_size);
            vmem.init(gTestAllocator, (u64)64 * 1024 * 1024, page_size);
        }
        UNITTEST_FIXTURE_TEARDOWN() { vmem.exit(); }

        UNITTEST_TEST(init)
        {
            xalloc* page_vcd = create_page_vcd_regions_cached(gTestAllocator, &alloc_test, &vmem, mem_base, mem_range, page_size, (u64)2 * 1024 * 1024, 4);
            page_vcd->release();
            alloc_test.reset(mem_base, page_size);
        }

        UNITTEST_TEST(alloc_dealloc)
        {
            xalloc* page_vcd = create_page_vcd_regions_cached(gTestAllocator, &alloc_test, &vmem, mem_base, mem_range, page_size, (u64)2 * 1024 * 1024, 4);

            void* p1 = page_vcd->allocate(128 * 1024);
            CHECK_EQUAL(mem_base, p1);

            u32 const s1 = page_vcd->deallocate(p1);
            CHECK_EQUAL(128 * 1024, s1);

            CHECK_EQUAL(alloc_test.m_dealloc_count, 1);
            CHECK_EQUAL(vmem.m_commit_cnt, 1);
            CHECK_EQUAL(alloc_test.m_dealloc_count, alloc_test.m_alloc_count);

            CHECK_EQUAL(1, vmem.m_commit_cnt);
            CHECK_EQUAL(0, vmem.m_decommit_cnt);

			page_vcd->release();

            CHECK_EQUAL(1, vmem.m_commit_cnt);
            CHECK_EQUAL(1, vmem.m_decommit_cnt);

            alloc_test.reset(mem_base, page_size);
        }
    }
}
UNITTEST_SUITE_END
