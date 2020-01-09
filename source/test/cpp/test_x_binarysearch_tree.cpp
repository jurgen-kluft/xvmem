#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_binarysearch_tree.h"

#include "xunittest/xunittest.h"

using namespace xcore;

extern xalloc* gSystemAllocator;

namespace xcore
{
	struct mynode
	{
		float m_float;
		s32 m_integer;

		void* m_key;
		s32 m_bstnode_color;
		xbst::pointer_based::node_t	m_bstnode;
	};

    u64 get_key_mynode_f(const xbst::pointer_based::node_t* lhs)
	{
		mynode const* n = (mynode const*)((const xbyte*)lhs - X_OFFSET_OF(mynode, m_bstnode));
		return (u64)&n->m_key;
	}

	s32 compare_mynode_f(const u64 pkey, const xbst::pointer_based::node_t* node)
	{
		mynode* n = (mynode*)((xbyte*)node - X_OFFSET_OF(mynode, m_bstnode));
		void* key = *(void**)pkey;
		if (key < n->m_key)
			return -1;
		if (key > n->m_key)
			return 1;
		return 0;
	}

    s32 get_color_mynode_f(const xbst::pointer_based::node_t* lhs)
	{
		mynode const* n = (mynode const*)((const xbyte*)lhs - X_OFFSET_OF(mynode, m_bstnode));
		return n->m_bstnode_color;
	}

    void set_color_mynode_f(xbst::pointer_based::node_t* lhs, s32 color)
	{
		mynode* n = (mynode*)((xbyte*)lhs - X_OFFSET_OF(mynode, m_bstnode));
		n->m_bstnode_color = color;
	}

}

UNITTEST_SUITE_BEGIN(binarysearch_tree)
{
    UNITTEST_FIXTURE(main)
    {
        UNITTEST_FIXTURE_SETUP()
		{
		}

        UNITTEST_FIXTURE_TEARDOWN()
		{
		}

		UNITTEST_TEST(init)
		{
			xbst::pointer_based::tree_t tree;
			tree.m_get_key_f = get_key_mynode_f;
			tree.m_compare_f = compare_mynode_f;
			tree.m_get_color_f = get_color_mynode_f;
			tree.m_set_color_f = set_color_mynode_f;
		}

		UNITTEST_TEST(insert) 
		{
			xbst::pointer_based::tree_t tree;
			tree.m_get_key_f = get_key_mynode_f;
			tree.m_compare_f = compare_mynode_f;
			tree.m_get_color_f = get_color_mynode_f;
			tree.m_set_color_f = set_color_mynode_f;

			const s32 numnodes = 16;
			mynode nodes[numnodes];
			for (s32 i=0; i<numnodes; ++i)
			{
				nodes[i].m_bstnode.clear();
				nodes[i].m_bstnode_color = 0;
				nodes[i].m_float = 100.0f + i*2;
				nodes[i].m_integer = 100 + i;
				nodes[i].m_key = &nodes[i];
			}

			xbst::pointer_based::node_t* root = nullptr;
			for (s32 i=0; i<numnodes; ++i)
			{
				xbst::pointer_based::insert(root, &tree, (u64)&nodes[i].m_key, &nodes[i].m_bstnode);
			}
			const char* result = nullptr;
			xbst::pointer_based::validate(root, &tree, result);
			CHECK_NULL(result);
		}

        UNITTEST_TEST(add_many)
        {
            u32 const value_count = 1024;

			xbst::pointer_based::tree_t tree;
			tree.m_get_key_f = get_key_mynode_f;
			tree.m_compare_f = compare_mynode_f;
			tree.m_get_color_f = get_color_mynode_f;
			tree.m_set_color_f = set_color_mynode_f;

			xbst::pointer_based::node_t* root = nullptr;
			const s32 numnodes = 1024;
			mynode nodes[numnodes];
			for (s32 i=0; i<numnodes; ++i)
			{
				nodes[i].m_bstnode.clear();
				nodes[i].m_bstnode_color = 0;
				nodes[i].m_float = 100.0f + i*2;
				nodes[i].m_integer = 100 + i;
				nodes[i].m_key = &nodes[i].m_bstnode;
				xbst::pointer_based::insert(root, &tree, (u64)&nodes[i].m_key, &nodes[i].m_bstnode);
			}

			for (s32 i=0; i<numnodes; ++i)
			{
				xbst::pointer_based::node_t* found = nullptr;
				bool could_find = xbst::pointer_based::find(root, &tree, (u64)&nodes[i].m_key, found);
				CHECK_TRUE(could_find);
				CHECK_EQUAL(found, &nodes[i].m_bstnode);
			}
        }
	}
}
UNITTEST_SUITE_END
