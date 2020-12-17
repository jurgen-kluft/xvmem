#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xunittest/xunittest.h"

#include "xvmem/private/x_addr_db.h"

using namespace xcore;

extern alloc_t* gTestAllocator;

UNITTEST_SUITE_BEGIN(addr_db)
{
    UNITTEST_FIXTURE(main)
    {
        UNITTEST_FIXTURE_SETUP() {}

        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(init) 
		{
			xaddr_db db;
			db.initialize(gTestAllocator, 512 * 1024 * 1024, 4096);
			db.release(gTestAllocator);
		}
    }
}
UNITTEST_SUITE_END
