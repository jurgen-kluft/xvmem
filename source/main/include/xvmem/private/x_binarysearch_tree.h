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
        enum ENodeColor
        {
            COLOR_BLACK = 0,
            COLOR_RED   = 1
        };
        enum ENodeChild
        {
            LEFT  = 0,
            RIGHT = 1
        };

        namespace pointer_based
        {
            struct node_t
            {
                node_t* parent;
                node_t* children[2];

                void clear()
                {
                    parent      = nullptr;
                    children[0] = nullptr;
                    children[1] = nullptr;
                }
            };

            // Pointer to a function to compare two nodes, and returns as follows:
            // - (0, +inf] if lhs > rhs
            // - 0 if lhs == rhs
            // - [-inf, 0) if lhs < rhs
            struct tree_t;
            typedef u64 (*get_key_f)(const node_t* lhs);
            typedef s32 (*compare_f)(const u64 key, const node_t* node);
            typedef s32 (*get_color_f)(const node_t* lhs);
            typedef void (*set_color_f)(node_t* lhs, s32 color);
            struct tree_t
            {
                compare_f   m_compare_f;
                get_color_f m_get_color_f;
                set_color_f m_set_color_f;
                get_key_f   m_get_key_f; // Only used by validate()
            };

            // Note: Call this repeatedly until function returns false
            // 'n' will contain the node that is unlinked from the tree.
            bool clear(node_t*& root, tree_t* tree, node_t*& n);
            bool find(node_t*& root, tree_t* tree, u64 data, node_t*& found);
            bool upper(node_t*& root, tree_t* tree, u64 data, node_t*& found);
            bool insert(node_t*& root, tree_t* tree, u64 data, node_t* node);
            bool remove(node_t*& root, tree_t* tree, node_t* node);
            s32  validate(node_t* root, tree_t* tree, const char*& result);

			bool get_min(node_t* root, tree_t* tree, node_t*& found);
        } // namespace pointer_based

        namespace index_based
        {
            struct tree_t;
            enum { NIL = 0xffffffff };

            struct node_t
            {
                u32 parent;
                u32 children[2];
                // color (1 bit) is stored by the user

                void clear()
                {
                    parent      = NIL;
                    children[0] = NIL;
                    children[1] = NIL;
                }
            };

            // Pointer to a function to compare two nodes, and returns as follows:
            // - (0, +inf] if lhs > rhs
            // - 0 if lhs == rhs
            // - [-inf, 0) if lhs < rhs
            class xtree_t
            {
            public:
                virtual u64 get_key(const node_t* lhs) const = 0;
                virtual s32 compare(const u64 key, const node_t* node) = 0;
                virtual s32 get_color(const node_t* lhs)const = 0;
                virtual void set_color(node_t* lhs, s32 color) = 0;
            };

            struct tree_t;
            typedef u64 (*get_key_f)(const node_t* lhs);
            typedef s32 (*compare_f)(const u64 key, const node_t* node);
            typedef s32 (*get_color_f)(const node_t* lhs);
            typedef void (*set_color_f)(node_t* lhs, s32 color);
            struct tree_t
            {
                inline tree_t()
                    : m_compare_f(nullptr)
                    , m_get_color_f(nullptr)
                    , m_set_color_f(nullptr)
                    , m_get_key_f(nullptr)
                {
                }
                compare_f   m_compare_f;
                get_color_f m_get_color_f;
                set_color_f m_set_color_f;
                get_key_f   m_get_key_f; // Only used by validate()
            };

            // Note: Call this repeatedly until function returns false
            // 'n' will contain the node that is unlinked from the tree.
            bool clear(u32& root, tree_t* tree, dexer_t* dexer, u32& n);
			bool find_specific(u32& root, tree_t* tree, dexer_t* dexer, u64 data, u32& found, compare_f comparer);
            bool find(u32& root, tree_t* tree, dexer_t* dexer, u64 data, u32& found);
            bool upper(u32& root, tree_t* tree, dexer_t* dexer, u64 data, u32& found);
            bool insert(u32& root, tree_t* tree, dexer_t* dexer, u64 data, u32 node);
            bool remove(u32& root, tree_t* tree, dexer_t* dexer, u32 node);
            s32  validate(node_t* root, u32 iroot, tree_t* tree, dexer_t* dexer, const char*& result);

            bool get_min(u32 root, tree_t* tree, dexer_t* dexer, u32& found);
        } // namespace index_based
    }     // namespace xbst
} // namespace xcore

#endif // _X_BST_H_
