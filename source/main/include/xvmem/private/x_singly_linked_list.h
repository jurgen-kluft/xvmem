#ifndef _X_XVMEM_SINGLY_LINKED_LIST_H_
#define _X_XVMEM_SINGLY_LINKED_LIST_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xbase/x_debug.h"

namespace xcore
{
    typedef u16      lindex_t;

    struct lnode_t
    {
        static const u16 NIL = 0xFFFF;
        lindex_t         m_next;
    };

    struct lhead_t
    {
        lindex_t m_index;
        inline lhead_t()
            : m_index(0xFFFF)
        {
        }
        void     initialize(u32 const sizeof_node, lnode_t* list, u16 start, u16 size);
        void     reset() { m_index = 0xFFFF; }
        bool     is_nil() const { return m_index == 0xFFFF; }
        void     insert(u32 const sizeof_node, lnode_t* list, lindex_t item); // Inserts 'item' at the head
        lnode_t* remove(u32 const sizeof_node, lnode_t* list);
        lindex_t remove_i(u32 const sizeof_node, lnode_t* list);

        inline void operator=(u16 i) { m_index = i; }
        inline void operator=(const lindex_t& index) { m_index = index; }
        inline void operator=(const lhead_t& head) { m_index = head.m_index; }

        static lnode_t* idx2node(u32 const sizeof_node, lnode_t* list, lindex_t i)
        {
            if (i == 0xFFFF)
                return nullptr;
            return (lnode_t*)((uptr)list + ((uptr)sizeof_node * i));
        }

        static lindex_t node2idx(u32 const sizeof_node, lnode_t* list, lnode_t* node)
        {
            if (node == nullptr)
                return lindex_t();
            lindex_t const index = (u16)(((uptr)node - (uptr)list) / sizeof_node);
            return index;
        }
    };

    struct list_t
    {
        inline list_t()
            : m_size(0)
            , m_size_max(0)
        {
        }
        inline list_t(u16 size, u16 size_max)
            : m_size(size)
            , m_size_max(size_max)
        {
        }

        inline u32  size() const { return m_size; }
        inline bool is_empty() const { return m_size == 0; }
        inline bool is_full() const { return m_size == m_size_max; }

        void        initialize(u32 const sizeof_node, lnode_t* list, u16 start, u16 size, u16 max_size);
        inline void reset()
        {
            m_size = 0;
            m_head.reset();
        }

        void     insert(u32 const sizeof_node, lnode_t* list, lindex_t item); // Inserts 'item' at the head
        lnode_t* remove(u32 const sizeof_node, lnode_t* list);
        lindex_t remove_i(u32 const sizeof_node, lnode_t* list);

        lnode_t* idx2node(u32 const sizeof_node, lnode_t* list, lindex_t i) const
        {
            ASSERT(i < m_size_max);
            return m_head.idx2node(sizeof_node, list, i);
        }

        lindex_t node2idx(u32 const sizeof_node, lnode_t* list, lnode_t* node) const
        {
            lindex_t i = m_head.node2idx(sizeof_node, list, node);
            ASSERT(i < m_size_max);
            return i;
        }

        u16     m_size;
        u16     m_size_max;
        lhead_t m_head;
    };

} // namespace xcore

#endif // _X_XVMEM_DOUBLY_LINKED_LIST_H_