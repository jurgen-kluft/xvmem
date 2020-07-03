#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xunittest/xunittest.h"

#include "xvmem/private/x_size_db.h"

using namespace xcore;

extern xalloc* gTestAllocator;


UNITTEST_SUITE_BEGIN(size_db)
{
    UNITTEST_FIXTURE(main)
    {
        UNITTEST_FIXTURE_SETUP() {}

        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(init) 
		{
			xsize_db sdb;
			sdb.initialize(gTestAllocator, 250, 4096);
			sdb.release(gTestAllocator);
		}
    }
}
UNITTEST_SUITE_END
