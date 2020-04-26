#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_page_vcd_direct.h"
#include "xvmem/x_virtual_memory.h"

#include "xunittest/xunittest.h"

using namespace xcore;

extern xalloc* gTestAllocator;

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
    u64   m_mem_range;
    u32   m_alloc_count;
    u32   m_dealloc_count;

    virtual void* v_allocate(u32 size, u32 alignment)
    {
        m_alloc_count += 1;
        return m_mem_base;
    }

    virtual u32 v_deallocate(void* ptr)
    {
        m_dealloc_count += 1;
        return 0;
    }

    virtual void v_release() {}
};

UNITTEST_SUITE_BEGIN(strategy_page_vcd_direct)
{
    UNITTEST_FIXTURE(main)
    {
        xvmem_test_vcd  vmem;
        xalloc_test alloc_test;
        void* const mem_base  = (void*)0x10000000;
        u32 const   page_size = 64 * 1024;

        UNITTEST_FIXTURE_SETUP()
        {
            alloc_test.m_mem_base      = mem_base;
            alloc_test.m_mem_range     = (u64)32 * 1024 * 1024 * 1024;
            alloc_test.m_alloc_count   = 0;
            alloc_test.m_dealloc_count = 0;

            vmem.init(gTestAllocator, (u64)64 * 1024 * 1024, page_size);
        }
        UNITTEST_FIXTURE_TEARDOWN() { vmem.exit(); }

        UNITTEST_TEST(init)
        {
            xalloc* page_vcd = create_page_vcd_direct(gTestAllocator, &alloc_test, &vmem, page_size);
            page_vcd->release();
        }

        UNITTEST_TEST(alloc_dealloc)
        {
            xalloc* page_vcd = create_page_vcd_direct(gTestAllocator, &alloc_test, &vmem, page_size);

            void* p1 = page_vcd->allocate(128 * 1024);
            CHECK_EQUAL(mem_base, p1);
            u32 const s1 = page_vcd->deallocate(p1);
            //CHECK_EQUAL(128 * 1024, s1);
            CHECK_EQUAL(alloc_test.m_dealloc_count, 1);
            CHECK_EQUAL(vmem.m_commit_cnt, 1);
            CHECK_EQUAL(alloc_test.m_dealloc_count, alloc_test.m_alloc_count);
            CHECK_EQUAL(vmem.m_commit_cnt, vmem.m_decommit_cnt);

            page_vcd->release();
        }
    }
}
UNITTEST_SUITE_END
