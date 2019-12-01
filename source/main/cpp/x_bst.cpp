#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/private/x_bst.h"

namespace xcore
{
    namespace pointer_based
    {
        bool find(pnode& root, tree_t* tree, void* data, pnode& found)
        {
            bool ret;

            ASSERT(tree != nullptr);
            ASSERT(value != nullptr);

            pnode = nullptr;
            if (root == nullptr)
            {
                ret = false;
                goto done;
            }

            pnode node = root;
            while (node != nullptr)
            {
                s32 const c = tree->m_compare_f(tree, node, data);
                if (c < 0) {
                    node = node->right;
                } else if (c == 0) {
                    break; // We found our node
                } else {
                    // Otherwise, we want the right node, and continue iteration
                    node = node->left;
                }
            }

            if (node == nullptr)
            {
                ret = false;
                goto done;
            }

            // Return the node we found
            ret = true;
            *found = node;

        done:
            return ret;
        }

        static inline pnode helper_get_sibling(pnode node)
        {
            pnode parent = node->get_parent();
            if (parent == nullptr)
                return nullptr;

            if (node == parent->get_left())
                return parent->get_right();

            return parent->get_left();
        }

        static inline pnode helper_get_grandparent(pnode node)
        {
            pnode parent_node = node->get_parent();
            if (parent_node == nullptr)
                return nullptr;
            return parent_node->get_parent();
        }

        static inline pnode helper_get_uncle(pnode node)
        {
            pnode grandparent = helper_get_grandparent(node);
            if (grandparent == nullptr)
                return nullptr;

            if (node->get_parent() == grandparent->get_left())
                return grandparent->get_right();
            return grandparent->get_left();
        }

        static inline void helper_rotate_left(pnode& root, pnode node)
        {
            pnode x = node;
            pnode y = x->get_right();
            x->set_right(y->get_left());
            if (y->get_left() != nullptr)
            {
                pnode yleft = y->get_left();
                RB_TREE_NODE_SET_PARENT(yleft, x);
            }

            y->set_parent(x->get_parent());
            if (x->get_parent() == nullptr)
            {
                root = y;
            }
            else
            {
                pnode xp = x->get_parent();
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

        static inline void helper_rotate_right(pnode& root, pnode node)
        {
            pnode x = node;
            pnode y = x->get_left();
            x->set_left(y->get_right());

            if (y->get_right() != nullptr)
            {
                pnode yright = y->get_right();
                yright->set_parent(x);
            }

            y->set_parent(x->get_parent()));
            if (x->get_parent() == nullptr)
            {
                root = y;
            }
            else
            {
                pnode xp = x->get_parent();
                if (x == xp->get_left())
                {
                    xp->set_left(y);
                }
                else
                {
                    xp->set_right(y);
                }
            }

            y->right = x;
            x->set_parent(y);
        }

        static void helper_insert_rebalance(tree_t* tree, pnode& root, pnode node)
        {
            pnode new_node_parent = node->get_parent();

            if (new_node_parent != nullptr && new_node_parent->is_color_black(tree)==false)
            {
                pnode iter = node;

                // Iterate until we're at the root (which we just color black) or
                // until we the parent node is no longer red.
                while ((root != iter) && (iter->get_parent() != nullptr) && (iter->get_parent()->is_color_red(tree)))
                {
                    node * parent = iter->get_parent();
                    node * grandparent = helper_get_grandparent(iter);
                    node * uncle = nullptr;

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
                            helper_rotate_left(tree, iter);
                        }
                        else if (uncle_is_left && parent->get_left() == iter)
                        {
                            iter = iter->get_parent();
                            helper_rotate_right(tree, iter);
                        }

                        // Case 3 - Recolor and rotate
                        parent = iter=>get_parent();
                        parent->set_color_black(tree);

                        grandparent = helper_get_grandparent(iter);
                        grandparent->set_color_red(tree);
                        if (!uncle_is_left)
                        {
                            helper_rotate_right(tree, grandparent);
                        }
                        else
                        {
                            helper_rotate_left(tree, grandparent);
                        }
                    }
                }

                // Make sure the tree root is black (Case 1: Continued)
                root->set_color_black(tree);
            }

            bool insert(pnode& root, tree_t* tree, void* data, pnode node);
            {
                bool ret;

                node* nd = NULL;

                ASSERT(tree != NULL);
                ASSERT(node != NULL);

                node->clear();

                // Case 1: Simplest case -- tree is empty
                if (root == NULL)
                {
                    root = node;
                    node->set_color_black(tree);
                    goto done;
                }

                // Otherwise, insert the node as you would typically in a BST
                nd = root;
                node->set_color_red(tree);

                // Insert a node into the tree as you normally would
                while (nd != NULL)
                {
                    s32 const c = tree->m_compare_f(tree->m_user, nd, data);
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
                helper_insert_rebalance(tree, node);

            done:
                return ret;
            }            
        }
    }    
}