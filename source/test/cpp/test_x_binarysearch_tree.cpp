#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_binarysearch_tree.h"

#include "xunittest/xunittest.h"

using namespace xcore;

extern xalloc* gTestAllocator;

namespace xcore
{
	namespace test_pointer_based
	{
		struct mynode : public xbst::pointer_based::node_t
		{
			XCORE_CLASS_PLACEMENT_NEW_DELETE
			float m_float;
			s32 m_integer;
			void* m_key;
			s32 m_bstnode_color;
		};

		u64 get_key_mynode_f(const xbst::pointer_based::node_t* lhs)
		{
			mynode const* n = (mynode const*)(lhs);
			return (u64)&n->m_key;
		}

		s32 compare_mynode_f(const u64 pkey, const xbst::pointer_based::node_t* node)
		{
			mynode* n = (mynode*)(node);
			void* key = *(void**)pkey;
			if (key < n->m_key)
				return -1;
			if (key > n->m_key)
				return 1;
			return 0;
		}

		s32 get_color_mynode_f(const xbst::pointer_based::node_t* lhs)
		{
			mynode const* n = (mynode const*)(lhs);
			return n->m_bstnode_color;
		}

		void set_color_mynode_f(xbst::pointer_based::node_t* lhs, s32 color)
		{
			mynode* n = (mynode*)(lhs);
			n->m_bstnode_color = color;
		}
	}

	namespace test_index_based
	{
		struct mynode : public xbst::index_based::node_t
		{
			XCORE_CLASS_PLACEMENT_NEW_DELETE
			float m_float;
			s32 m_integer;
			void* m_key;
			s32 m_bstnode_color;
		};

		u64 get_key_mynode_f(const xbst::index_based::node_t* lhs)
		{
			mynode const* n = (mynode const*)(lhs);
			return (u64)&n->m_key;
		}

		s32 compare_mynode_f(const u64 pkey, const xbst::index_based::node_t* node)
		{
			mynode* n = (mynode*)(node);
			void* key = *(void**)pkey;
			if (key < n->m_key)
				return -1;
			if (key > n->m_key)
				return 1;
			return 0;
		}

		s32 get_color_mynode_f(const xbst::index_based::node_t* lhs)
		{
			mynode const* n = (mynode const*)(lhs);
			return n->m_bstnode_color;
		}

		void set_color_mynode_f(xbst::index_based::node_t* lhs, s32 color)
		{
			mynode* n = (mynode*)(lhs);
			n->m_bstnode_color = color;
		}
	}
}

UNITTEST_SUITE_BEGIN(binarysearch_tree)
{
    UNITTEST_FIXTURE(pointer_based)
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
			tree.m_get_key_f = test_pointer_based::get_key_mynode_f;
			tree.m_compare_f = test_pointer_based::compare_mynode_f;
			tree.m_get_color_f = test_pointer_based::get_color_mynode_f;
			tree.m_set_color_f = test_pointer_based::set_color_mynode_f;
		}

		UNITTEST_TEST(insert) 
		{
			xbst::pointer_based::tree_t tree;
			tree.m_get_key_f = test_pointer_based::get_key_mynode_f;
			tree.m_compare_f = test_pointer_based::compare_mynode_f;
			tree.m_get_color_f = test_pointer_based::get_color_mynode_f;
			tree.m_set_color_f = test_pointer_based::set_color_mynode_f;

			const s32 numnodes = 16;
			test_pointer_based::mynode nodes[numnodes];
			for (s32 i=0; i<numnodes; ++i)
			{
				nodes[i].clear();
				nodes[i].m_bstnode_color = 0;
				nodes[i].m_float = 100.0f + i*2;
				nodes[i].m_integer = 100 + i;
				nodes[i].m_key = &nodes[i];
			}

			xbst::pointer_based::node_t* root = nullptr;
			for (s32 i=0; i<numnodes; ++i)
			{
				xbst::pointer_based::insert(root, &tree, (u64)&nodes[i].m_key, &nodes[i]);
			}
			const char* result = nullptr;
			xbst::pointer_based::validate(root, &tree, result);
			CHECK_NULL(result);
		}

        UNITTEST_TEST(add_many)
        {
            u32 const value_count = 1024;

			xbst::pointer_based::tree_t tree;
			tree.m_get_key_f = test_pointer_based::get_key_mynode_f;
			tree.m_compare_f = test_pointer_based::compare_mynode_f;
			tree.m_get_color_f = test_pointer_based::get_color_mynode_f;
			tree.m_set_color_f = test_pointer_based::set_color_mynode_f;

			xbst::pointer_based::node_t* root = nullptr;
			const s32 numnodes = 1024;
			test_pointer_based::mynode nodes[numnodes];
			for (s32 i=0; i<numnodes; ++i)
			{
				nodes[i].clear();
				nodes[i].m_bstnode_color = 0;
				nodes[i].m_float = 100.0f + i*2;
				nodes[i].m_integer = 100 + i;
				nodes[i].m_key = &nodes[i];
				xbst::pointer_based::insert(root, &tree, (u64)&nodes[i].m_key, &nodes[i]);
			}

			for (s32 i=0; i<numnodes; ++i)
			{
				xbst::pointer_based::node_t* found = nullptr;
				bool could_find = xbst::pointer_based::find(root, &tree, (u64)&nodes[i].m_key, found);
				CHECK_TRUE(could_find);
				CHECK_EQUAL(found, &nodes[i]);
			}
        }

        UNITTEST_TEST(add_many_randomly)
        {
            u32 const value_count = 1024;

			xbst::pointer_based::tree_t tree;
			tree.m_get_key_f = test_pointer_based::get_key_mynode_f;
			tree.m_compare_f = test_pointer_based::compare_mynode_f;
			tree.m_get_color_f = test_pointer_based::get_color_mynode_f;
			tree.m_set_color_f = test_pointer_based::set_color_mynode_f;

			xbst::pointer_based::node_t* root = nullptr;
			
			const u32 numnodes = 1024;
			u32 nodes_remap[numnodes];
			for (s32 i=0; i<numnodes; ++i)
			{
				nodes_remap[i] = i;
			}

			// Randomly swap elements
			u32 rnd = 3;
			for (s32 i=0; i<numnodes; ++i)
			{
				rnd = rnd * 1664525 + 1013904223;
				u32 const idx1 = (i+rnd) % numnodes;
				rnd = rnd * 1664525 + 1013904223;
				u32 const idx2 = (i+rnd) % numnodes;
				u32 const idxt = nodes_remap[idx1];
				nodes_remap[idx1] = nodes_remap[idx2];
				nodes_remap[idx2] = idxt;
			}

			test_pointer_based::mynode nodes[numnodes];
			for (s32 i=0; i<numnodes; ++i)
			{
				nodes[i].m_bstnode_color = 0;
				nodes[i].m_float = 0;
				nodes[i].m_integer = 0;
				nodes[i].m_key = 0;
			}
			for (s32 i=0; i<numnodes; ++i)
			{
				s32 const idx = nodes_remap[i];
				CHECK_EQUAL(0, nodes[idx].m_integer);

				nodes[idx].clear();
				nodes[idx].m_bstnode_color = 0;
				nodes[idx].m_float = 100.0f + idx*2;
				nodes[idx].m_integer = 100 + idx;
				nodes[idx].m_key = &nodes[idx];
				xbst::pointer_based::insert(root, &tree, (u64)&nodes[idx].m_key, &nodes[idx]);
			}

			for (s32 i=0; i<numnodes; ++i)
			{
				xbst::pointer_based::node_t* found = nullptr;
				bool could_find = xbst::pointer_based::find(root, &tree, (u64)&nodes[i].m_key, found);
				CHECK_TRUE(could_find);
				CHECK_EQUAL(found, &nodes[i]);
			}

			for (s32 i=0; i<numnodes; ++i)
			{
				s32 const idx = i;
				nodes[idx].clear();
				nodes[idx].m_bstnode_color = 0;
				nodes[idx].m_float = 100.0f + idx*2;
				nodes[idx].m_integer = 100 + idx;
				nodes[idx].m_key = &nodes[idx];
				xbst::pointer_based::remove(root, &tree, &nodes[idx]);
			}

        }
	}

    UNITTEST_FIXTURE(index_based)
    {
		static void* sNodeData = nullptr;
		static xfsadexed* sNodeHeap = nullptr;

        UNITTEST_FIXTURE_SETUP()
		{
			const s32 sizeof_node = sizeof(test_index_based::mynode);
			const s32 countof_node = 16384;
			sNodeData = gTestAllocator->allocate(sizeof_node * countof_node);
			sNodeHeap = gTestAllocator->construct<xfsadexed_array>(sNodeData, sizeof_node, countof_node);
		}

        UNITTEST_FIXTURE_TEARDOWN()
		{
			gTestAllocator->deallocate(sNodeData);
			gTestAllocator->destruct(sNodeHeap);
		}

		UNITTEST_TEST(init)
		{
			xbst::index_based::tree_t tree;
			tree.m_get_key_f = test_index_based::get_key_mynode_f;
			tree.m_compare_f = test_index_based::compare_mynode_f;
			tree.m_get_color_f = test_index_based::get_color_mynode_f;
			tree.m_set_color_f = test_index_based::set_color_mynode_f;
		}

		UNITTEST_TEST(insert) 
		{
			xbst::index_based::tree_t tree;
			tree.m_get_key_f = test_index_based::get_key_mynode_f;
			tree.m_compare_f = test_index_based::compare_mynode_f;
			tree.m_get_color_f = test_index_based::get_color_mynode_f;
			tree.m_set_color_f = test_index_based::set_color_mynode_f;

			const s32 numnodes = 16;
			s32 nodes[numnodes];
			for (s32 i=0; i<numnodes; ++i)
			{
				test_index_based::mynode* pnode = sNodeHeap->construct<test_index_based::mynode>();
				nodes[i] = sNodeHeap->ptr2idx(pnode);
				pnode->clear();
				pnode->m_bstnode_color = 0;
				pnode->m_float = 100.0f + i*2;
				pnode->m_integer = 100 + i;
				pnode->m_key = &nodes[i];
			}

			u32 iroot = 0xffffffff;
			for (s32 i=0; i<numnodes; ++i)
			{
				test_index_based::mynode* pnode = (test_index_based::mynode*)sNodeHeap->idx2ptr(nodes[i]);
				xbst::index_based::insert(iroot, &tree, sNodeHeap, (u64)&pnode->m_key, nodes[i]);
			}
			const char* result = nullptr;
			xbst::index_based::node_t* proot = (xbst::index_based::node_t*)sNodeHeap->idx2ptr(iroot);
			xbst::index_based::validate(proot, iroot, &tree, sNodeHeap, result);
			CHECK_NULL(result);
		}

        UNITTEST_TEST(add_many)
        {
            u32 const value_count = 1024;

			xbst::index_based::tree_t tree;
			tree.m_get_key_f = test_index_based::get_key_mynode_f;
			tree.m_compare_f = test_index_based::compare_mynode_f;
			tree.m_get_color_f = test_index_based::get_color_mynode_f;
			tree.m_set_color_f = test_index_based::set_color_mynode_f;

			u32 iroot = 0xffffffff;
			xbst::index_based::node_t* proot = nullptr;

			const s32 numnodes = 1024;
			s32 nodes[numnodes];

			for (s32 i=0; i<numnodes; ++i)
			{
				test_index_based::mynode* pnode = sNodeHeap->construct<test_index_based::mynode>();
				nodes[i] = sNodeHeap->ptr2idx(pnode);

				pnode->clear();
				pnode->m_bstnode_color = 0;
				pnode->m_float = 100.0f + i*2;
				pnode->m_integer = 100 + i;
				pnode->m_key = pnode;
				xbst::index_based::insert(iroot, &tree, sNodeHeap, (u64)&pnode->m_key, nodes[i]);
			}

			for (s32 i=0; i<numnodes; ++i)
			{
				test_index_based::mynode* pnode = (test_index_based::mynode*)sNodeHeap->idx2ptr(nodes[i]);
				u32 ifound = 0;
				bool could_find = xbst::index_based::find(iroot, &tree, sNodeHeap, (u64)&pnode->m_key, ifound);
				CHECK_TRUE(could_find);
				CHECK_EQUAL(ifound, nodes[i]);
			}
        }

        UNITTEST_TEST(add_many_randomly)
        {
            u32 const value_count = 1024;

			xbst::index_based::tree_t tree;
			tree.m_get_key_f = test_index_based::get_key_mynode_f;
			tree.m_compare_f = test_index_based::compare_mynode_f;
			tree.m_get_color_f = test_index_based::get_color_mynode_f;
			tree.m_set_color_f = test_index_based::set_color_mynode_f;

			u32 iroot = 0xffffffff;
			xbst::index_based::node_t* root = nullptr;
			
			const u32 numnodes = 1024;
			u32 nodes[numnodes];
			for (s32 i=0; i<numnodes; ++i)
			{
				test_index_based::mynode* pnode = sNodeHeap->construct<test_index_based::mynode>();
				nodes[i] = sNodeHeap->ptr2idx(pnode);
			}

			// Randomly swap elements
			u32 rnd = 3;
			for (s32 i=0; i<numnodes; ++i)
			{
				rnd = rnd * 1664525 + 1013904223;
				u32 const idx1 = (i+rnd) % numnodes;
				rnd = rnd * 1664525 + 1013904223;
				u32 const idx2 = (i+rnd) % numnodes;
				u32 const idxt = nodes[idx1];
				nodes[idx1] = nodes[idx2];
				nodes[idx2] = idxt;
			}

			for (s32 i=0; i<numnodes; ++i)
			{
				test_index_based::mynode* pnode = (test_index_based::mynode*)sNodeHeap->idx2ptr(nodes[i]);
				pnode->m_bstnode_color = 0;
				pnode->m_float = 0;
				pnode->m_integer = 0;
				pnode->m_key = 0;
			}
			for (s32 i=0; i<numnodes; ++i)
			{
				test_index_based::mynode* pnode = (test_index_based::mynode*)sNodeHeap->idx2ptr(nodes[i]);
				CHECK_EQUAL(0, pnode->m_integer);

				pnode->clear();
				pnode->m_bstnode_color = 0;
				pnode->m_float = 100.0f + nodes[i]*2;
				pnode->m_integer = 100 + nodes[i];
				pnode->m_key = pnode;
				xbst::index_based::insert(iroot, &tree, sNodeHeap, (u64)&pnode->m_key, nodes[i]);
			}

			for (s32 i=0; i<numnodes; ++i)
			{
				test_index_based::mynode* pnode = (test_index_based::mynode*)sNodeHeap->idx2ptr(nodes[i]);
				u32 ifound;
				bool could_find = xbst::index_based::find(iroot, &tree, sNodeHeap, (u64)&pnode->m_key, ifound);
				CHECK_TRUE(could_find);
				CHECK_EQUAL(ifound, nodes[i]);
			}

			for (s32 i=0; i<numnodes; ++i)
			{
				test_index_based::mynode* pnode = (test_index_based::mynode*)sNodeHeap->idx2ptr(nodes[i]);
				pnode->clear();
				pnode->m_bstnode_color = 0;
				pnode->m_float = 100.0f + nodes[i]*2;
				pnode->m_integer = 100 + nodes[i];
				pnode->m_key = pnode;
				bool removed = xbst::index_based::remove(iroot, &tree, sNodeHeap, nodes[i]);
				CHECK_TRUE(removed);
			}

        }
	}

}
UNITTEST_SUITE_END
