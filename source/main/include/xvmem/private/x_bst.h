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
                bool has_parent() { return parent != nullptr; }
                void set_parent(node_t* p) { parent = p; }

                void set_left(node_t* child) { children[0] = child; }
                void set_right(node_t* child) { children[1] = child; }
                void set_child(s32 c, node_t* child) { children[c] = child; }

                bool has_left() { return children[0] != nullptr; }
                bool has_right() { return children[1] != nullptr; }
                bool has_child(s32 c) const { ASSERT(c==LEFT || c==RIGHT); return children[c] != nullptr; }

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
                compare_f   m_compare_f;
                get_color_f m_get_color_f;
                set_color_f m_set_color_f;
                get_key_f   m_get_key_f;	// Only used by validate()
            };

            // Note: Call this repeatedly until function returns false
            // 'n' will contain the node that is unlinked from the tree.
            bool clear(node_t*& root, tree_t* tree, node_t*& n);
            bool find(node_t*& root, tree_t* tree, void* data, node_t*& found);
            bool upper(node_t*& root, tree_t* tree, void* data, node_t*& found);
            bool insert(node_t*& root, tree_t* tree, void* data, node_t* node);
            bool remove(node_t*& root, tree_t* tree, void* data, node_t* node);
            s32 validate(node_t*& root, tree_t* tree, const char*& result);

			// Traversal
			bool in_order(node_t* root, node_t*& node);
		}

        namespace index_based
        {
			struct tree_t;

            struct node_t
            {
                enum EChild { LEFT=0, RIGHT=1 };

                u32 parent;
                u32 children[2];
				// color (1 bit) is stored by the user

                void clear() { parent = 0; children[0] = 0; children[1] = 0; }

                u32 get_parent() const { return parent; }
				bool has_parent() const { return parent != 0; }
                void set_parent(u32 p) { parent = p; }

                void set_left(u32 child) { children[0] = child; }
                void set_right(u32 child) { children[1] = child; }
                void set_child(s32 c, u32 child) { children[c] = child; }

                bool has_left() { return children[0] != 0; }
                bool has_right() { return children[1] != 0; }
                bool has_child(s32 c) const { ASSERT(c==LEFT || c==RIGHT); return children[c] != 0; }

                u32 get_left() { return children[0]; }
                u32 get_right() { return children[1]; }
                u32 get_child(s32 c) const { ASSERT(c==LEFT || c==RIGHT); return children[c]; }

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
				inline tree_t() : m_user(nullptr), m_dexer(nullptr), m_compare_f(nullptr), m_get_color_f(nullptr), m_set_color_f(nullptr), m_get_key_f(nullptr) { }
                void*       m_user;
				xdexer*     m_dexer;
                compare_f   m_compare_f;
                get_color_f m_get_color_f;
                set_color_f m_set_color_f;
                get_key_f   m_get_key_f;	// Only used by validate()

				node_t*	    idx2ptr(u32);
				u32  	    ptr2idx(node_t*);
            };

            // Note: Call this repeatedly until function returns false
            // 'n' will contain the node that is unlinked from the tree.
            bool clear(u32& root, tree_t* tree, u32& n);
            bool find(u32& root, tree_t* tree, void* data, u32& found);
            bool upper(u32& root, tree_t* tree, void* data, u32& found);
            bool insert(u32& root, tree_t* tree, void* data, u32 node);
            bool remove(u32& root, tree_t* tree, void* data, u32 node);
            s32 validate(node_t* root, u32 iroot, tree_t* tree, const char*& result);

			// Traversal
			bool in_order(u32 root, u32& node, tree_t* tree);
        }
    } // namespace xbst
} // namespace xcore

#endif // _X_BST_H_
