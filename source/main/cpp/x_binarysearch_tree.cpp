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
            inline node_t* get_parent(node_t* n) { return n->parent; }
            inline bool    has_parent(node_t* n) { return n->parent != nullptr; }
            inline void    set_parent(node_t* n, node_t* p) { n->parent = p; }

            inline void set_left(node_t* n, node_t* child) { n->children[0] = child; }
            inline void set_right(node_t* n, node_t* child) { n->children[1] = child; }
            inline void set_child(node_t* n, s32 c, node_t* child) { n->children[c] = child; }

            inline bool has_left(node_t* n) { return n->children[0] != nullptr; }
            inline bool has_right(node_t* n) { return n->children[1] != nullptr; }
            inline bool has_child(node_t* n, s32 c) 
            {
                ASSERT(c == LEFT || c == RIGHT);
                return n->children[c] != nullptr;
            }

            inline node_t* get_left(node_t* n) { return n->children[0]; }
            inline node_t* get_right(node_t* n) { return n->children[1]; }
            inline node_t* get_child(node_t* n, s32 c) 
            {
                ASSERT(c == LEFT || c == RIGHT);
                return n->children[c];
            }

            inline void set_color(node_t* n, tree_t* t, s32 color);
            inline s32  get_color(node_t* n, tree_t* t) ;
            inline void set_color_black(node_t* n, tree_t* t);
            inline void set_color_red(node_t* n, tree_t* t);
            inline bool is_color_black(node_t* n, tree_t* t) ;
            inline bool is_color_red(node_t* n, tree_t* t) ;

            bool clear(node_t*& iterator, tree_t* tree, node_t*& n)
            {
                // Rotate away the left links so that we can treat this like the destruction of a linked list
                node_t* it = iterator;
                iterator   = nullptr;
                while (it != nullptr)
                {
                    if (!has_left(it))
                    { // No left links, just kill the node and move on
                        iterator = get_right(it);
                        it->clear();
                        n = it;
                        return true;
                    }
                    else
                    { // Rotate away the left link and check again
                        iterator = get_left(it);
                        set_left(it, get_right(iterator));
                        set_right(iterator, it);
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
                    return ret;
                }

                node_t* node = root;
                while (node != nullptr)
                {
                    s32 const c = tree->m_compare_f(data, node);
                    ASSERT(c == 0 || c == -1 || c == 1);
                    if (c == 0)
                        break;
                    node = get_child(node, (c + 1) >> 1);
                }

                if (node == nullptr)
                {
                    ret = false;
                    return ret;
                }

                // Return the node we found
                ret   = true;
                found = node;

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
                    return ret;
                }

                node_t* node = root;
                while (node != nullptr)
                {
                    s32 const c = tree->m_compare_f(data, node);
                    ASSERT(c == 0 || c == -1 || c == 1);
                    node_t* child = nullptr;
                    if (c == 0)
                        break;

                    child = get_child(node, (c + 1) >> 1);
                    if (child == nullptr)
                        break;
                    node = child;
                }

                ret   = true;
                found = node;

                return ret;
            }

            static inline node_t* helper_get_sibling(node_t* node)
            {
                node_t* parent = get_parent(node);
                if (parent == nullptr)
                    return nullptr;
                s32 const c = (node == get_left(parent)) ? 1 : 0;
                return get_child(parent, c);
            }

            static inline node_t* helper_get_grandparent(node_t* node)
            {
                node_t* parent_node = get_parent(node);
                if (parent_node == nullptr)
                    return nullptr;
                return get_parent(parent_node);
            }

            static inline node_t* helper_get_uncle(node_t* node)
            {
                node_t* grandparent = helper_get_grandparent(node);
                if (grandparent == nullptr)
                    return nullptr;
                const s32 c = (get_parent(node) == get_left(grandparent)) ? 1 : 0;
                return get_child(grandparent, c);
            }

            static inline void helper_rotate_left(node_t*& root, node_t* node)
            {
                node_t* x = node;
                node_t* y = get_right(x);
                set_right(x, get_left(y));
                if (has_left(y))
                {
                    node_t* yleft = get_left(y);
                    set_parent(yleft, x);
                }

                set_parent(y, get_parent(x));
                if (!has_parent(x))
                {
                    root = y;
                }
                else
                {
                    node_t*   xp = get_parent(x);
                    s32 const c  = (x == get_left(xp)) ? 0 : 1;
                    set_child(xp, c, y);
                }
                set_left(y, x);
                set_parent(x, y);
            }

            static inline void helper_rotate_right(node_t*& root, node_t* node)
            {
                node_t* x = node;
                node_t* y = get_left(x);
                set_left(x, get_right(y));

                if (has_right(y))
                {
                    node_t* yright = get_right(y);
                    set_parent(yright, x);
                }

                set_parent(y, get_parent(x));
                if (!has_parent(x))
                {
                    root = y;
                }
                else
                {
                    node_t*   xp = get_parent(x);
                    s32 const c  = (x == get_left(xp)) ? 0 : 1;
                    set_child(xp, c, y);
                }

                set_right(y, x);
                set_parent(x, y);
            }

            static void helper_insert_rebalance(tree_t* tree, node_t*& root, node_t* node)
            {
                node_t* new_node_parent = get_parent(node);

                if (new_node_parent != nullptr && is_color_black(new_node_parent, tree) == false)
                {
                    node_t* iter = node;

                    // Iterate until we're at the root (which we just color black) or
                    // until we the parent node is no longer red.
                    while ((root != iter) && has_parent(iter) && (is_color_red(get_parent(iter), tree)))
                    {
                        node_t* parent      = get_parent(iter);
                        node_t* grandparent = helper_get_grandparent(iter);
                        node_t* uncle       = nullptr;

                        ASSERT(is_color_red(iter, tree));

                        bool uncle_is_left;
                        if (parent == get_left(grandparent))
                        {
                            uncle_is_left = false;
                            uncle         = get_right(grandparent);
                        }
                        else
                        {
                            uncle_is_left = true;
                            uncle         = get_left(grandparent);
                        }

                        // Case 1: Uncle is not black
                        if (uncle && is_color_red(uncle, tree))
                        {
                            // Color parent and uncle black
                            set_color_black(parent, tree);
                            set_color_black(uncle, tree);

                            // Color Grandparent as Red
                            set_color_red(grandparent, tree);
                            iter = grandparent;
                            // Continue iteration, processing grandparent
                        }
                        else
                        {
                            // Case 2 - node's parent is red, but uncle is black
                            if (!uncle_is_left && get_right(parent) == iter)
                            {
                                iter = get_parent(iter);
                                helper_rotate_left(root, iter);
                            }
                            else if (uncle_is_left && get_left(parent) == iter)
                            {
                                iter = get_parent(iter);
                                helper_rotate_right(root, iter);
                            }

                            // Case 3 - Recolor and rotate
                            parent = get_parent(iter);
                            set_color_black(parent, tree);

                            grandparent = helper_get_grandparent(iter);
                            set_color_red(grandparent, tree);
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
                    set_color_black(root, tree);
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
                    set_color_black(node, tree);
                    return ret;
                }

                // Otherwise, insert the node as you would typically in a BST
                node_t* nd = root;
                set_color_red(node, tree);

                // Insert a node into the tree as you normally would
                while (nd != nullptr)
                {
                    s32 const c = tree->m_compare_f(key, nd);
                    if (c == 0)
                    {
                        ret = false;
                        return ret;
                    }

                    if (c < 0)
                    {
                        if (!has_left(nd))
                        {
                            set_left(nd, node);
                            break;
                        }
                        else
                        {
                            nd = get_left(nd);
                        }
                    }
                    else
                    {
                        if (!has_right(nd))
                        {
                            set_right(nd, node);
                            break;
                        }
                        else
                        {
                            nd = get_right(nd);
                        }
                    }
                }

                set_parent(node, nd);

                // Rebalance the tree about the node we just added
                helper_insert_rebalance(tree, root, node);

                ret = true;
                return ret;
            }

            static node_t* helper_find_minimum(node_t* node)
            {
                node_t* x = node;
                while (has_left(x))
                {
                    x = get_left(x);
                }
                return x;
            }

            static node_t* helper_find_maximum(node_t* node)
            {
                node_t* x = node;
                while (has_right(x))
                {
                    x = get_right(x);
                }
                return x;
            }

            static node_t* helper_find_successor(node_t* node)
            {
                node_t* x = node;
                if (has_right(x))
                {
                    return helper_find_minimum(get_right(x));
                }

                node_t* y = get_parent(x);
                while (y != nullptr && x == get_right(y))
                {
                    x = y;
                    y = get_parent(y);
                }
                return y;
            }

            static node_t* helper_find_predecessor(node_t* node)
            {
                node_t* x = node;
                if (has_left(x))
                {
                    return helper_find_maximum(get_left(x));
                }

                node_t* y = get_parent(x);
                while (y != nullptr && x == get_left(y))
                {
                    x = y;
                    y = get_parent(y);
                }
                return y;
            }

            // Replace x with y, inserting y where x previously was
            static void helper_swap_node(tree_t* tree, node_t*& root, node_t* x, node_t* y)
            {
                node_t* left   = get_left(x);
                node_t* right  = get_right(x);
                node_t* parent = get_parent(x);

                set_parent(y, parent);
                if (parent != nullptr)
                {
                    if (get_left(parent) == x)
                    {
                        set_left(parent, y);
                    }
                    else
                    {
                        set_right(parent, y);
                    }
                }
                else
                {
                    if (root == x)
                    {
                        root = y;
                    }
                }

                set_right(y, right);
                if (right != nullptr)
                {
                    set_parent(right, y);
                }
                set_right(x, nullptr);
                set_left(y, left);
                if (left != nullptr)
                {
                    set_parent(left, y);
                }
                set_left(x, nullptr);

                set_color(y, tree, get_color(x, tree));
                set_parent(x, nullptr);
            }

            static void helper_delete_rebalance(tree_t* tree, node_t*& root, node_t* node, node_t* parent, s32 node_is_left)
            {
                node_t* x       = node;
                node_t* xp      = parent;
                s32     is_left = node_is_left;

                while (x != root && (x == nullptr || is_color_black(x, tree)))
                {
                    node_t* w = is_left ? get_right(xp) : get_left(xp); /* Sibling */
                    if (w != nullptr && is_color_red(w, tree))
                    {
                        // Case 1
                        set_color_black(w, tree);
                        set_color_red(xp, tree);
                        if (is_left)
                        {
                            helper_rotate_left(root, xp);
                        }
                        else
                        {
                            helper_rotate_right(root, xp);
                        }
                        w = is_left ? get_right(xp) : get_left(xp);
                    }

                    node_t* wleft  = w != nullptr ? get_left(w) : nullptr;
                    node_t* wright = w != nullptr ? get_right(w) : nullptr;
                    if ((wleft == nullptr || is_color_black(wleft, tree)) && (wright == nullptr || is_color_black(wright, tree)))
                    {
                        // Case 2
                        if (w != nullptr)
                        {
                            set_color_red(w, tree);
                        }
                        x       = xp;
                        xp      = get_parent(x);
                        is_left = xp && (x == get_left(xp));
                    }
                    else
                    {
                        if (is_left && (wright == nullptr || is_color_black(wright, tree)))
                        {
                            // Case 3a
                            set_color_red(w, tree);
                            if (wleft)
                            {
                                set_color_black(wleft, tree);
                            }
                            helper_rotate_right(root, w);
                            w = get_right(xp);
                        }
                        else if (!is_left && (wleft == nullptr || is_color_black(wleft, tree)))
                        {
                            // Case 3b
                            set_color_red(w, tree);
                            if (wright)
                            {
                                set_color_black(wright, tree);
                            }
                            helper_rotate_left(root, w);
                            w = get_left(xp);
                        }

                        // Case 4
                        wleft  = get_left(w);
                        wright = get_right(w);

                        set_color(w, tree, get_color(xp, tree));
                        set_color_black(xp, tree);

                        if (is_left && wright != nullptr)
                        {
                            set_color_black(wright, tree);
                            helper_rotate_left(root, xp);
                        }
                        else if (!is_left && wleft != nullptr)
                        {
                            set_color_black(wleft, tree);
                            helper_rotate_right(root, xp);
                        }
                        x = root;
                    }
                }

                if (x != nullptr)
                {
                    set_color_black(x, tree);
                }
            }

            bool remove(node_t*& root, tree_t* tree, node_t* node)
            {
                bool const ret = true;

                ASSERT(tree != nullptr);
                ASSERT(node != nullptr);

                node_t* y;
                if (!has_left(node) || !has_right(node))
                {
                    y = node;
                }
                else
                {
                    y = helper_find_successor(node);
                }

                node_t* x;
                if (has_left(y))
                {
                    x = get_left(y);
                }
                else
                {
                    x = get_right(y);
                }

                node_t* xp;
                if (x != nullptr)
                {
                    xp = get_parent(y);
                    set_parent(x, xp);
                }
                else
                {
                    xp = get_parent(y);
                }

                s32 is_left = 0;
                if (!has_parent(y))
                {
                    root = x;
                    xp   = nullptr;
                }
                else
                {
                    node_t* yp = get_parent(y);
                    if (y == get_left(yp))
                    {
                        set_left(yp, x);
                        is_left = 1;
                    }
                    else
                    {
                        set_right(yp, x);
                        is_left = 0;
                    }
                }

                s32 const y_color = get_color(y, tree);

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

            void set_color(node_t* n, tree_t* t, s32 color) { t->m_set_color_f(n, color); }
            s32  get_color(node_t* n, tree_t* t) { return t->m_get_color_f(n); }
            void set_color_black(node_t* n, tree_t* t) { t->m_set_color_f(n, COLOR_BLACK); }
            void set_color_red(node_t* n, tree_t* t) { t->m_set_color_f(n, COLOR_RED); }
            bool is_color_black(node_t* n, tree_t* t) { return t->m_get_color_f(n) == COLOR_BLACK; }
            bool is_color_red(node_t* n, tree_t* t) { return t->m_get_color_f(n) == COLOR_RED; }

            s32 validate(node_t* root, tree_t* tree, const char*& result)
            {
                s32 lh, rh;
                if (root == nullptr)
                {
                    return 1;
                }
                else
                {
                    node_t* ln = get_left(root);
                    node_t* rn = get_right(root);

                    // Consecutive red links
                    if (is_color_red(root, tree))
                    {
                        if ((ln != nullptr && is_color_red(ln, tree)) || (rn != nullptr && is_color_red(rn, tree)))
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
                        return is_color_red(root, tree) ? lh : lh + 1;
                    }
                    return 0;
                }
            }

            bool get_min(node_t* proot, tree_t* tree, node_t*& found)
            {
                found         = nullptr;
                node_t* pnode = proot;
                while (pnode != nullptr)
                {
                    found = pnode;
                    pnode = get_left(pnode);
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
            node_t* idx2ptr(dexer_t* dexer, u32 idx)
            {
                if (idx == NIL)
                    return nullptr;
                return (node_t*)dexer->idx2ptr(idx);
            }

            u32 ptr2idx(dexer_t* dexer, node_t* p)
            {
                if (p == nullptr)
                    return NIL;
                return dexer->ptr2idx(p);
            }

            u32  get_parent(node_t* n) { return n->parent; }
            bool has_parent(node_t* n) { return n->parent != NIL; }
            void set_parent(node_t* n, u32 p) { n->parent = p; }

            void set_left(node_t* n, u32 child) { n->children[0] = child; }
            void set_right(node_t* n, u32 child) { n->children[1] = child; }
            void set_child(node_t* n, s32 c, u32 child) { n->children[c] = child; }

            bool has_left(node_t* n) { return n->children[0] != NIL; }
            bool has_right(node_t* n) { return n->children[1] != NIL; }
            bool has_child(node_t* n, s32 c)
            {
                ASSERT(c == LEFT || c == RIGHT);
                return n->children[c] != NIL;
            }

            u32 get_left(node_t* n) { return n->children[0]; }
            u32 get_right(node_t* n) { return n->children[1]; }
            u32 get_child(node_t* n, s32 c)
            {
                ASSERT(c == LEFT || c == RIGHT);
                return n->children[c];
            }

            void set_color(node_t* n, tree_t* t, s32 color);
            s32  get_color(node_t* n, tree_t* t) ;
            void set_color_black(node_t* n, tree_t* t);
            void set_color_red(node_t* n, tree_t* t);
            bool is_color_black(node_t* n, tree_t* t) ;
            bool is_color_red(node_t* n, tree_t* t) ;

            bool clear(u32& iiterator, tree_t* tree, dexer_t* dexer, u32& n)
            {
                //	Rotate away the left links so that we can treat this like the destruction of a linked list
                node_t* iterator = idx2ptr(dexer, iiterator);
                u32     iit      = iiterator;
                node_t* it       = iterator;
                iiterator        = NIL;
                iterator         = nullptr;
                while (it != nullptr)
                {
                    if (!has_left(it))
                    { // No left links, just kill the node and move on
                        iiterator = get_right(it);
                        it->clear();
                        n = iit;
                        return true;
                    }
                    else
                    { // Rotate away the left link and check again
                        iiterator = get_left(it);
                        iterator  = idx2ptr(dexer, iiterator);
                        set_left(it, get_right(iterator));
                        set_right(iterator, iit);
                    }
                    it        = iterator;
                    iit       = iiterator;
                    iterator  = nullptr;
                    iiterator = NIL;
                }
                return false;
            }

            bool find_specific(u32& root, tree_t* tree, dexer_t* dexer, u64 data, u32& found, compare_f comparer)
            {
                bool ret;

                ASSERT(tree != nullptr);

                found = NIL;
                if (root == NIL)
                {
                    ret = false;
                    return ret;
                }

                node_t* node = idx2ptr(dexer, root);
                while (node != nullptr)
                {
                    s32 const c = comparer(data, node);
                    ASSERT(c == 0 || c == -1 || c == 1);
                    if (c == 0)
                        break;

                    u32 const ni = get_child(node, (c + 1) >> 1);
                    node         = idx2ptr(dexer, ni);
                }

                if (node == nullptr)
                {
                    ret = false;
                    return ret;
                }

                // Return the node we found
                ret   = true;
                found = ptr2idx(dexer, node);

                return ret;
            }

            bool find(u32& root, tree_t* tree, dexer_t* dexer, u64 data, u32& found) { return find_specific(root, tree, dexer, data, found, tree->m_compare_f); }

            bool upper(u32& root, tree_t* tree, dexer_t* dexer, u64 data, u32& found)
            {
                bool ret;

                ASSERT(tree != nullptr);

                found = NIL;
                if (root == NIL)
                {
                    ret = false;
                    return ret;
                }

                node_t* node = idx2ptr(dexer, root);
                while (node != nullptr)
                {
                    s32 const c = tree->m_compare_f(data, node);
                    ASSERT(c == 0 || c == -1 || c == 1);
                    if (c == 0)
                        break;

                    u32 const ni = get_child(node, (c + 1) >> 1);
                    if (ni == NIL)
                        break;
                    node = idx2ptr(dexer, ni);
                }

                // Return the node we found
                ret   = true;
                found = ptr2idx(dexer, node);

                return ret;
            }

            static inline u32 helper_get_sibling(node_t* node, u32 inode, tree_t* tree, dexer_t* dexer)
            {
                u32 iparent = get_parent(node);
                if (iparent == NIL)
                    return NIL;
                node_t*   pparent = idx2ptr(dexer, iparent);
                s32 const c       = (inode == get_left(pparent)) ? 1 : 0;
                return get_child(pparent, c);
            }

            static inline u32 helper_get_grandparent(node_t* node, u32 inode, tree_t* tree, dexer_t* dexer)
            {
                u32 iparent = get_parent(node);
                if (iparent == NIL)
                    return NIL;
                node_t* parent = idx2ptr(dexer, iparent);
                return get_parent(parent);
            }

            static inline u32 helper_get_uncle(node_t* node, u32 inode, tree_t* tree, dexer_t* dexer)
            {
                u32 igrandparent = helper_get_grandparent(node, inode, tree, dexer);
                if (igrandparent == NIL)
                    return NIL;
                node_t*   grandparent = idx2ptr(dexer, igrandparent);
                const s32 c           = (get_parent(node) == get_left(grandparent)) ? 1 : 0;
                return get_child(grandparent, c);
            }

            static inline void helper_rotate_left(node_t*& root, u32& iroot, node_t* node, u32 inode, tree_t* tree, dexer_t* dexer)
            {
                u32     ix = inode;
                node_t* x  = node;
                u32     iy = get_right(x);
                node_t* y  = idx2ptr(dexer, iy);
                set_right(x, get_left(y));
                if (has_left(y))
                {
                    u32     iyleft = get_left(y);
                    node_t* yleft  = idx2ptr(dexer, iyleft);
                    set_parent(yleft, ix);
                }

                set_parent(y, get_parent(x));
                if (!has_parent(x))
                {
                    root  = y;
                    iroot = iy;
                }
                else
                {
                    u32       ixp = get_parent(x);
                    node_t*   xp  = idx2ptr(dexer, ixp);
                    s32 const c   = (ix == get_left(xp)) ? 0 : 1;
                    set_child(xp, c, iy);
                }
                set_left(y, ix);
                set_parent(x, iy);
            }

            static inline void helper_rotate_right(node_t*& root, u32& iroot, node_t* node, u32 inode, tree_t* tree, dexer_t* dexer)
            {
                u32     ix = inode;
                node_t* x  = node;
                u32     iy = get_left(x);
                node_t* y  = idx2ptr(dexer, iy);
                set_left(x, get_right(y));
                if (has_right(y))
                {
                    u32     iyright = get_right(y);
                    node_t* yright  = idx2ptr(dexer, iyright);
                    set_parent(yright, ix);
                }

                set_parent(y, get_parent(x));
                if (!has_parent(x))
                {
                    root  = y;
                    iroot = iy;
                }
                else
                {
                    u32       ixp = get_parent(x);
                    node_t*   xp  = idx2ptr(dexer, ixp);
                    s32 const c   = (ix == get_left(xp)) ? 0 : 1;
                    set_child(xp, c, iy);
                }

                set_right(y, ix);
                set_parent(x, iy);
            }

            static void helper_insert_rebalance(tree_t* tree, dexer_t* dexer, node_t*& root, u32& iroot, node_t* node, u32 inode)
            {
                u32     inew_node_parent = get_parent(node);
                node_t* new_node_parent  = idx2ptr(dexer, inew_node_parent);

                if (new_node_parent != nullptr && is_color_black(new_node_parent, tree) == false)
                {
                    u32     iiter = inode;
                    node_t* iter  = node;

                    // Iterate until we're at the root (which we just color black) or
                    // until we the parent node is no longer red.
                    while ((root != iter) && has_parent(iter))
                    {
                        u32     iparent = get_parent(iter);
                        node_t* parent  = idx2ptr(dexer, iparent);
                        if (!is_color_red(parent, tree))
                            break;

                        u32     igrandparent = helper_get_grandparent(iter, iiter, tree, dexer);
                        node_t* grandparent  = idx2ptr(dexer, igrandparent);

                        u32     iuncle = NIL;
                        node_t* uncle  = nullptr;

                        ASSERT(is_color_red(iter, tree));

                        bool uncle_is_left;
                        if (iparent == get_left(grandparent))
                        {
                            uncle_is_left = false;
                            iuncle        = get_right(grandparent);
                        }
                        else
                        {
                            uncle_is_left = true;
                            iuncle        = get_left(grandparent);
                        }
                        uncle = idx2ptr(dexer, iuncle);

                        // Case 1: Uncle is not black
                        if (uncle && is_color_red(uncle, tree))
                        {
                            // Color parent and uncle black
                            set_color_black(parent, tree);
                            set_color_black(uncle, tree);

                            // Color Grandparent as Red
                            set_color_red(grandparent, tree);
                            iiter = igrandparent;
                            iter  = grandparent;
                            // Continue iteration, processing grandparent
                        }
                        else
                        {
                            // Case 2 - node's parent is red, but uncle is black
                            if (!uncle_is_left && get_right(parent) == iiter)
                            {
                                iiter = get_parent(iter);
                                iter  = idx2ptr(dexer, iiter);
                                helper_rotate_left(root, iroot, iter, iiter, tree, dexer);
                            }
                            else if (uncle_is_left && get_left(parent) == iiter)
                            {
                                iiter = get_parent(iter);
                                iter  = idx2ptr(dexer, iiter);
                                helper_rotate_right(root, iroot, iter, iiter, tree, dexer);
                            }

                            // Case 3 - Recolor and rotate
                            iparent = get_parent(iter);
                            parent  = idx2ptr(dexer, iparent);
                            set_color_black(parent, tree);

                            igrandparent = helper_get_grandparent(iter, iiter, tree, dexer);
                            grandparent  = idx2ptr(dexer, igrandparent);
                            set_color_red(grandparent, tree);
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
                    set_color_black(root, tree);
                }
            }

            bool insert(u32& iroot, tree_t* tree, dexer_t* dexer, u64 key, u32 inode)
            {
                bool ret;

                ASSERT(tree != nullptr);
                node_t* node = idx2ptr(dexer, inode);
                ASSERT(node != nullptr);
                node->clear();

                // Case 1: Simplest case -- tree is empty
                if (iroot == NIL)
                {
                    ret   = true;
                    iroot = inode;
                    set_color_black(node, tree);
                    return ret;
                }

                // Otherwise, insert the node as you would typically in a BST
                u32     ind = iroot;
                node_t* nd  = idx2ptr(dexer, iroot);
                set_color_red(node, tree);

                // Insert a node into the tree as you normally would
                while (nd != nullptr)
                {
                    s32 const c = tree->m_compare_f(key, nd);
                    if (c == 0)
                    {
                        ret = false;
                        return ret;
                    }

                    if (c < 0)
                    {
                        if (!has_left(nd))
                        {
                            set_left(nd, inode);
                            break;
                        }
                        else
                        {
                            ind = get_left(nd);
                            nd  = idx2ptr(dexer, ind);
                        }
                    }
                    else
                    {
                        if (!has_right(nd))
                        {
                            set_right(nd, inode);
                            break;
                        }
                        else
                        {
                            ind = get_right(nd);
                            nd  = idx2ptr(dexer, ind);
                        }
                    }
                }

                set_parent(node, ind);

                // Rebalance the tree about the node we just added
                node_t* root = idx2ptr(dexer, iroot);
                helper_insert_rebalance(tree, dexer, root, iroot, node, inode);

                ret = true;
                return ret;
            }

            static node_t* helper_find_minimum(node_t* node, tree_t* tree, dexer_t* dexer)
            {
                node_t* x = node;
                while (has_left(x))
                {
                    u32 const ix = get_left(x);
                    x            = idx2ptr(dexer, ix);
                }
                return x;
            }

            static node_t* helper_find_maximum(node_t* node, tree_t* tree, dexer_t* dexer)
            {
                node_t* x = node;
                while (has_right(x))
                {
                    u32 const ix = get_right(x);
                    x            = idx2ptr(dexer, ix);
                }
                return x;
            }

            static node_t* helper_find_successor(node_t* node, u32 inode, tree_t* tree, dexer_t* dexer)
            {
                node_t* x  = node;
                u32     ix = inode;
                if (has_right(x))
                {
                    u32 const ir = get_right(x);
                    node_t*   r  = idx2ptr(dexer, ir);
                    return helper_find_minimum(r, tree, dexer);
                }

                u32     iy = get_parent(x);
                node_t* y  = idx2ptr(dexer, iy);
                while (y != nullptr && ix == get_right(y))
                {
                    x  = y;
                    ix = iy;
                    iy = get_parent(y);
                    y  = idx2ptr(dexer, iy);
                }
                return y;
            }

            static node_t* helper_find_predecessor(node_t* node, u32 inode, tree_t* tree, dexer_t* dexer)
            {
                u32     ix = inode;
                node_t* x  = node;
                if (has_left(x))
                {
                    u32 const il = get_right(x);
                    node_t*   l  = idx2ptr(dexer, il);
                    return helper_find_maximum(l, tree, dexer);
                }

                u32     iy = get_parent(x);
                node_t* y  = idx2ptr(dexer, iy);
                while (y != nullptr && ix == get_left(y))
                {
                    x  = y;
                    iy = iy;
                    iy = get_parent(y);
                    y  = idx2ptr(dexer, iy);
                }
                return y;
            }

            // Replace x with y, inserting y where x previously was
            static void helper_swap_node(node_t*& root, u32& iroot, node_t* x, u32 ix, node_t* y, u32 iy, tree_t* tree, dexer_t* dexer)
            {
                u32     ileft   = get_left(x);
                u32     iright  = get_right(x);
                u32     iparent = get_parent(x);
                node_t* left    = idx2ptr(dexer, ileft);
                node_t* right   = idx2ptr(dexer, iright);
                node_t* parent  = idx2ptr(dexer, iparent);

                set_parent(y, iparent);
                if (parent != nullptr)
                {
                    if (get_left(parent) == ix)
                    {
                        set_left(parent, iy);
                    }
                    else
                    {
                        set_right(parent, iy);
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

                set_right(y, iright);
                if (right != nullptr)
                {
                    set_parent(right, iy);
                }
                set_right(x, NIL);
                set_left(y, ileft);
                if (left != nullptr)
                {
                    set_parent(left, iy);
                }
                set_left(x, NIL);

                set_color(y, tree, get_color(x, tree));
                set_parent(x, NIL);
            }

            static void helper_delete_rebalance(node_t*& root, u32 iroot, node_t* node, u32 inode, node_t* parent, u32 iparent, s32 node_is_left, tree_t* tree, dexer_t* dexer)
            {
                u32     ix      = inode;
                node_t* x       = node;
                u32     ixp     = iparent;
                node_t* xp      = parent;
                s32     is_left = node_is_left;

                while (x != root && (x == nullptr || is_color_black(x, tree)))
                {
                    u32     iw = is_left ? get_right(xp) : get_left(xp); // Sibling
                    node_t* w  = idx2ptr(dexer, iw);
                    if (w != nullptr && is_color_red(w, tree))
                    {
                        // Case 1
                        set_color_black(w, tree);
                        set_color_red(xp, tree);
                        if (is_left)
                        {
                            helper_rotate_left(root, iroot, xp, ixp, tree, dexer);
                        }
                        else
                        {
                            helper_rotate_right(root, iroot, xp, ixp, tree, dexer);
                        }
                        iw = is_left ? get_right(xp) : get_left(xp);
                        w  = idx2ptr(dexer, iw);
                    }

                    u32     iwleft  = (w != nullptr) ? get_left(w) : NIL;
                    u32     iwright = (w != nullptr) ? get_right(w) : NIL;
                    node_t* wleft   = idx2ptr(dexer, iwleft);
                    node_t* wright  = idx2ptr(dexer, iwright);

                    if ((wleft == nullptr || is_color_black(wleft, tree)) && (wright == nullptr || is_color_black(wright, tree)))
                    {
                        // Case 2
                        if (w != nullptr)
                        {
                            set_color_red(w, tree);
                        }
                        x       = xp;
                        ix      = ixp;
                        ixp     = get_parent(x);
                        xp      = idx2ptr(dexer, ixp);
                        is_left = xp && (ix == get_left(xp));
                    }
                    else
                    {
                        if (is_left && (wright == nullptr || is_color_black(wright, tree)))
                        {
                            // Case 3a
                            set_color_red(w, tree);
                            if (wleft)
                            {
                                set_color_black(wleft, tree);
                            }
                            helper_rotate_right(root, iroot, w, iw, tree, dexer);
                            iw = get_right(xp);
                            w  = idx2ptr(dexer, iw);
                        }
                        else if (!is_left && (wleft == nullptr || is_color_black(wleft, tree)))
                        {
                            // Case 3b
                            set_color_red(w, tree);
                            if (wright)
                            {
                                set_color_black(wright, tree);
                            }
                            helper_rotate_left(root, iroot, w, iw, tree, dexer);
                            iw = get_left(xp);
                            w  = idx2ptr(dexer, iw);
                        }

                        // Case 4
                        iwleft  = get_left(w);
                        iwright = get_right(w);
                        wleft   = idx2ptr(dexer, iwleft);
                        wright  = idx2ptr(dexer, iwright);

                        set_color(w, tree, get_color(xp, tree));
                        set_color_black(xp, tree);

                        if (is_left && wright != nullptr)
                        {
                            set_color_black(wright, tree);
                            helper_rotate_left(root, iroot, xp, ixp, tree, dexer);
                        }
                        else if (!is_left && wleft != nullptr)
                        {
                            set_color_black(wleft, tree);
                            helper_rotate_right(root, iroot, xp, ixp, tree, dexer);
                        }
                        x  = root;
                        ix = iroot;
                    }
                }

                if (x != nullptr)
                {
                    set_color_black(x, tree);
                }
            }

            bool remove(u32& iroot, tree_t* tree, dexer_t* dexer, u32 inode)
            {
                bool const ret = true;

                ASSERT(tree != nullptr);

                node_t* node = idx2ptr(dexer, inode);
                node_t* root = idx2ptr(dexer, iroot);

                u32     iy;
                node_t* y;
                if (!has_left(node) || !has_right(node))
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
                if (has_left(y))
                {
                    ix = get_left(y);
                    x  = idx2ptr(dexer, ix);
                }
                else
                {
                    ix = get_right(y);
                    x  = idx2ptr(dexer, ix);
                }

                u32 ixp;
                if (x != nullptr)
                {
                    ixp = get_parent(y);
                    set_parent(x, ixp);
                }
                else
                {
                    ixp = get_parent(y);
                }
                node_t* xp = idx2ptr(dexer, ixp);

                s32 is_left = 0;
                if (!has_parent(y))
                {
                    root  = x;
                    iroot = ix;
                    ixp   = NIL;
                    xp    = nullptr;
                }
                else
                {
                    u32     iyp = get_parent(y);
                    node_t* yp  = idx2ptr(dexer, iyp);
                    if (iy == get_left(yp))
                    {
                        set_left(yp, ix);
                        is_left = 1;
                    }
                    else
                    {
                        set_right(yp, ix);
                        is_left = 0;
                    }
                }

                s32 const y_color = get_color(y, tree);

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

            void set_color(node_t* n, tree_t* t, s32 color) { t->m_set_color_f(n, color); }
            s32  get_color(node_t* n, tree_t* t)  { return t->m_get_color_f(n); }
            void set_color_black(node_t* n, tree_t* t) { t->m_set_color_f(n, COLOR_BLACK); }
            void set_color_red(node_t* n, tree_t* t) { t->m_set_color_f(n, COLOR_RED); }
            bool is_color_black(node_t* n, tree_t* t)  { return t->m_get_color_f(n) == COLOR_BLACK; }
            bool is_color_red(node_t* n, tree_t* t)  { return t->m_get_color_f(n) == COLOR_RED; }

            s32 validate(node_t* root, u32 iroot, tree_t* tree, dexer_t* dexer, const char*& result)
            {
                s32 lh, rh;
                if (root == nullptr)
                {
                    return 1;
                }
                else
                {
                    u32     iln = get_left(root);
                    u32     irn = get_right(root);
                    node_t* ln  = idx2ptr(dexer, iln);
                    node_t* rn  = idx2ptr(dexer, irn);

                    // Consecutive red links
                    if (is_color_red(root, tree))
                    {
                        if ((ln != nullptr && is_color_red(ln, tree)) || (rn != nullptr && is_color_red(rn, tree)))
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
                        return is_color_red(root, tree) ? lh : lh + 1;
                    }
                    return 0;
                }
            }

            bool get_min(u32 iroot, tree_t* tree, dexer_t* dexer, u32& found)
            {
                found         = 0xffffffff;
                u32     inode = iroot;
                node_t* pnode = idx2ptr(dexer, iroot);
                while (pnode != nullptr)
                {
                    found = inode;
                    inode = get_left(pnode);
                    pnode = idx2ptr(dexer, inode);
                }
                return found != 0xffffffff;
            }

        } // namespace index_based
    }     // namespace xbst
} // namespace xcore