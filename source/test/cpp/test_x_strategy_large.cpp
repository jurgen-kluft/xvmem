#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_large.h"

#include "xunittest/xunittest.h"

using namespace xcore;

extern xalloc* gTestAllocator;

UNITTEST_SUITE_BEGIN(strategy_large)
{
    UNITTEST_FIXTURE(main)
    {
        static const u64 sMemoryRange = (u64)128 * 1024 * 1024 * 1024;
        static const u64 sSpaceRange  = (u64)256 * 1024 * 1024;
        static const u64 sMinumSize   = (u64)32 * 1024 * 1024;
        static const u64 sMaximumSize = (u64)512 * 1024 * 1024;
        static const u32 sPageSize    = (u64)64 * 1024;

        UNITTEST_FIXTURE_SETUP() {}

        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(init)
        {
            void*                     mem_base = (void*)0x00ff000000000000ULL;
            xlargestrat::xinstance_t* inst     = xlargestrat::create(gTestAllocator, mem_base, sMemoryRange, sMinumSize, sMemoryRange / sMaximumSize);
            xlargestrat::destroy(inst);
        }

        UNITTEST_TEST(alloc_dealloc)
        {
            void*                     mem_base = (void*)0x00ff000000000000ULL;
            xlargestrat::xinstance_t* inst     = xlargestrat::create(gTestAllocator, mem_base, sMemoryRange, sMinumSize, sMemoryRange / sMaximumSize);

            void* p1 = xlargestrat::allocate(inst, sMinumSize, 1024);
            xlargestrat::deallocate(inst, p1);

            xlargestrat::destroy(inst);
        }
    }
}
UNITTEST_SUITE_END
