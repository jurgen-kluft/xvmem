#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_doubly_linked_list.h"

#include "xunittest/xunittest.h"

using namespace xcore;

extern xalloc* gTestAllocator;

llnode* gCreateList(u32 count)
{
	llnode* list = (llnode*)gTestAllocator->allocate(sizeof(llnode) * count);
	return list;
}

void gDestroyList(llnode* list)
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
			llnode* list_data = gCreateList(1024);
			llist list(0, 1024);

			CHECK_TRUE(list.is_empty());
			CHECK_EQUAL(0, list.size());
			CHECK_TRUE(list.m_head.is_nil());

			gDestroyList(list_data);
		}

        UNITTEST_TEST(insert_1) 
		{
			llnode* list_data = gCreateList(1024);
			llist list(0, 1024);

			CHECK_TRUE(list.is_empty());
			CHECK_EQUAL(0, list.size());
			CHECK_TRUE(list.m_head.is_nil());

			list.insert(list_data, 0);

			CHECK_FALSE(list.is_empty());
			CHECK_EQUAL(1, list.size());
			CHECK_FALSE(list.m_head.is_nil());

			llnode* node = list.idx2node(list_data, 0);
			CHECK_TRUE(node->is_linked());
			CHECK_EQUAL(0, node->m_next);
			CHECK_EQUAL(0, node->m_prev);

			gDestroyList(list_data);
		}

        UNITTEST_TEST(insert_1_remove_head) 
		{
			llnode* list_data = gCreateList(1024);
			llist list(0, 1024);

			CHECK_TRUE(list.is_empty());
			CHECK_EQUAL(0, list.size());
			CHECK_TRUE(list.m_head.is_nil());

			list.insert(list_data, 0);

			CHECK_FALSE(list.is_empty());
			CHECK_EQUAL(1, list.size());
			CHECK_FALSE(list.m_head.is_nil());

			llnode* node = list.remove_head(list_data);

			CHECK_TRUE(list.is_empty());
			CHECK_EQUAL(0, list.size());
			CHECK_TRUE(list.m_head.is_nil());

			CHECK_FALSE(node->is_linked());
			CHECK_TRUE(node->m_next.is_nil());
			CHECK_TRUE(node->m_prev.is_nil());

			gDestroyList(list_data);
		}

        UNITTEST_TEST(insert_N_remove_head) 
		{
			llnode* list_data = gCreateList(1024);
			llist list(0, 1024);

			CHECK_TRUE(list.is_empty());
			CHECK_EQUAL(0, list.size());
			CHECK_TRUE(list.m_head.is_nil());

			const s32 count = 256;
			for (s32 i=0; i<count; ++i)
			{
				list.insert(list_data, i);
			}

			CHECK_FALSE(list.is_empty());
			CHECK_EQUAL(count, list.size());
			CHECK_FALSE(list.m_head.is_nil());

			for (s32 i=0; i<count; ++i)
			{
				llnode* node = list.remove_head(list_data);
				CHECK_FALSE(node->is_linked());
				CHECK_TRUE(node->m_next.is_nil());
				CHECK_TRUE(node->m_prev.is_nil());
			}

			CHECK_TRUE(list.is_empty());
			CHECK_EQUAL(0, list.size());
			CHECK_TRUE(list.m_head.is_nil());

			gDestroyList(list_data);
		}

        UNITTEST_TEST(insert_N_remove_tail) 
		{
			llnode* list_data = gCreateList(1024);
			llist list(0, 1024);

			CHECK_TRUE(list.is_empty());
			CHECK_EQUAL(0, list.size());
			CHECK_TRUE(list.m_head.is_nil());

			const s32 count = 256;
			for (s32 i=0; i<count; ++i)
			{
				list.insert(list_data, i);
			}

			CHECK_FALSE(list.is_empty());
			CHECK_EQUAL(count, list.size());
			CHECK_FALSE(list.m_head.is_nil());

			for (s32 i=0; i<count; ++i)
			{
				llnode* node = list.remove_tail(list_data);
				CHECK_FALSE(node->is_linked());
				CHECK_TRUE(node->m_next.is_nil());
				CHECK_TRUE(node->m_prev.is_nil());
			}

			CHECK_TRUE(list.is_empty());
			CHECK_EQUAL(0, list.size());
			CHECK_TRUE(list.m_head.is_nil());

			gDestroyList(list_data);
		}

        UNITTEST_TEST(insert_N_remove_item) 
		{
			llnode* list_data = gCreateList(1024);
			llist list(0, 1024);

			CHECK_TRUE(list.is_empty());
			CHECK_EQUAL(0, list.size());
			CHECK_TRUE(list.m_head.is_nil());

			const s32 count = 256;
			for (s32 i=0; i<count; ++i)
			{
				list.insert(list_data, i);
			}

			CHECK_FALSE(list.is_empty());
			CHECK_EQUAL(count, list.size());
			CHECK_FALSE(list.m_head.is_nil());

			for (s32 i=0; i<count; ++i)
			{
				llnode* node = list.remove_item(list_data, i);
				CHECK_FALSE(node->is_linked());
				CHECK_TRUE(node->m_next.is_nil());
				CHECK_TRUE(node->m_prev.is_nil());
			}

			CHECK_TRUE(list.is_empty());
			CHECK_EQUAL(0, list.size());
			CHECK_TRUE(list.m_head.is_nil());

			gDestroyList(list_data);
		}

    }
}
UNITTEST_SUITE_END
