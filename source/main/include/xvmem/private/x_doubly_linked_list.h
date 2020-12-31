#ifndef _X_XVMEM_DOUBLY_LINKED_LIST_H_
#define _X_XVMEM_DOUBLY_LINKED_LIST_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xbase/x_debug.h"

namespace xcore
{
    typedef u16      llindex_t;
    static const u16 NIL = 0xFFFF;
    inline void      reset(llindex_t& i) { i = NIL; }

    struct llnode_t
    {
        inline void link(llindex_t p, llindex_t n)
        {
            m_prev = p;
            m_next = n;
        }
        inline void unlink()
        {
            reset(m_prev);
            reset(m_next);
        }
        inline bool is_linked() const { return m_prev != NIL && m_next != NIL; }
        inline bool is_last() const { return m_prev != NIL && m_prev == m_next; }
        llindex_t   m_prev, m_next;
    };

    struct llhead_t
    {
        llindex_t m_index;
        inline llhead_t()
            : m_index(NIL)
        {
        }

        void      reset() { m_index = NIL; }
        bool      is_nil() const { return m_index == NIL; }
        void      insert(llnode_t* list, u32 const sizeof_node, llindex_t item);      // Inserts 'item' at the head
        void      insert_tail(llnode_t* list, u32 const sizeof_node, llindex_t item); // Inserts 'item' at the tail end
        llnode_t* remove_item(llnode_t* list, u32 const sizeof_node, llindex_t item);
        llnode_t* remove_head(llnode_t* list, u32 const sizeof_node);
        llnode_t* remove_tail(llnode_t* list, u32 const sizeof_node);
        llindex_t remove_headi(llnode_t* list, u32 const sizeof_node);
        llindex_t remove_taili(llnode_t* list, u32 const sizeof_node);

        inline void operator=(u16 i) { m_index = i; }
        inline void operator=(const llindex_t& index) { m_index = index; }
        inline void operator=(const llhead_t& head) { m_index = head.m_index; }

        static llnode_t* idx2node(llnode_t* list, u32 const sizeof_node, llindex_t i)
        {
            if (i == NIL)
                return nullptr;
            return &list[i];
        }

        static llindex_t node2idx(llnode_t* list, llnode_t* node, u32 const sizeof_node)
        {
            if (node == nullptr)
                return llindex_t();
            llindex_t const index = (u16)(((u64)node - (u64)&list[0]) / sizeof(llnode_t));
            return index;
        }
    };

    struct llist_t
    {
        inline llist_t()
            : m_size(0)
            , m_size_max(0)
        {
        }
        inline llist_t(u16 size, u16 size_max)
            : m_size(size)
            , m_size_max(size_max)
        {
        }

        inline u32  size() const { return m_size; }
        inline bool is_empty() const { return m_size == 0; }
        inline bool is_full() const { return m_size == m_size_max; }

        void        initialize(llnode_t* list, u16 start, u16 size, u16 max_size);
        inline void reset()
        {
            m_size = 0;
            m_head.reset();
        }

        void      insert(llnode_t* list, u32 const sizeof_node, llindex_t item);      // Inserts 'item' at the head
        void      insert_tail(llnode_t* list, u32 const sizeof_node, llindex_t item); // Inserts 'item' at the tail end
        llnode_t* remove_item(llnode_t* list, u32 const sizeof_node, llindex_t item);
        llnode_t* remove_head(llnode_t* list, u32 const sizeof_node);
        llnode_t* remove_tail(llnode_t* list, u32 const sizeof_node);
        llindex_t remove_headi(llnode_t* list, u32 const sizeof_node);
        llindex_t remove_taili(llnode_t* list, u32 const sizeof_node);

        llnode_t* idx2node(llnode_t* list, u32 const sizeof_node, llindex_t i) const
        {
            ASSERT(i < m_size_max);
            return m_head.idx2node(list, sizeof_node, i);
        }

        llindex_t node2idx(llnode_t* list, llnode_t* node, u32 const sizeof_node) const
        {
            llindex_t i = m_head.node2idx(list, node, sizeof_node);
            ASSERT(i < m_size_max);
            return i;
        }

        u16      m_size;
        u16      m_size_max;
        llhead_t m_head;
    };

} // namespace xcore

#endif // _X_XVMEM_DOUBLY_LINKED_LIST_H_