#ifndef _X_BST_H_
#define _X_BST_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xbase/x_allocator.h"

namespace xcore
{
    namespace xbst
    {
        namespace pointer_based
        {
            struct node_t
            {
                enum EChild { LEFT=0, RIGHT=1 };

                node_t* parent;
                node_t* children[2];

                void clear() { parent = nullptr; children[0] = nullptr; children[1] = nullptr; }

                node_t* get_parent() { return parent; }
                void set_parent(node_t* p) { parent = p; }

                void set_left(node_t* child) { children[0] = child; }
                void set_right(node_t* child) { children[1] = child; }
                void set_child(s32 c, node_t* child) { children[c] = child; }

                node_t* get_left() { return children[0]; }
                node_t* get_right() { return children[1]; }
                node_t* get_child(s32 c) const { ASSERT(c==LEFT || c==RIGHT); return children[c]; }

                void set_color_black(tree_t* t);
                void set_color_red(tree_t* t);

                bool is_color_black(tree_t* t) const;
                bool is_color_red(tree_t* t) const;
            };
            typedef node_t*   pnode;

            // Pointer to a function to compare two nodes, and returns as follows:
            // - (0, +inf] if lhs > rhs
            // - 0 if lhs == rhs
            // - [-inf, 0) if lhs < rhs
            struct tree_t;
            typedef s32 (*compare_f)(tree_t* tree, const pnode lhs, const void* rhs);
            typedef s32 (*get_color_f)(tree_t* tree, const pnode lhs);
            typedef void (*set_color_f)(tree_t* tree, pnode lhs, s32 color);
            struct tree_t
            {
                void*       m_user;
                compare_f   m_compare_f;
                get_color_f m_get_color_f;
                set_color_f m_set_color_f;
            };

            // Note: Call this repeatedly until function returns false
            // 'n' will contain the node that is unlinked from the tree.
            bool clear(pnode& root, pnode& n);
            bool find(pnode& root, tree_t* tree, void* data, pnode& found);
            bool upper(pnode& root, tree_t* tree, void* data, pnode& found);
            bool insert(pnode& root, tree_t* tree, void* data, pnode node);
            bool remove(pnode& root, tree_t* tree, void* data, pnode node);
        }

        namespace index_based
        {
            typedef u32     inode;
            struct node_t
            {
                inode left;
                inode right;
                inode parent;
            };
            typedef node_t*   pnode;

            // Pointer to a function to compare two nodes, and returns as follows:
            // - (0, +inf] if lhs > rhs
            // - 0 if lhs == rhs
            // - [-inf, 0) if lhs < rhs
            struct tree_t;
            typedef s32 (*compare_f)(tree_t* tree, const pnode lhs, const void* rhs);
            typedef s32 (*get_color_f)(tree_t* tree, const pnode lhs);
            typedef void (*set_color_f)(tree_t* tree, pnode lhs, s32 color);
            typedef pnode (*idx2ptr_f)(tree_t* tree, inode idx);
            struct tree_t
            {
                void*       m_user;
                compare_f   m_compare_f;
                get_color_f m_get_color_f;
                set_color_f m_set_color_f;
                idx2ptr_f   m_idx2ptr_f;
            };

            // Note: Call this repeatedly until function returns false
            // 'n' will contain the node that is unlinked from the tree.
            bool clear(inode& root, inode& node);
            bool find(inode& root, tree_t* tree, void* data, inode& found);
            bool upper(inode& root, tree_t* tree, void* data, inode& found);
            bool insert(inode& root, tree_t* tree, void* data, inode node);
            bool remove(inode& root, tree_t* tree, void* data, inode node);
        }
    } // namespace xbst
} // namespace xcore

#endif // _X_BST_H_
