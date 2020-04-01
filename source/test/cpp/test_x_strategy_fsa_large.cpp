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
            void*     mem_base  = nullptr;
            u64 const mem_range = (u64)16 * 1024 * 1024 * 1024;
            u32 const allocsize = 64 * 1024;

            xfsa_large::xinstance_t* fsa = xfsa_large::create(gTestAllocator, mem_base, mem_range, allocsize);

            xfsa_large::destroy(fsa);
        }

        UNITTEST_TEST(create_then_alloc_then_release)
        {
            void*     mem_base  = nullptr;
            u64 const mem_range = (u64)64 * 1024 * 1024;
            u32 const allocsize = 64 * 1024;

            xfsa_large::xinstance_t* fsa = xfsa_large::create(gTestAllocator, mem_base, mem_range, allocsize);

			void* p1 = xfsa_large::allocate(fsa, 40*1024, sizeof(void*));
			xfsa_large::deallocate(fsa, p1);

            xfsa_large::destroy(fsa);
        }

        UNITTEST_TEST(create_then_alloc_free_many_then_release)
        {
            void*     mem_base  = nullptr;
            u64 const mem_range = (u64)64 * 1024 * 1024;
            u32 const allocsize = 64 * 1024;

            xfsa_large::xinstance_t* fsa = xfsa_large::create(gTestAllocator, mem_base, mem_range, allocsize);

            for (s32 i = 0; i < 1024; ++i)
            {
                u32 const size = 32 + ((i / 64) * 32);
            }

            xfsa_large::destroy(fsa);
        }
    }
}
UNITTEST_SUITE_END
