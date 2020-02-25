#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_segregated.h"

#include "xunittest/xunittest.h"

using namespace xcore;

extern xalloc* gTestAllocator;

UNITTEST_SUITE_BEGIN(strategy_segregated)
{
    UNITTEST_FIXTURE(main)
    {
		static void* sNodeData = nullptr;
		static xfsadexed* sNodeHeap = nullptr;

        UNITTEST_FIXTURE_SETUP()
		{
			const s32 sizeof_node = 32;
			const s32 countof_node = 16384;
			sNodeData = gTestAllocator->allocate(sizeof_node * countof_node, 8);
			sNodeHeap = gTestAllocator->construct<xfsadexed_array>(sNodeData, sizeof_node, countof_node);
		}

        UNITTEST_FIXTURE_TEARDOWN()
		{
			gTestAllocator->deallocate(sNodeData);
			gTestAllocator->destruct(sNodeHeap);
		}

        UNITTEST_TEST(init)
		{
			void* mem_base = (void*)0x00ff000000000000ULL;
			xsegregatedstrat::xinstance_t* segstrat = xsegregatedstrat::create(gTestAllocator, sNodeHeap, mem_base, (u64)128 * 1024 * 1024 * 1024, (u64)1 * 1024 * 1024 * 1024, 10* 64*1024, 32 * 1024 * 1024, 1 * 1024 * 1024, 64*1024);
			xsegregatedstrat::destroy(segstrat);
		}
    }
}
UNITTEST_SUITE_END
