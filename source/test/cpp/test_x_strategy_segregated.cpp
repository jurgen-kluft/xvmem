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

        UNITTEST_FIXTURE_SETUP() {}

        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(init) {}
    }
}
UNITTEST_SUITE_END
