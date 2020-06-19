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

	u32     m_index;
    void*   m_mem_address[8];
    u64     m_mem_range[8];
    u32     m_page_size;


    void init(xalloc* main_allocator, u32 page_size)
    {
        m_main_allocator = main_allocator;
        m_page_size      = page_size;

		// The first one is for FSA
		m_index = 0;
		m_mem_range[0] = 512 * 1024 * 1024;
		m_mem_address[0] = gTestAllocator->allocate((u32)m_mem_range[0], 64*1024);
        for (s32 i=1; i<8; i++)
		{
			m_mem_range[i] = ((u64)i * 64 * 1024 * 1024 * 1024);
			m_mem_address[i] = (void*)((u64)m_mem_address[0] + m_mem_range[i]);
		}
    }

    void exit() 
	{
		m_main_allocator->deallocate(m_mem_address[0]); 
	}

    virtual bool initialize(u32 pagesize) { return true; }

    virtual bool reserve(u64 address_range, u32& page_size, u32 attributes, void*& baseptr)
    {
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
		u32 mNumAllocs;
		u32 mNumDeallocs;
		u64 mMemoryAllocated;
		u64 mMemoryDeallocated;

    public:
        xalloc_with_stats()
            : mAllocator(nullptr)
        {
			mNumAllocs = 0;
			mNumDeallocs = 0;
			mMemoryAllocated = 0;
			mMemoryDeallocated = 0;
        }

		void init(xalloc* allocator)
		{
			mAllocator = allocator;
		}

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

UNITTEST_SUITE_BEGIN(main_allocator)
{
    UNITTEST_FIXTURE(main)
    {
		xalloc_with_stats s_alloc;
		xvmem_test s_vmem_test;

        UNITTEST_FIXTURE_SETUP() 
		{
			s_alloc.init(gTestAllocator);
			s_vmem_test.init(gTestAllocator, 64 * 1024);
		}

        UNITTEST_FIXTURE_TEARDOWN() 
		{
			s_vmem_test.exit();
		}

        UNITTEST_TEST(init) 
		{
			xalloc* allocator = gCreateVmAllocator(&s_alloc, &s_vmem_test);
			allocator->release();
		}

    }
}
UNITTEST_SUITE_END
