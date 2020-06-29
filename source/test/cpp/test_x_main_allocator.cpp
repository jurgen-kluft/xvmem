#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/x_virtual_main_allocator.h"
#include "xvmem/x_virtual_memory.h"

#include "xunittest/xunittest.h"

using namespace xcore;

extern xalloc* gTestAllocator;

class xvmem_test : public xvmem
{
public:
    xalloc* m_main_allocator;

    u32   m_index;
    void* m_mem_address[8];
    u64   m_mem_range[8];
    u32   m_page_size;

    void init(xalloc* main_allocator, u32 page_size)
    {
        m_main_allocator = main_allocator;
        m_page_size      = page_size;

        // The first one is for FSA and is intrusive so we need actual memory
        m_index          = 0;
        m_mem_range[0]   = 32 * 1024 * 1024;
        m_mem_address[0] = gTestAllocator->allocate((u32)m_mem_range[0], 512 * 1024 * 1024);
        m_mem_range[1]   = 4 * 1024 * 1024;
        m_mem_address[1] = gTestAllocator->allocate((u32)m_mem_range[1], 32 * 1024 * 1024);
        m_mem_range[2]   = 4 * 1024 * 1024;
        m_mem_address[2] = gTestAllocator->allocate((u32)m_mem_range[2], 32 * 1024 * 1024);
        m_mem_range[3]   = 128 * 1024 * 1024;
        m_mem_address[3] = (void*)((u64)m_mem_address[0] + m_mem_range[3]);
        m_mem_range[4]   = 128 * 1024 * 1024;
        m_mem_address[4] = (void*)((u64)m_mem_address[1] + m_mem_range[4]);
        for (s32 i = 5; i < 7; i++)
        {
            m_mem_range[i]   = ((u64)128 * 1024 * 1024 * 1024);
            m_mem_address[i] = (void*)((u64)m_mem_address[i - 1] + m_mem_range[i - 1]);
        }
    }

    void exit() { m_main_allocator->deallocate(m_mem_address[0]); }

    virtual bool initialize(u32 pagesize) { return true; }

    virtual bool reserve(u64 address_range, u32& page_size, u32 attributes, void*& baseptr)
    {
        ASSERT(m_index < 8);
        page_size = m_page_size;
        baseptr   = m_mem_address[m_index];
        m_index += 1;
        return true;
    }

    virtual bool release(void* baseptr, u64 address_range) { return true; }
    virtual bool commit(void* address, u32 page_size, u32 page_count) { return true; }
    virtual bool decommit(void* address, u32 page_size, u32 page_count) { return true; }
};

class xalloc_with_stats : public xalloc
{
    xalloc* mAllocator;
    u32     mNumAllocs;
    u32     mNumDeallocs;
    u64     mMemoryAllocated;
    u64     mMemoryDeallocated;

public:
    xalloc_with_stats()
        : mAllocator(nullptr)
    {
        mNumAllocs         = 0;
        mNumDeallocs       = 0;
        mMemoryAllocated   = 0;
        mMemoryDeallocated = 0;
    }

    void init(xalloc* allocator) { mAllocator = allocator; }

    virtual void* v_allocate(u32 size, u32 alignment)
    {
        mNumAllocs++;
        mMemoryAllocated += size;
        return mAllocator->allocate(size, alignment);
    }

    virtual u32 v_deallocate(void* mem)
    {
        mNumDeallocs++;
        u32 const size = mAllocator->deallocate(mem);
        mMemoryDeallocated += size;
        return size;
    }

    virtual void v_release()
    {
        mAllocator->release();
        mAllocator = NULL;
    }
};

struct xvmem_testconfig : public xvmem_config
{
    xvmem_testconfig()
    {
        m_node16_heap_size     = MB(4);    // Size of node heap for sizeof(node)==16
        m_node32_heap_size     = MB(4);    // Size of node heap for sizeof(node)==32
        m_fsa_mem_range        = MBx(32);  // The virtual memory range for the FSA allocator
        m_med_count            = 2;        //
        m_med_mem_range[0]     = MB(128);  //
        m_med_min_size[0]      = KB(4);    // Medium-Size-Allocator; Minimum allocation size
        m_med_step_size[0]     = 256;      //
        m_med_max_size[0]      = KB(64);   // Medium-Size-Allocator; Maximum allocation size
        m_med_addr_node_cnt[0] = 1024;     // Medium-Size-Allocator; Number of address nodes to split the memory range
        m_med_region_size[0]   = MB(1);    // Medium-Size-Allocator; Size of commit/decommit regions
        m_med_region_cached[0] = 50;       // Keep 50 medium size regions cached
        m_med_mem_range[1]     = MB(128);  // MedLarge-Size-Allocator; The virtual memory range
        m_med_min_size[1]      = KB(64);   // MedLarge-Size-Allocator; Minimum allocation size
        m_med_step_size[1]     = KB(2);    //
        m_med_max_size[1]      = KB(512);  // MedLarge-Size-Allocator; Maximum allocation size
        m_med_addr_node_cnt[1] = 128;      // Medium-Size-Allocator; Number of address nodes to split the memory range
        m_med_region_size[1]   = MB(8);    // MedLarge-Size-Allocator; Size of commit/decommit regions
        m_med_region_cached[1] = 8;        // Keep 8 medium-2-large regions cached
        m_seg_min_size         = KB(512);  //
        m_seg_max_size         = MB(32);   //
        m_seg_mem_range        = GBx(128); //
        m_large_min_size       = MB(32);   //
        m_large_mem_range      = GBx(128); //
        m_large_max_allocs     = 64;       // Maximum number of large allocations
    }
};

UNITTEST_SUITE_BEGIN(main_allocator)
{
    UNITTEST_FIXTURE(main)
    {
        xalloc_with_stats s_alloc;
        xvmem_testconfig  s_vmem_testconfig;
        xvmem_test        s_vmem_test;

        UNITTEST_FIXTURE_SETUP()
        {
            s_alloc.init(gTestAllocator);
            s_vmem_test.init(gTestAllocator, 64 * 1024);
        }

        UNITTEST_FIXTURE_TEARDOWN() { s_vmem_test.exit(); }

        UNITTEST_TEST(test_config)
        {
			const char* reason = nullptr;
			bool const valid = s_vmem_testconfig.validate(reason);
			CHECK_EQUAL_T(true, valid, reason);
		}

        UNITTEST_TEST(init)
        {
            xalloc* allocator = gCreateVmAllocator(&s_alloc, &s_vmem_test, &s_vmem_testconfig);
            allocator->release();
        }
    }
}
UNITTEST_SUITE_END
