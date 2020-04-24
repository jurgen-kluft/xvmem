#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_doubly_linked_list.h"

#include "xunittest/xunittest.h"

using namespace xcore;

extern xalloc* gTestAllocator;

xalist_t::node_t* gCreateList(u32 count)
{
	xalist_t::node_t* list = (xalist_t::node_t*)gTestAllocator->allocate(sizeof(xalist_t::node_t) * count);
	return list;
}

void gDestroyList(xalist_t::node_t* list)
{
	gTestAllocator->deallocate(list);
}


UNITTEST_SUITE_BEGIN(doubly_linked_list)
{
    UNITTEST_FIXTURE(main)
    {
        UNITTEST_FIXTURE_SETUP() {}

        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(init) 
		{
			xalist_t::node_t* list_data = gCreateList(1024);
			xalist_t list;

			CHECK_TRUE(list.is_empty());
			CHECK_EQUAL(0, list.m_count);
			CHECK_EQUAL(xalist_t::NIL, list.m_head);

			gDestroyList(list_data);
		}

    }
}
UNITTEST_SUITE_END
