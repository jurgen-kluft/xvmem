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
    static const u16 NIL = 0xFFFF;
    inline void      reset(lindex_t& i) { i = NIL; }

    struct lnode_t
    {
        inline void link(lindex_t n) { m_next = n; }
        inline void unlink() { reset(m_next); }
        inline bool is_linked() const { return m_next != NIL; }
        lindex_t    m_next;
    };

    struct lhead_t
    {
        lindex_t m_index;
        inline lhead_t()
            : m_index(NIL)
        {
        }
        void     reset() { m_index = NIL; }
        bool     is_nil() { return m_index == NIL; }
        void     insert(lnode_t* list, lindex_t item); // Inserts 'item' at the head
        lnode_t* remove(lnode_t* list);
        lindex_t remove_i(lnode_t* list);

        inline void operator=(u16 i) { m_index = i; }
        inline void operator=(const lindex_t& index) { m_index = index; }
        inline void operator=(const lhead_t& head) { m_index = head.m_index; }

        static lnode_t* idx2node(lnode_t* list, lindex_t i)
        {
            if (i == NIL)
                return nullptr;
            return &list[i];
        }

        static lindex_t node2idx(lnode_t* list, lnode_t* node)
        {
            if (node == nullptr)
                return lindex_t();
            lindex_t const index = (u16)(((u64)node - (u64)&list[0]) / sizeof(lnode_t));
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

        void        initialize(lnode_t* list, u16 start, u16 size, u16 max_size);
        inline void reset()
        {
            m_size = 0;
            m_head.reset();
        }

        void     insert(lnode_t* list, lindex_t item); // Inserts 'item' at the head
        lnode_t* remove(lnode_t* list);
        lindex_t remove_i(lnode_t* list);

        lnode_t* idx2node(lnode_t* list, lindex_t i) const
        {
            ASSERT(i < m_size_max);
            return m_head.idx2node(list, i);
        }

        lindex_t node2idx(lnode_t* list, lnode_t* node) const
        {
            lindex_t i = m_head.node2idx(list, node);
            ASSERT(i < m_size_max);
            return i;
        }

        u16     m_size;
        u16     m_size_max;
        lhead_t m_head;
    };

} // namespace xcore

#endif // _X_XVMEM_DOUBLY_LINKED_LIST_H_