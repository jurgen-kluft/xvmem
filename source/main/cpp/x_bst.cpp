#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/private/x_bst.h"

namespace xcore
{
    namespace xbst
    {
		namespace pointer_based
		{
			bool find(node_t*& root, tree_t* tree, void* data, node_t*& found)
			{
				bool ret;

				ASSERT(tree != nullptr);
				ASSERT(data != nullptr);

				found = nullptr;
				if (root == nullptr)
				{
					ret = false;
					goto done;
				}

				node_t* node = root;
				while (node != nullptr)
				{
					s32 const c = tree->m_compare_f(data, node);
					ASSERT(c==0 || c==-1 || c==1);
					if (c == 0)
						break;
					node = node->get_child((c + 1) >> 1);
				}

				if (node == nullptr)
				{
					ret = false;
					goto done;
				}

				// Return the node we found
				ret = true;
				found = node;

			done:
				return ret;
			}

            bool upper(node_t*& root, tree_t* tree, void* data, node_t*& found)
			{
				bool ret;

				ASSERT(tree != nullptr);
				ASSERT(data != nullptr);

				found = nullptr;
				if (root == nullptr)
				{
					ret = false;
					goto done;
				}

				node_t* node = root;
				while (node != nullptr)
				{
					s32 const c = tree->m_compare_f(data, node);
					ASSERT(c==0 || c==-1 || c==1);
					node_t* child = nullptr;
					if (c == 0)
						break;

					child = node->get_child((c + 1) >> 1);
					if (child == nullptr)
						break;
					node = child;
				}

				ret = true;
				found = node;

			done:
				return ret;
			}

			static inline node_t* helper_get_sibling(node_t* node)
			{
				node_t* parent = node->get_parent();
				if (parent == nullptr)
					return nullptr;
				s32 const c = (node == parent->get_left()) ? 1 : 0;
				return parent->get_child(c);
			}

			static inline node_t* helper_get_grandparent(node_t* node)
			{
				node_t* parent_node = node->get_parent();
				if (parent_node == nullptr)
					return nullptr;
				return parent_node->get_parent();
			}

			static inline node_t* helper_get_uncle(node_t* node)
			{
				node_t* grandparent = helper_get_grandparent(node);
				if (grandparent == nullptr)
					return nullptr;
				const s32 c = (node->get_parent() == grandparent->get_left()) ? 1 : 0;
				return grandparent->get_child(c);
			}

			static inline void helper_rotate_left(node_t*& root, node_t* node)
			{
				node_t* x = node;
				node_t* y = x->get_right();
				x->set_right(y->get_left());
				if (y->has_left())
				{
					node_t* yleft = y->get_left();
					yleft->set_parent(x);
				}

				y->set_parent(x->get_parent());
				if (x->has_parent())
				{
					root = y;
				}
				else
				{
					node_t* xp = x->get_parent();
					s32 const c = (x == xp->get_left()) ? 0 : 1;
					xp->set_child(c, y);
				}
				y->set_left(x);
				x->set_parent(y);
			}

			static inline void helper_rotate_right(node_t*& root, node_t* node)
			{
				node_t* x = node;
				node_t* y = x->get_left();
				x->set_left(y->get_right());

				if (y->has_right())
				{
					node_t* yright = y->get_right();
					yright->set_parent(x);
				}

				y->set_parent(x->get_parent());
				if (!x->has_parent())
				{
					root = y;
				}
				else
				{
					node_t* xp = x->get_parent();
					s32 const c = (x == xp->get_left()) ? 0 : 1;
					xp->set_child(c, y);
				}

				y->set_right(x);
				x->set_parent(y);
			}

			static void helper_insert_rebalance(tree_t* tree, node_t*& root, node_t* node)
			{
				node_t* new_node_parent = node->get_parent();

				if (new_node_parent != nullptr && new_node_parent->is_color_black(tree)==false)
				{
					node_t* iter = node;

					// Iterate until we're at the root (which we just color black) or
					// until we the parent node is no longer red.
					while ((root != iter) && iter->has_parent() && (iter->get_parent()->is_color_red(tree)))
					{
						node_t* parent = iter->get_parent();
						node_t* grandparent = helper_get_grandparent(iter);
						node_t* uncle = nullptr;

						ASSERT(iter->is_color_red(tree));

						bool uncle_is_left;
						if (parent == grandparent->get_left())
						{
							uncle_is_left = false;
							uncle = grandparent->get_right();
						}
						else
						{
							uncle_is_left = true;
							uncle = grandparent->get_left();
						}

						// Case 1: Uncle is not black
						if (uncle && uncle->is_color_red(tree))
						{
							// Color parent and uncle black
							parent->set_color_black(tree);
							uncle->set_color_black(tree);

							// Color Grandparent as Red
							grandparent->set_color_red(tree);
							iter = grandparent;
							// Continue iteration, processing grandparent
						}
						else
						{
							// Case 2 - node's parent is red, but uncle is black
							if (!uncle_is_left && parent->get_right() == iter)
							{
								iter = iter->get_parent();
								helper_rotate_left(root, iter);
							}
							else if (uncle_is_left && parent->get_left() == iter)
							{
								iter = iter->get_parent();
								helper_rotate_right(root, iter);
							}

							// Case 3 - Recolor and rotate
							parent = iter->get_parent();
							parent->set_color_black(tree);

							grandparent = helper_get_grandparent(iter);
							grandparent->set_color_red(tree);
							if (!uncle_is_left)
							{
								helper_rotate_right(root, grandparent);
							}
							else
							{
								helper_rotate_left(root, grandparent);
							}
						}
					}

					// Make sure the tree root is black (Case 1: Continued)
					root->set_color_black(tree);
				}
			}

			bool insert(node_t*& root, tree_t* tree, void* key, node_t* node)
			{
				bool ret;

				ASSERT(tree != nullptr);
				ASSERT(node != nullptr);

				node->clear();

				// Case 1: Simplest case -- tree is empty
				if (root == nullptr)
				{
					ret = true;
					root = node;
					node->set_color_black(tree);
					goto done;
				}

				// Otherwise, insert the node as you would typically in a BST
				node_t* nd = root;
				node->set_color_red(tree);

				// Insert a node into the tree as you normally would
				while (nd != nullptr)
				{
					s32 const c = tree->m_compare_f(key, nd);
					if (c == 0)
					{
						ret = false;
						goto done;
					}

					if (c < 0)
					{
						if (!nd->has_left())
						{
							nd->set_left(node);
							break;
						}
						else
						{
							nd = nd->get_left();
						}
					}
					else 
					{
						if (!nd->has_right())
						{
							nd->set_right(node);
							break;
						}
						else
						{
							nd = nd->get_right();
						}
					}
				}

				node->set_parent(nd);

				// Rebalance the tree about the node we just added
				helper_insert_rebalance(tree, root, node);
				
				ret = true;

			done:
				return ret;
			}

			static node_t* helper_find_minimum(node_t* node)
			{
				node_t* x = node;
				while (x->has_left())
				{
					x = x->get_left();
				}
				return x;
			}

			static node_t* helper_find_maximum(node_t* node)
			{
				node_t* x = node;
				while (x->has_right())
				{
					x = x->get_right();
				}
				return x;
			}

			static node_t* helper_find_successor(node_t* node)
			{
				node_t* x = node;
				if (x->has_right())
				{
					return helper_find_minimum(x->get_right());
				}

				node_t* y = x->get_parent();
				while (y != nullptr && x == y->get_right())
				{
					x = y;
					y = y->get_parent();
				}
				return y;
			}

			static node_t* helper_find_predecessor(node_t* node)
			{
				node_t* x = node;
				if (x->has_left())
				{
					return helper_find_maximum(x->get_left());
				}

				node_t* y = x->get_parent();
				while (y != nullptr && x == y->get_left())
				{
					x = y;
					y = y->get_parent();
				}
				return y;
			}

			// Replace x with y, inserting y where x previously was
			static void helper_swap_node(tree_t* tree, node_t*& root, node_t* x, node_t* y)
			{
				node_t* left = x->get_left();
				node_t* right = x->get_right();
				node_t* parent = x->get_parent();

				y->set_parent(parent);
				if (parent != nullptr)
				{
					if (parent->get_left() == x)
					{
						parent->set_left(y);
					}
					else
					{
						parent->set_right(y);
					}
				}
				else
				{
					if (root == x)
					{
						root = y;
					}
				}

				y->set_right(right);
				if (right != nullptr)
				{
					right->set_parent(y);
				}
				x->set_right(nullptr);
				y->set_left(left);
				if (left != nullptr)
				{
					left->set_parent(y);
				}
				x->set_left(nullptr);

				y->set_color(tree, x->get_color(tree));
				x->set_parent(nullptr);
			}

			static void helper_delete_rebalance(tree_t* tree, node_t*& root, node_t* node, node_t* parent, s32 node_is_left)
			{
				node_t* x = node;
				node_t* xp = parent;
				s32 is_left = node_is_left;

				while (x != root && (x == nullptr || x->is_color_black(tree)))
				{
					node_t* w = is_left ? xp->get_right() : xp->get_left();    /* Sibling */
					if (w != nullptr && w->is_color_red(tree))
					{
						// Case 1
						w->set_color_black(tree);
						xp->set_color_red(tree);
						if (is_left)
						{
							helper_rotate_left(root, xp);
						}
						else
						{
							helper_rotate_right(root, xp);
						}
						w = is_left ? xp->get_right() : xp->get_left();
					}

					node_t* wleft = w != nullptr ? w->get_left() : nullptr;
					node_t* wright = w != nullptr ? w->get_right() : nullptr;
					if ((wleft == nullptr || wleft->is_color_black(tree)) && (wright == nullptr || wright->is_color_black(tree)))
					{
						// Case 2
						if (w != nullptr)
						{
							w->set_color_red(tree);
						}
						x = xp;
						xp = x->get_parent();
						is_left = xp && (x == xp->get_left());
					}
					else
					{
						if (is_left && (wright == nullptr || wright->is_color_black(tree)))
						{
							// Case 3a
							w->set_color_red(tree);
							if (wleft)
							{
								wleft->set_color_black(tree);
							}
							helper_rotate_right(root, w);
							w = xp->get_right();
						}
						else if (!is_left && (wleft == nullptr || wleft->is_color_black(tree)))
						{
							// Case 3b
							w->set_color_red(tree);
							if (wright)
							{
								wright->set_color_black(tree);
							}
							helper_rotate_left(root, w);
							w = xp->get_left();
						}

						// Case 4
						wleft = w->get_left();
						wright = w->get_right();

						w->set_color(tree, xp->get_color(tree));
						xp->set_color_black(tree);

						if (is_left && wright != nullptr)
						{
							wright->set_color_black(tree);
							helper_rotate_left(root, xp);
						}
						else if (!is_left && wleft != nullptr)
						{
							wleft->set_color_black(tree);
							helper_rotate_right(root, xp);
						}
						x = root;
					}
				}

				if (x != nullptr)
				{
					x->set_color_black(tree);
				}
			}

			bool remove(tree_t* tree, node_t*& root, node_t* node)
			{
				bool const ret = true;

				ASSERT(tree != nullptr);
				ASSERT(node != nullptr);

				node_t* y;
				if (!node->has_left() || !node->has_right())
				{
					y = node;
				}
				else
				{
					y = helper_find_successor(node);
				}

				node_t* x;
				if (y->has_left())
				{
					x = y->get_left();
				}
				else
				{
					x = y->get_right();
				}

				node_t* xp;
				if (x != nullptr)
				{
					xp = y->get_parent();
					x->set_parent(xp);
				}
				else
				{
					xp = y->get_parent();
				}

				s32 is_left = 0;
				if (!y->has_parent())
				{
					root = x;
					xp = nullptr;
				}
				else
				{
					node_t* yp = y->get_parent();
					if (y == yp->get_left())
					{
						yp->set_left(x);
						is_left = 1;
					}
					else
					{
						yp->set_right(x);
						is_left = 0;
					}
				}

				s32 const y_color = y->get_color(tree);

				// Swap in the node
				if (y != node)
				{
					helper_swap_node(tree, root, node, y);
					if (xp == node)
					{
						xp = y;
					}
				}

				if (y_color == COLOR_BLACK)
				{
					helper_delete_rebalance(tree, root, x, xp, is_left);
				}

				node->clear();

				return ret;
			}

            void node_t::set_color(tree_t* t, s32 color)
			{
				t->m_set_color_f(this, color);
			}
            
			s32  node_t::get_color(tree_t* t) const
            {
				return t->m_get_color_f(this);
            }
            
            void node_t::set_color_black(tree_t* t)
            {
				t->m_set_color_f(this, COLOR_BLACK);
            }
            
            void node_t::set_color_red(tree_t* t)
            {
				t->m_set_color_f(this, COLOR_RED);
            }
            
            bool node_t::is_color_black(tree_t* t) const
            {
				return t->m_get_color_f(this) == COLOR_BLACK;
            }
            
            bool node_t::is_color_red(tree_t* t) const
            {
				return t->m_get_color_f(this) == COLOR_RED;
            }
            

            s32 validate(node_t*& root, tree_t* tree, const char*& result)
            {
                s32 lh, rh;
                if (root == nullptr)
                {
                    return 1;
                }
                else
                {
                    node_t *ln = root->get_left();
                    node_t *rn = root->get_right();

                    // Consecutive red links
                    if (root->is_color_red(tree))
                    {
                        if ((ln!=nullptr && ln->is_color_red(tree)) || (rn!=nullptr && rn->is_color_red(tree)))
                        {
                            result = "Red violation";
                            return 0;
                        }
                    }

                    lh = validate(ln, tree, result);
                    rh = validate(rn, tree, result);

                    const void* root_key = tree->m_get_key_f(root);

                    // Invalid binary search tree
                    if ((ln != nullptr && tree->m_compare_f(root_key, ln) <= 0) || (rn != nullptr && tree->m_compare_f(root_key, rn) >= 0))
                    {
                        result = "Binary tree violation";
                        return 0;
                    }

                    // Black height mismatch
                    if (lh != 0 && rh != 0 && lh != rh)
                    {
                        result = "Black violation";
                        return 0;
                    }

                    // Only count black links
                    if (lh != 0 && rh != 0)
                    {
                        return root->is_color_red(tree) ? lh : lh + 1;
                    }
                    return 0;
                }

            }
		}


		// ============================================================================================================
		// ============================================================================================================
		// ============================================================================================================
		// ============================================================================================================
		// ============================================================================================================
		// ============================================================================================================
		// ============================================================================================================
		// ============================================================================================================

		namespace index_based
		{
			node_t*	    tree_t::idx2ptr(u32 idx)
			{
				if (idx == 0) return nullptr;
				return (node_t*)m_dexer->idx2ptr(idx);
			}
			
			u32  	    tree_t::ptr2idx(node_t* p)
			{
				if (p == nullptr) return 0;
				return m_dexer->ptr2idx(p);
			}


			bool find(u32& root, tree_t* tree, void* data, u32& found)
			{
				bool ret;

				ASSERT(tree != nullptr);
				ASSERT(data != nullptr);

				found = 0;
				if (root == 0)
				{
					ret = false;
					goto done;
				}

				node_t* node = tree->idx2ptr(root);
				while (node != nullptr)
				{
					s32 const c = tree->m_compare_f(data, node);
					ASSERT(c==0 || c==-1 || c==1);
					if (c == 0)
						break;

					u32 const ni = node->get_child((c + 1) >> 1);
					node = tree->idx2ptr(ni);
				}

				if (node == nullptr)
				{
					ret = false;
					goto done;
				}

				// Return the node we found
				ret = true;
				found = tree->ptr2idx(node);

			done:
				return ret;
			}

			bool upper(u32& root, tree_t* tree, void* data, u32& found)
			{
				bool ret;

				ASSERT(tree != nullptr);
				ASSERT(data != nullptr);

				found = 0;
				if (root == 0)
				{
					ret = false;
					goto done;
				}

				node_t* node = tree->idx2ptr(root);
				while (node != nullptr)
				{
					s32 const c = tree->m_compare_f(data, node);
					ASSERT(c==0 || c==-1 || c==1);
					if (c == 0)
						break;

					u32 const ni = node->get_child((c + 1) >> 1);
					if (ni == 0)
						break;
					node = tree->idx2ptr(ni);
				}

				// Return the node we found
				ret = true;
				found = tree->ptr2idx(node);

			done:
				return ret;
			}

			static inline u32 helper_get_sibling(node_t* node, u32 inode, tree_t* tree)
			{
				u32 iparent = node->get_parent();
				if (iparent == 0)
					return 0;
				node_t* pparent = tree->idx2ptr(iparent);
				s32 const c = (inode == pparent->get_left()) ? 1 : 0;
				return pparent->get_child(c);
			}

			static inline u32 helper_get_grandparent(node_t* node, u32 inode, tree_t* tree)
			{
				u32 iparent = node->get_parent();
				if (iparent == 0)
					return 0;
				node_t* parent = tree->idx2ptr(iparent);
				return parent->get_parent();
			}

			static inline u32 helper_get_uncle(node_t* node, u32 inode, tree_t* tree)
			{
				u32 igrandparent = helper_get_grandparent(node, inode, tree);
				if (igrandparent == 0)
					return 0;
				node_t* grandparent = tree->idx2ptr(igrandparent);
				const s32 c = (node->get_parent() == grandparent->get_left()) ? 1 : 0;
				return grandparent->get_child(c);
			}

			static inline void helper_rotate_left(node_t*& root, u32& iroot, node_t* node, u32 inode, tree_t* tree)
			{
				u32 ix = inode;
				node_t* x = node;
				u32 iy = x->get_right();
				node_t* y = tree->idx2ptr(iy);
				x->set_right(y->get_left());
				if (y->has_left())
				{
					u32 iyleft = y->get_left();
					node_t* yleft = tree->idx2ptr(iyleft);
					yleft->set_parent(ix);
				}

				y->set_parent(x->get_parent());
				if (x->has_parent())
				{
					root = y;
					iroot = iy;
				}
				else
				{
					u32 ixp = x->get_parent();
					node_t* xp = tree->idx2ptr(ixp);
					s32 const c = (ix == xp->get_left()) ? 0 : 1;
					xp->set_child(c, iy);
				}
				y->set_left(ix);
				x->set_parent(iy);
			}

			static inline void helper_rotate_right(node_t*& root, u32& iroot, node_t* node, u32 inode, tree_t* tree)
			{
				u32 ix = inode;
				node_t* x = node;
				u32 iy = x->get_left();
				node_t* y = tree->idx2ptr(iy);
				x->set_left(y->get_right());
				if (y->has_right())
				{
					u32 iyright = y->get_right();
					node_t* yright = tree->idx2ptr(iyright);
					yright->set_parent(ix);
				}

				y->set_parent(x->get_parent());
				if (!x->has_parent())
				{
					root = y;
					iroot = iy;
				}
				else
				{
					u32 ixp = x->get_parent();
					node_t* xp = tree->idx2ptr(ixp);
					s32 const c = (ix == xp->get_left()) ? 0 : 1;
					xp->set_child(c, iy);
				}

				y->set_right(ix);
				x->set_parent(iy);
			}

			static void helper_insert_rebalance(tree_t* tree, node_t*& root, u32& iroot, node_t* node, u32 inode)
			{
				u32 inew_node_parent = node->get_parent();
				node_t* new_node_parent = tree->idx2ptr(inew_node_parent);

				if (new_node_parent != nullptr && new_node_parent->is_color_black(tree)==false)
				{
					u32 iiter = inode;
					node_t* iter = node;

					// Iterate until we're at the root (which we just color black) or
					// until we the parent node is no longer red.
					while ((root != iter) && iter->has_parent())
					{
						u32 iparent = iter->get_parent();
						node_t* parent = tree->idx2ptr(iparent);
						if (!parent->is_color_red(tree))
							break;

						u32 igrandparent = helper_get_grandparent(iter, iiter, tree);
						node_t* grandparent = tree->idx2ptr(igrandparent);

						u32 iuncle = 0;
						node_t* uncle = nullptr;

						ASSERT(iter->is_color_red(tree));

						bool uncle_is_left;
						if (iparent == grandparent->get_left())
						{
							uncle_is_left = false;
							iuncle = grandparent->get_right();
						}
						else
						{
							uncle_is_left = true;
							iuncle = grandparent->get_left();
						}
						uncle = tree->idx2ptr(iuncle);

						// Case 1: Uncle is not black
						if (uncle && uncle->is_color_red(tree))
						{
							// Color parent and uncle black
							parent->set_color_black(tree);
							uncle->set_color_black(tree);

							// Color Grandparent as Red
							grandparent->set_color_red(tree);
							iiter = igrandparent;
							iter = grandparent;
							// Continue iteration, processing grandparent
						}
						else
						{
							// Case 2 - node's parent is red, but uncle is black
							if (!uncle_is_left && parent->get_right() == iiter)
							{
								iiter = iter->get_parent();
								iter = tree->idx2ptr(iiter);
								helper_rotate_left(root, iroot, iter, iiter, tree);
							}
							else if (uncle_is_left && parent->get_left() == iiter)
							{
								iiter = iter->get_parent();
								iter = tree->idx2ptr(iiter);
								helper_rotate_right(root, iroot, iter, iiter, tree);
							}

							// Case 3 - Recolor and rotate
							iparent = iter->get_parent();
							parent = tree->idx2ptr(iparent);
							parent->set_color_black(tree);

							igrandparent = helper_get_grandparent(iter, iiter, tree);
							grandparent = tree->idx2ptr(igrandparent);
							grandparent->set_color_red(tree);
							if (!uncle_is_left)
							{
								helper_rotate_right(root, iroot, grandparent, igrandparent, tree);
							}
							else
							{
								helper_rotate_right(root, iroot, grandparent, igrandparent, tree);
							}
						}
					}

					// Make sure the tree root is black (Case 1: Continued)
					root->set_color_black(tree);
				}
			}

			bool insert(u32& iroot, tree_t* tree, void* key, u32 inode)
			{
				bool ret;

				ASSERT(tree != nullptr);
				node_t* node = tree->idx2ptr(inode);
				ASSERT(node != nullptr);
				node->clear();

				node_t* root = tree->idx2ptr(iroot);

				// Case 1: Simplest case -- tree is empty
				if (iroot == 0)
				{
					ret = true;
					iroot = inode;
					node->set_color_black(tree);
					goto done;
				}

				// Otherwise, insert the node as you would typically in a BST
				u32 ind = iroot;
				node_t* nd = tree->idx2ptr(iroot);
				node->set_color_red(tree);

				// Insert a node into the tree as you normally would
				while (nd != nullptr)
				{
					s32 const c = tree->m_compare_f(key, nd);
					if (c == 0)
					{
						ret = false;
						goto done;
					}

					if (c < 0)
					{
						if (!nd->has_left())
						{
							nd->set_left(inode);
							break;
						}
						else
						{
							ind = nd->get_left();
							nd = tree->idx2ptr(ind);
						}
					}
					else 
					{
						if (!nd->has_right())
						{
							nd->set_right(inode);
							break;
						}
						else
						{
							ind = nd->get_right();
							nd = tree->idx2ptr(ind);
						}
					}
				}

				node->set_parent(ind);

				// Rebalance the tree about the node we just added
				helper_insert_rebalance(tree, root, iroot, node, inode);
				
				ret = true;

			done:
				return ret;
			}
		}	
	}
}