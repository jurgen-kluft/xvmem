#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/x_virtual_main_allocator.h"
#include "xvmem/x_virtual_memory.h"

#include "xunittest/xunittest.h"

using namespace xcore;

extern alloc_t* gTestAllocator;
class xalloc_with_stats : public alloc_t
{
    alloc_t* mAllocator;
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

    void init(alloc_t* allocator) { mAllocator = allocator; }

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

        UNITTEST_FIXTURE_TEARDOWN()
		{
			s_vmem_test.exit(); 
		}

        UNITTEST_TEST(test_config)
        {
			const char* reason = nullptr;
			bool const valid = s_vmem_testconfig.validate(reason);
			CHECK_EQUAL_T(true, valid, reason);
		}

        UNITTEST_TEST(init)
        {
            alloc_t* allocator = gCreateVmAllocator(&s_alloc, &s_vmem_test, &s_vmem_testconfig);

			s_vmem_test.reset();
        }
    }
}
UNITTEST_SUITE_END
