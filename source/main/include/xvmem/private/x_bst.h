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
        enum ENodeColor { COLOR_BLACK=0, COLOR_RED=1 };
        namespace pointer_based
        {
			struct tree_t;

            struct node_t
            {
                enum EChild { LEFT=0, RIGHT=1 };

                node_t* parent;
                node_t* children[2];
				// color (1 bit) is stored by the user

                void clear() { parent = nullptr; children[0] = nullptr; children[1] = nullptr; }

                node_t* get_parent() { return parent; }
                void set_parent(node_t* p) { parent = p; }

                void set_left(node_t* child) { children[0] = child; }
                void set_right(node_t* child) { children[1] = child; }
                void set_child(s32 c, node_t* child) { children[c] = child; }

                node_t* get_left() { return children[0]; }
                node_t* get_right() { return children[1]; }
                node_t* get_child(s32 c) const { ASSERT(c==LEFT || c==RIGHT); return children[c]; }

                void set_color(tree_t* t, s32 color);
                s32 get_color(tree_t* t) const;
                void set_color_black(tree_t* t);
                void set_color_red(tree_t* t);
                bool is_color_black(tree_t* t) const;
                bool is_color_red(tree_t* t) const;
            };

            // Pointer to a function to compare two nodes, and returns as follows:
            // - (0, +inf] if lhs > rhs
            // - 0 if lhs == rhs
            // - [-inf, 0) if lhs < rhs
            struct tree_t;
            typedef const void* (*get_key_f)(const node_t* lhs);
            typedef s32 (*compare_f)(const void* key, const node_t* node);
            typedef s32 (*get_color_f)(const node_t* lhs);
            typedef void (*set_color_f)(node_t* lhs, s32 color);
            struct tree_t
            {
                void*       m_user;
                get_key_f   m_get_key_f;
                compare_f   m_compare_f;
                get_color_f m_get_color_f;
                set_color_f m_set_color_f;
            };

            // Note: Call this repeatedly until function returns false
            // 'n' will contain the node that is unlinked from the tree.
            bool clear(node_t*& root, node_t*& n);
            bool find(node_t*& root, tree_t* tree, void* data, node_t*& found);
            bool upper(node_t*& root, tree_t* tree, void* data, node_t*& found);
            bool insert(node_t*& root, tree_t* tree, void* data, node_t* node);
            bool remove(node_t*& root, tree_t* tree, void* data, node_t* node);
            s32 validate(node_t*& root, tree_t* tree, const char*& result);
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
