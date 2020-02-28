#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_binarysearch_tree.h"

namespace xcore
{
    namespace xbst
    {
        namespace pointer_based
        {
            bool clear(node_t*& iterator, tree_t* tree, node_t*& n)
            {
                // Rotate away the left links so that we can treat this like the destruction of a linked list
                node_t* it = iterator;
                iterator   = nullptr;
                while (it != nullptr)
                {
                    if (!it->has_left())
                    { // No left links, just kill the node and move on
                        iterator = it->get_right();
                        it->clear();
                        n = it;
                        return true;
                    }
                    else
                    { // Rotate away the left link and check again
                        iterator = it->get_left();
                        it->set_left(iterator->get_right());
                        iterator->set_right(it);
                    }
                    it       = iterator;
                    iterator = nullptr;
                }
                return false;
            }

            bool find(node_t*& root, tree_t* tree, u64 data, node_t*& found)
            {
                bool ret;

                ASSERT(tree != nullptr);

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
                    ASSERT(c == 0 || c == -1 || c == 1);
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
                ret   = true;
                found = node;

            done:
                return ret;
            }

            bool upper(node_t*& root, tree_t* tree, u64 data, node_t*& found)
            {
                bool ret;

                ASSERT(tree != nullptr);

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
                    ASSERT(c == 0 || c == -1 || c == 1);
                    node_t* child = nullptr;
                    if (c == 0)
                        break;

                    child = node->get_child((c + 1) >> 1);
                    if (child == nullptr)
                        break;
                    node = child;
                }

                ret   = true;
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
                if (!x->has_parent())
                {
                    root = y;
                }
                else
                {
                    node_t*   xp = x->get_parent();
                    s32 const c  = (x == xp->get_left()) ? 0 : 1;
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
                    node_t*   xp = x->get_parent();
                    s32 const c  = (x == xp->get_left()) ? 0 : 1;
                    xp->set_child(c, y);
                }

                y->set_right(x);
                x->set_parent(y);
            }

            static void helper_insert_rebalance(tree_t* tree, node_t*& root, node_t* node)
            {
                node_t* new_node_parent = node->get_parent();

                if (new_node_parent != nullptr && new_node_parent->is_color_black(tree) == false)
                {
                    node_t* iter = node;

                    // Iterate until we're at the root (which we just color black) or
                    // until we the parent node is no longer red.
                    while ((root != iter) && iter->has_parent() && (iter->get_parent()->is_color_red(tree)))
                    {
                        node_t* parent      = iter->get_parent();
                        node_t* grandparent = helper_get_grandparent(iter);
                        node_t* uncle       = nullptr;

                        ASSERT(iter->is_color_red(tree));

                        bool uncle_is_left;
                        if (parent == grandparent->get_left())
                        {
                            uncle_is_left = false;
                            uncle         = grandparent->get_right();
                        }
                        else
                        {
                            uncle_is_left = true;
                            uncle         = grandparent->get_left();
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
                            if (uncle_is_left)
                            {
                                helper_rotate_left(root, grandparent);
                            }
                            else
                            {
                                helper_rotate_right(root, grandparent);
                            }
                        }
                    }

                    // Make sure the tree root is black (Case 1: Continued)
                    root->set_color_black(tree);
                }
            }

            bool insert(node_t*& root, tree_t* tree, u64 key, node_t* node)
            {
                bool ret;

                ASSERT(tree != nullptr);
                ASSERT(node != nullptr);

                node->clear();

                // Case 1: Simplest case -- tree is empty
                if (root == nullptr)
                {
                    ret  = true;
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
                node_t* left   = x->get_left();
                node_t* right  = x->get_right();
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
                node_t* x       = node;
                node_t* xp      = parent;
                s32     is_left = node_is_left;

                while (x != root && (x == nullptr || x->is_color_black(tree)))
                {
                    node_t* w = is_left ? xp->get_right() : xp->get_left(); /* Sibling */
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

                    node_t* wleft  = w != nullptr ? w->get_left() : nullptr;
                    node_t* wright = w != nullptr ? w->get_right() : nullptr;
                    if ((wleft == nullptr || wleft->is_color_black(tree)) && (wright == nullptr || wright->is_color_black(tree)))
                    {
                        // Case 2
                        if (w != nullptr)
                        {
                            w->set_color_red(tree);
                        }
                        x       = xp;
                        xp      = x->get_parent();
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
                        wleft  = w->get_left();
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

            bool remove(node_t*& root, tree_t* tree, node_t* node)
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
                    xp   = nullptr;
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

            void node_t::set_color(tree_t* t, s32 color) { t->m_set_color_f(this, color); }

            s32 node_t::get_color(tree_t* t) const { return t->m_get_color_f(this); }

            void node_t::set_color_black(tree_t* t) { t->m_set_color_f(this, COLOR_BLACK); }

            void node_t::set_color_red(tree_t* t) { t->m_set_color_f(this, COLOR_RED); }

            bool node_t::is_color_black(tree_t* t) const { return t->m_get_color_f(this) == COLOR_BLACK; }

            bool node_t::is_color_red(tree_t* t) const { return t->m_get_color_f(this) == COLOR_RED; }

            s32 validate(node_t* root, tree_t* tree, const char*& result)
            {
                s32 lh, rh;
                if (root == nullptr)
                {
                    return 1;
                }
                else
                {
                    node_t* ln = root->get_left();
                    node_t* rn = root->get_right();

                    // Consecutive red links
                    if (root->is_color_red(tree))
                    {
                        if ((ln != nullptr && ln->is_color_red(tree)) || (rn != nullptr && rn->is_color_red(tree)))
                        {
                            result = "Red violation";
                            return 0;
                        }
                    }

                    lh = validate(ln, tree, result);
                    rh = validate(rn, tree, result);

                    const u64 root_key = tree->m_get_key_f(root);

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

			bool get_min(node_t* proot, tree_t* tree, node_t*& found)
			{
				found = nullptr;
				node_t* pnode = proot;
				while (pnode != nullptr)
				{
					found = pnode;
					pnode = pnode->get_left();
				}
				return found != nullptr;
			}

        } // namespace pointer_based

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
            node_t* idx2ptr(xdexer* dexer, u32 idx)
            {
                if (idx == node_t::NIL)
                    return nullptr;
                return (node_t*)dexer->idx2ptr(idx);
            }

            u32 ptr2idx(xdexer* dexer, node_t* p)
            {
                if (p == nullptr)
                    return node_t::NIL;
                return dexer->ptr2idx(p);
            }

            bool clear(u32& iiterator, tree_t* tree, xdexer* dexer, u32& n)
            {
                //	Rotate away the left links so that we can treat this like the destruction of a linked list
                node_t* iterator = idx2ptr(dexer, iiterator);
                u32     iit      = iiterator;
                node_t* it       = iterator;
                iiterator        = node_t::NIL;
                iterator         = nullptr;
                while (it != nullptr)
                {
                    if (!it->has_left())
                    { // No left links, just kill the node and move on
                        iiterator = it->get_right();
                        it->clear();
                        n = iit;
                        return true;
                    }
                    else
                    { // Rotate away the left link and check again
                        iiterator = it->get_left();
                        iterator  = idx2ptr(dexer, iiterator);
                        it->set_left(iterator->get_right());
                        iterator->set_right(iit);
                    }
                    it        = iterator;
                    iit       = iiterator;
                    iterator  = nullptr;
                    iiterator = node_t::NIL;
                }
                return false;
            }

            bool find_specific(u32& root, tree_t* tree, xdexer* dexer, u64 data, u32& found, compare_f comparer)
            {
                bool ret;

                ASSERT(tree != nullptr);

                found = node_t::NIL;
                if (root == node_t::NIL)
                {
                    ret = false;
                    goto done;
                }

                node_t* node = idx2ptr(dexer, root);
                while (node != nullptr)
                {
                    s32 const c = comparer(data, node);
                    ASSERT(c == 0 || c == -1 || c == 1);
                    if (c == 0)
                        break;

                    u32 const ni = node->get_child((c + 1) >> 1);
                    node         = idx2ptr(dexer, ni);
                }

                if (node == nullptr)
                {
                    ret = false;
                    goto done;
                }

                // Return the node we found
                ret   = true;
                found = ptr2idx(dexer, node);

            done:
                return ret;
            }

            bool find(u32& root, tree_t* tree, xdexer* dexer, u64 data, u32& found)
            {
				return find_specific(root, tree, dexer, data, found, tree->m_compare_f);
			}

            bool upper(u32& root, tree_t* tree, xdexer* dexer, u64 data, u32& found)
            {
                bool ret;

                ASSERT(tree != nullptr);

                found = node_t::NIL;
                if (root == node_t::NIL)
                {
                    ret = false;
                    goto done;
                }

                node_t* node = idx2ptr(dexer, root);
                while (node != nullptr)
                {
                    s32 const c = tree->m_compare_f(data, node);
                    ASSERT(c == 0 || c == -1 || c == 1);
                    if (c == 0)
                        break;

                    u32 const ni = node->get_child((c + 1) >> 1);
                    if (ni == node_t::NIL)
                        break;
                    node = idx2ptr(dexer, ni);
                }

                // Return the node we found
                ret   = true;
                found = ptr2idx(dexer, node);

            done:
                return ret;
            }

            static inline u32 helper_get_sibling(node_t* node, u32 inode, tree_t* tree, xdexer* dexer)
            {
                u32 iparent = node->get_parent();
                if (iparent == node_t::NIL)
                    return node_t::NIL;
                node_t*   pparent = idx2ptr(dexer, iparent);
                s32 const c       = (inode == pparent->get_left()) ? 1 : 0;
                return pparent->get_child(c);
            }

            static inline u32 helper_get_grandparent(node_t* node, u32 inode, tree_t* tree, xdexer* dexer)
            {
                u32 iparent = node->get_parent();
                if (iparent == node_t::NIL)
                    return node_t::NIL;
                node_t* parent = idx2ptr(dexer, iparent);
                return parent->get_parent();
            }

            static inline u32 helper_get_uncle(node_t* node, u32 inode, tree_t* tree, xdexer* dexer)
            {
                u32 igrandparent = helper_get_grandparent(node, inode, tree, dexer);
                if (igrandparent == node_t::NIL)
                    return node_t::NIL;
                node_t*   grandparent = idx2ptr(dexer, igrandparent);
                const s32 c           = (node->get_parent() == grandparent->get_left()) ? 1 : 0;
                return grandparent->get_child(c);
            }

            static inline void helper_rotate_left(node_t*& root, u32& iroot, node_t* node, u32 inode, tree_t* tree, xdexer* dexer)
            {
                u32     ix = inode;
                node_t* x  = node;
                u32     iy = x->get_right();
                node_t* y  = idx2ptr(dexer, iy);
                x->set_right(y->get_left());
                if (y->has_left())
                {
                    u32     iyleft = y->get_left();
                    node_t* yleft  = idx2ptr(dexer, iyleft);
                    yleft->set_parent(ix);
                }

                y->set_parent(x->get_parent());
                if (!x->has_parent())
                {
                    root  = y;
                    iroot = iy;
                }
                else
                {
                    u32       ixp = x->get_parent();
                    node_t*   xp  = idx2ptr(dexer, ixp);
                    s32 const c   = (ix == xp->get_left()) ? 0 : 1;
                    xp->set_child(c, iy);
                }
                y->set_left(ix);
                x->set_parent(iy);
            }

            static inline void helper_rotate_right(node_t*& root, u32& iroot, node_t* node, u32 inode, tree_t* tree, xdexer* dexer)
            {
                u32     ix = inode;
                node_t* x  = node;
                u32     iy = x->get_left();
                node_t* y  = idx2ptr(dexer, iy);
                x->set_left(y->get_right());
                if (y->has_right())
                {
                    u32     iyright = y->get_right();
                    node_t* yright  = idx2ptr(dexer, iyright);
                    yright->set_parent(ix);
                }

                y->set_parent(x->get_parent());
                if (!x->has_parent())
                {
                    root  = y;
                    iroot = iy;
                }
                else
                {
                    u32       ixp = x->get_parent();
                    node_t*   xp  = idx2ptr(dexer, ixp);
                    s32 const c   = (ix == xp->get_left()) ? 0 : 1;
                    xp->set_child(c, iy);
                }

                y->set_right(ix);
                x->set_parent(iy);
            }

            static void helper_insert_rebalance(tree_t* tree, xdexer* dexer, node_t*& root, u32& iroot, node_t* node, u32 inode)
            {
                u32     inew_node_parent = node->get_parent();
                node_t* new_node_parent  = idx2ptr(dexer, inew_node_parent);

                if (new_node_parent != nullptr && new_node_parent->is_color_black(tree) == false)
                {
                    u32     iiter = inode;
                    node_t* iter  = node;

                    // Iterate until we're at the root (which we just color black) or
                    // until we the parent node is no longer red.
                    while ((root != iter) && iter->has_parent())
                    {
                        u32     iparent = iter->get_parent();
                        node_t* parent  = idx2ptr(dexer, iparent);
                        if (!parent->is_color_red(tree))
                            break;

                        u32     igrandparent = helper_get_grandparent(iter, iiter, tree, dexer);
                        node_t* grandparent  = idx2ptr(dexer, igrandparent);

                        u32     iuncle = node_t::NIL;
                        node_t* uncle  = nullptr;

                        ASSERT(iter->is_color_red(tree));

                        bool uncle_is_left;
                        if (iparent == grandparent->get_left())
                        {
                            uncle_is_left = false;
                            iuncle        = grandparent->get_right();
                        }
                        else
                        {
                            uncle_is_left = true;
                            iuncle        = grandparent->get_left();
                        }
                        uncle = idx2ptr(dexer, iuncle);

                        // Case 1: Uncle is not black
                        if (uncle && uncle->is_color_red(tree))
                        {
                            // Color parent and uncle black
                            parent->set_color_black(tree);
                            uncle->set_color_black(tree);

                            // Color Grandparent as Red
                            grandparent->set_color_red(tree);
                            iiter = igrandparent;
                            iter  = grandparent;
                            // Continue iteration, processing grandparent
                        }
                        else
                        {
                            // Case 2 - node's parent is red, but uncle is black
                            if (!uncle_is_left && parent->get_right() == iiter)
                            {
                                iiter = iter->get_parent();
                                iter  = idx2ptr(dexer, iiter);
                                helper_rotate_left(root, iroot, iter, iiter, tree, dexer);
                            }
                            else if (uncle_is_left && parent->get_left() == iiter)
                            {
                                iiter = iter->get_parent();
                                iter  = idx2ptr(dexer, iiter);
                                helper_rotate_right(root, iroot, iter, iiter, tree, dexer);
                            }

                            // Case 3 - Recolor and rotate
                            iparent = iter->get_parent();
                            parent  = idx2ptr(dexer, iparent);
                            parent->set_color_black(tree);

                            igrandparent = helper_get_grandparent(iter, iiter, tree, dexer);
                            grandparent  = idx2ptr(dexer, igrandparent);
                            grandparent->set_color_red(tree);
                            if (uncle_is_left)
                            {
                                helper_rotate_left(root, iroot, grandparent, igrandparent, tree, dexer);
                            }
                            else
                            {
                                helper_rotate_right(root, iroot, grandparent, igrandparent, tree, dexer);
                            }
                        }
                    }

                    // Make sure the tree root is black (Case 1: Continued)
                    root->set_color_black(tree);
                }
            }

            bool insert(u32& iroot, tree_t* tree, xdexer* dexer, u64 key, u32 inode)
            {
                bool ret;

                ASSERT(tree != nullptr);
                node_t* node = idx2ptr(dexer, inode);
                ASSERT(node != nullptr);
                node->clear();

                // Case 1: Simplest case -- tree is empty
                if (iroot == node_t::NIL)
                {
                    ret   = true;
                    iroot = inode;
                    node->set_color_black(tree);
                    goto done;
                }

                // Otherwise, insert the node as you would typically in a BST
                u32     ind = iroot;
                node_t* nd  = idx2ptr(dexer, iroot);
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
                            nd  = idx2ptr(dexer, ind);
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
                            nd  = idx2ptr(dexer, ind);
                        }
                    }
                }

                node->set_parent(ind);

                // Rebalance the tree about the node we just added
                node_t* root = idx2ptr(dexer, iroot);
                helper_insert_rebalance(tree, dexer, root, iroot, node, inode);

                ret = true;

            done:
                return ret;
            }

            static node_t* helper_find_minimum(node_t* node, tree_t* tree, xdexer* dexer)
            {
                node_t* x = node;
                while (x->has_left())
                {
                    u32 const ix = x->get_left();
                    x            = idx2ptr(dexer, ix);
                }
                return x;
            }

            static node_t* helper_find_maximum(node_t* node, tree_t* tree, xdexer* dexer)
            {
                node_t* x = node;
                while (x->has_right())
                {
                    u32 const ix = x->get_right();
                    x            = idx2ptr(dexer, ix);
                }
                return x;
            }

            static node_t* helper_find_successor(node_t* node, u32 inode, tree_t* tree, xdexer* dexer)
            {
                node_t* x  = node;
                u32     ix = inode;
                if (x->has_right())
                {
                    u32 const ir = x->get_right();
                    node_t*   r  = idx2ptr(dexer, ir);
                    return helper_find_minimum(r, tree, dexer);
                }

                u32     iy = x->get_parent();
                node_t* y  = idx2ptr(dexer, iy);
                while (y != nullptr && ix == y->get_right())
                {
                    x  = y;
                    ix = iy;
                    iy = y->get_parent();
                    y  = idx2ptr(dexer, iy);
                }
                return y;
            }

            static node_t* helper_find_predecessor(node_t* node, u32 inode, tree_t* tree, xdexer* dexer)
            {
                u32     ix = inode;
                node_t* x  = node;
                if (x->has_left())
                {
                    u32 const il = x->get_right();
                    node_t*   l  = idx2ptr(dexer, il);
                    return helper_find_maximum(l, tree, dexer);
                }

                u32     iy = x->get_parent();
                node_t* y  = idx2ptr(dexer, iy);
                while (y != nullptr && ix == y->get_left())
                {
                    x  = y;
                    iy = iy;
                    iy = y->get_parent();
                    y  = idx2ptr(dexer, iy);
                }
                return y;
            }

            // Replace x with y, inserting y where x previously was
            static void helper_swap_node(node_t*& root, u32& iroot, node_t* x, u32 ix, node_t* y, u32 iy, tree_t* tree, xdexer* dexer)
            {
                u32     ileft   = x->get_left();
                u32     iright  = x->get_right();
                u32     iparent = x->get_parent();
                node_t* left    = idx2ptr(dexer, ileft);
                node_t* right   = idx2ptr(dexer, iright);
                node_t* parent  = idx2ptr(dexer, iparent);

                y->set_parent(iparent);
                if (parent != nullptr)
                {
                    if (parent->get_left() == ix)
                    {
                        parent->set_left(iy);
                    }
                    else
                    {
                        parent->set_right(iy);
                    }
                }
                else
                {
                    if (root == x)
                    {
                        root  = y;
                        iroot = iy;
                    }
                }

                y->set_right(iright);
                if (right != nullptr)
                {
                    right->set_parent(iy);
                }
                x->set_right(node_t::NIL);
                y->set_left(ileft);
                if (left != nullptr)
                {
                    left->set_parent(iy);
                }
                x->set_left(node_t::NIL);

                y->set_color(tree, x->get_color(tree));
                x->set_parent(node_t::NIL);
            }

            static void helper_delete_rebalance(node_t*& root, u32 iroot, node_t* node, u32 inode, node_t* parent, u32 iparent, s32 node_is_left, tree_t* tree, xdexer* dexer)
            {
                u32     ix      = inode;
                node_t* x       = node;
                u32     ixp     = iparent;
                node_t* xp      = parent;
                s32     is_left = node_is_left;

                while (x != root && (x == nullptr || x->is_color_black(tree)))
                {
                    u32     iw = is_left ? xp->get_right() : xp->get_left(); // Sibling
                    node_t* w  = idx2ptr(dexer, iw);
                    if (w != nullptr && w->is_color_red(tree))
                    {
                        // Case 1
                        w->set_color_black(tree);
                        xp->set_color_red(tree);
                        if (is_left)
                        {
                            helper_rotate_left(root, iroot, xp, ixp, tree, dexer);
                        }
                        else
                        {
                            helper_rotate_right(root, iroot, xp, ixp, tree, dexer);
                        }
                        iw = is_left ? xp->get_right() : xp->get_left();
                        w  = idx2ptr(dexer, iw);
                    }

                    u32     iwleft  = (w != nullptr) ? w->get_left() : node_t::NIL;
                    u32     iwright = (w != nullptr) ? w->get_right() : node_t::NIL;
                    node_t* wleft   = idx2ptr(dexer, iwleft);
                    node_t* wright  = idx2ptr(dexer, iwright);

                    if ((wleft == nullptr || wleft->is_color_black(tree)) && (wright == nullptr || wright->is_color_black(tree)))
                    {
                        // Case 2
                        if (w != nullptr)
                        {
                            w->set_color_red(tree);
                        }
                        x       = xp;
                        ix      = ixp;
                        ixp     = x->get_parent();
                        xp      = idx2ptr(dexer, ixp);
                        is_left = xp && (ix == xp->get_left());
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
                            helper_rotate_right(root, iroot, w, iw, tree, dexer);
                            iw = xp->get_right();
                            w  = idx2ptr(dexer, iw);
                        }
                        else if (!is_left && (wleft == nullptr || wleft->is_color_black(tree)))
                        {
                            // Case 3b
                            w->set_color_red(tree);
                            if (wright)
                            {
                                wright->set_color_black(tree);
                            }
                            helper_rotate_left(root, iroot, w, iw, tree, dexer);
                            iw = xp->get_left();
                            w  = idx2ptr(dexer, iw);
                        }

                        // Case 4
                        iwleft  = w->get_left();
                        iwright = w->get_right();
                        wleft   = idx2ptr(dexer, iwleft);
                        wright  = idx2ptr(dexer, iwright);

                        w->set_color(tree, xp->get_color(tree));
                        xp->set_color_black(tree);

                        if (is_left && wright != nullptr)
                        {
                            wright->set_color_black(tree);
                            helper_rotate_left(root, iroot, xp, ixp, tree, dexer);
                        }
                        else if (!is_left && wleft != nullptr)
                        {
                            wleft->set_color_black(tree);
                            helper_rotate_right(root, iroot, xp, ixp, tree, dexer);
                        }
                        x  = root;
                        ix = iroot;
                    }
                }

                if (x != nullptr)
                {
                    x->set_color_black(tree);
                }
            }

            bool remove(u32& iroot, tree_t* tree, xdexer* dexer, u32 inode)
            {
                bool const ret = true;

                ASSERT(tree != nullptr);

                node_t* node = idx2ptr(dexer, inode);
                node_t* root = idx2ptr(dexer, iroot);

                u32     iy;
                node_t* y;
                if (!node->has_left() || !node->has_right())
                {
                    y  = node;
                    iy = inode;
                }
                else
                {
                    y  = helper_find_successor(node, inode, tree, dexer);
                    iy = ptr2idx(dexer, y);
                }

                u32     ix;
                node_t* x;
                if (y->has_left())
                {
                    ix = y->get_left();
                    x  = idx2ptr(dexer, ix);
                }
                else
                {
                    ix = y->get_right();
                    x  = idx2ptr(dexer, ix);
                }

                u32 ixp;
                if (x != nullptr)
                {
                    ixp = y->get_parent();
                    x->set_parent(ixp);
                }
                else
                {
                    ixp = y->get_parent();
                }
                node_t* xp = idx2ptr(dexer, ixp);

                s32 is_left = 0;
                if (!y->has_parent())
                {
                    root  = x;
                    iroot = ix;
                    ixp   = node_t::NIL;
                    xp    = nullptr;
                }
                else
                {
                    u32     iyp = y->get_parent();
                    node_t* yp  = idx2ptr(dexer, iyp);
                    if (iy == yp->get_left())
                    {
                        yp->set_left(ix);
                        is_left = 1;
                    }
                    else
                    {
                        yp->set_right(ix);
                        is_left = 0;
                    }
                }

                s32 const y_color = y->get_color(tree);

                // Swap in the node
                if (y != node)
                {
                    helper_swap_node(root, iroot, node, inode, y, iy, tree, dexer);
                    if (xp == node)
                    {
                        xp  = y;
                        ixp = iy;
                    }
                }

                if (y_color == COLOR_BLACK)
                {
                    helper_delete_rebalance(root, iroot, x, ix, xp, ixp, is_left, tree, dexer);
                }

                node->clear();
                return ret;
            }

            void node_t::set_color(tree_t* t, s32 color) { t->m_set_color_f(this, color); }
            s32  node_t::get_color(tree_t* t) const { return t->m_get_color_f(this); }
            void node_t::set_color_black(tree_t* t) { t->m_set_color_f(this, COLOR_BLACK); }
            void node_t::set_color_red(tree_t* t) { t->m_set_color_f(this, COLOR_RED); }
            bool node_t::is_color_black(tree_t* t) const { return t->m_get_color_f(this) == COLOR_BLACK; }
            bool node_t::is_color_red(tree_t* t) const { return t->m_get_color_f(this) == COLOR_RED; }

            s32 validate(node_t* root, u32 iroot, tree_t* tree, xdexer* dexer, const char*& result)
            {
                s32 lh, rh;
                if (root == nullptr)
                {
                    return 1;
                }
                else
                {
                    u32     iln = root->get_left();
                    u32     irn = root->get_right();
                    node_t* ln  = idx2ptr(dexer, iln);
                    node_t* rn  = idx2ptr(dexer, irn);

                    // Consecutive red links
                    if (root->is_color_red(tree))
                    {
                        if ((ln != nullptr && ln->is_color_red(tree)) || (rn != nullptr && rn->is_color_red(tree)))
                        {
                            result = "Red violation";
                            return 0;
                        }
                    }

                    lh = validate(ln, iln, tree, dexer, result);
                    rh = validate(rn, irn, tree, dexer, result);

                    const u64 root_key = tree->m_get_key_f(root);

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

            bool get_min(u32 iroot, tree_t* tree, xdexer* dexer, u32& found)
			{
				found = 0xffffffff;
				u32 inode = iroot;
                node_t* pnode = idx2ptr(dexer, iroot);
                while (pnode != nullptr)
                {
					found = inode;
                    inode = pnode->get_left();
                    pnode  = idx2ptr(dexer, inode);
                }
				return found != 0xffffffff;
			}

        } // namespace index_based
    }     // namespace xbst
} // namespace xcore