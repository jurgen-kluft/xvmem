#ifndef _X_XVMEM_DOUBLY_LINKED_LIST_H_
#define _X_XVMEM_DOUBLY_LINKED_LIST_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    struct llindex_t
    {
        const u16 NIL = 0xFFFF;

        inline llindex_t() : m_index(NIL) {}
        inline llindex_t(u16 i) : m_index(i) {}
        inline bool is_nil() const { return m_index == NIL; }
        inline void reset() { m_index = NIL; }
        inline void operator = (const llindex_t& i) { m_index = i.m_index; }
        inline bool operator == (const llindex_t& i) const { return m_index == i.m_index; }
        inline u16 get() const { return m_index; }

        u16 m_index;
    };

    struct llnode_t
    {
        inline void link(llindex_t p, llindex_t n)
        {
            m_prev = p;
            m_next = n;
        }
        inline void unlink()
        {
            m_prev.reset();
            m_next.reset();;
        }
        inline bool is_linked() const { return !m_prev.is_nil() && !m_next.is_nil(); }
        llindex_t m_prev, m_next;
    };

    struct llhead_t : public llindex_t
    {
        inline llhead_t() : llindex_t(NIL) {}

        void    insert(llnode_t* list, llindex_t item);      // Inserts 'item' at the head
        void    insert_tail(llnode_t* list, llindex_t item); // Inserts 'item' at the tail end
        llnode_t* remove_item(llnode_t* list, llindex_t item);
        llnode_t* remove_head(llnode_t* list);
        llnode_t* remove_tail(llnode_t* list);
        llindex_t remove_headi(llnode_t* list);
        llindex_t remove_taili(llnode_t* list);

        inline void operator = (u16 i) { m_index = i; }
        inline void operator = (const llindex_t& index) { m_index = index.m_index; }
        inline void operator = (const llhead_t& head) { m_index = head.m_index; }

        static llnode_t* idx2node(llnode_t* list, llindex_t i)
        {
            if (i.is_nil())
                return nullptr;
            return &list[i.get()];
        }

        static llindex_t node2idx(llnode_t* list, llnode_t* node)
        {
            if (node == nullptr)
                return llindex_t();
            llindex_t const index = (u16)(((u64)node - (u64)&list[0]) / sizeof(llnode_t));
            return index;
        }

    };

    struct llist_t
    {
        inline llist_t() : m_size(0), m_size_max(0) {}
        inline llist_t(u16 size, u16 size_max) : m_size(size), m_size_max(size_max) {}

        inline u32  size() const { return m_size; }
        inline bool is_empty() const { return m_size == 0; }
        inline bool is_full() const { return m_size == m_size_max; }

        void initialize(llnode_t* list, u16 size, u16 max_size);
        inline void reset()
        {
            m_size = 0;
            m_head.reset();
        }

        void    insert(llnode_t* list, llindex_t item);      // Inserts 'item' at the head
        void    insert_tail(llnode_t* list, llindex_t item); // Inserts 'item' at the tail end
        llnode_t* remove_item(llnode_t* list, llindex_t item);
        llnode_t* remove_head(llnode_t* list);
        llnode_t* remove_tail(llnode_t* list);
        llindex_t remove_headi(llnode_t* list);
        llindex_t remove_taili(llnode_t* list);

        llnode_t* idx2node(llnode_t* list, llindex_t i) const
        {
            ASSERT(i.get() < m_size_max);
            return m_head.idx2node(list, i);
        }

        llindex_t node2idx(llnode_t* list, llnode_t* node) const
        {
            llindex_t i = m_head.node2idx(list, node);
            ASSERT(i.get() < m_size_max);
        }

        u16    m_size;
        u16    m_size_max;
        llhead_t m_head;
    };

} // namespace xcore

#endif // _X_XVMEM_DOUBLY_LINKED_LIST_H_