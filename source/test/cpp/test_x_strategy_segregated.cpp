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

		static const u64 sMemoryRange = (u64)128 * 1024 * 1024 * 1024;
		static const u64 sSpaceRange = (u64)256 * 1024 * 1024;
		static const u64 sMinumSize = (u64)64 * 10 * 1024;
		static const u64 sMaximumSize = (u64)32 * 1024 * 1024;
		static const u64 sStepSize = (u64)2 * 1024 * 1024;
		static const u32 sPageSize = (u64)64 * 1024;

        UNITTEST_FIXTURE_SETUP()
		{
			const u32 sizeof_node = 32;
			const u32 countof_node = 16384;
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
			xsegregatedstrat::xinstance_t* inst = xsegregatedstrat::create(gTestAllocator, mem_base, sMemoryRange, sMinumSize, sMaximumSize, sPageSize);
			xsegregatedstrat::destroy(inst);
		}

	
        UNITTEST_TEST(alloc_dealloc)
		{
			void* mem_base = (void*)0x00ff000000000000ULL;
			xsegregatedstrat::xinstance_t* inst = xsegregatedstrat::create(gTestAllocator, mem_base, sMemoryRange, sMinumSize, sMaximumSize, sPageSize);

			void* p1 = xsegregatedstrat::allocate(inst, sMinumSize, 1024);
			xsegregatedstrat::deallocate(inst, p1);

			xsegregatedstrat::destroy(inst);
		}
	}
}
UNITTEST_SUITE_END
