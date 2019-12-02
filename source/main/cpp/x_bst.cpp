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
					s32 const c = tree->m_compare_f(node, data);
					if (c < 0) {
						node = node->get_right();
					} else if (c == 0) {
						break; // We found our node
					} else {
						// Otherwise, we want the right node, and continue iteration
						node = node->get_left();
					}
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

			static inline node_t* helper_get_sibling(node_t* node)
			{
				node_t* parent = node->get_parent();
				if (parent == nullptr)
					return nullptr;

				if (node == parent->get_left())
					return parent->get_right();

				return parent->get_left();
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

				if (node->get_parent() == grandparent->get_left())
					return grandparent->get_right();
				return grandparent->get_left();
			}

			static inline void helper_rotate_left(node_t*& root, node_t* node)
			{
				node_t* x = node;
				node_t* y = x->get_right();
				x->set_right(y->get_left());
				if (y->get_left() != nullptr)
				{
					node_t* yleft = y->get_left();
					yleft->set_parent(x);
				}

				y->set_parent(x->get_parent());
				if (x->get_parent() == nullptr)
				{
					root = y;
				}
				else
				{
					node_t* xp = x->get_parent();
					if (x == xp->get_left())
					{
						xp->set_left(y);
					}
					else
					{
						xp->set_right(y);
					}
				}
				y->set_left(x);
				x->set_parent(y);
			}

			static inline void helper_rotate_right(node_t*& root, node_t* node)
			{
				node_t* x = node;
				node_t* y = x->get_left();
				x->set_left(y->get_right());

				if (y->get_right() != nullptr)
				{
					node_t* yright = y->get_right();
					yright->set_parent(x);
				}

				y->set_parent(x->get_parent());
				if (x->get_parent() == nullptr)
				{
					root = y;
				}
				else
				{
					node_t* xp = x->get_parent();
					if (x == xp->get_left())
					{
						xp->set_left(y);
					}
					else
					{
						xp->set_right(y);
					}
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
					while ((root != iter) && (iter->get_parent() != nullptr) && (iter->get_parent()->is_color_red(tree)))
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

				ASSERT(tree != NULL);
				ASSERT(node != NULL);

				node->clear();

				// Case 1: Simplest case -- tree is empty
				if (root == NULL)
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
				while (nd != NULL)
				{
					s32 const c = tree->m_compare_f(nd, key);
					if (c == 0)
					{
						ret = false;
						goto done;
					}

					if (c > 0)
					{
						if (nd->get_left() == NULL)
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
						if (nd->get_right() == NULL)
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
				while (x->get_left() != NULL)
				{
					x = x->get_left();
				}
				return x;
			}

			static node_t* helper_find_maximum(node_t* node)
			{
				node_t* x = node;
				while (x->get_right() != NULL)
				{
					x = x->get_right();
				}
				return x;
			}

			static node_t* helper_find_successor(node_t* node)
			{
				node_t* x = node;
				if (x->get_right() != NULL)
				{
					return helper_find_minimum(x->get_right());
				}

				node_t* y = x->get_parent();
				while (y != NULL && x == y->get_right())
				{
					x = y;
					y = y->get_parent();
				}
				return y;
			}

			static node_t* helper_find_predecessor(node_t* node)
			{
				node_t* x = node;
				if (x->get_left() != NULL)
				{
					return helper_find_maximum(x->get_left());
				}

				node_t* y = x->get_parent();
				while (y != NULL && x == y->get_left())
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
				if (parent != NULL)
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
				if (right != NULL)
				{
					right->set_parent(y);
				}
				x->set_right(NULL);
				y->set_left(left);
				if (left != NULL)
				{
					left->set_parent(y);
				}
				x->set_left(NULL);

				y->set_color(tree, x->get_color(tree));
				x->set_parent(NULL);
			}

			static void helper_delete_rebalance(tree_t* tree, node_t*& root, node_t* node, node_t* parent, s32 node_is_left)
			{
				node_t* x = node;
				node_t* xp = parent;
				s32 is_left = node_is_left;

				while (x != root && (x == NULL || x->is_color_black(tree)))
				{
					node_t* w = is_left ? xp->get_right() : xp->get_left();    /* Sibling */
					if (w != NULL && w->is_color_red(tree))
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

					node_t* wleft = w != NULL ? w->get_left() : NULL;
					node_t* wright = w != NULL ? w->get_right() : NULL;
					if ((wleft == NULL || wleft->is_color_black(tree)) && (wright == NULL || wright->is_color_black(tree)))
					{
						// Case 2
						if (w != NULL)
						{
							w->set_color_red(tree);
						}
						x = xp;
						xp = x->get_parent();
						is_left = xp && (x == xp->get_left());
					}
					else
					{
						if (is_left && (wright == NULL || wright->is_color_black(tree)))
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
						else if (!is_left && (wleft == NULL || wleft->is_color_black(tree)))
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

						if (is_left && wright != NULL)
						{
							wright->set_color_black(tree);
							helper_rotate_left(root, xp);
						}
						else if (!is_left && wleft != NULL)
						{
							wleft->set_color_black(tree);
							helper_rotate_right(root, xp);
						}
						x = root;
					}
				}

				if (x != NULL)
				{
					x->set_color_black(tree);
				}
			}

			bool remove(tree_t* tree, node_t*& root, node_t* node)
			{
				bool const ret = true;

				ASSERT(tree != NULL);
				ASSERT(node != NULL);

				node_t* y;
				if (node->get_left() == NULL || node->get_right() == NULL)
				{
					y = node;
				}
				else
				{
					y = helper_find_successor(node);
				}

				node_t* x;
				if (y->get_left() != NULL)
				{
					x = y->get_left();
				}
				else
				{
					x = y->get_right();
				}

				node_t* xp;
				if (x != NULL)
				{
					xp = y->get_parent();
					x->set_parent(xp);
				}
				else
				{
					xp = y->get_parent();
				}

				s32 is_left = 0;
				if (y->get_parent() == NULL)
				{
					root = x;
					xp = NULL;
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
                    if ((ln != nullptr && tree->m_compare_f(ln, root_key) >= 0) || (rn != nullptr && tree->m_compare_f(rn, root_key) <= 0))
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
	}
}