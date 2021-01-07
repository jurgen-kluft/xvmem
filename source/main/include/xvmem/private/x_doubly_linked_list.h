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

    struct llnode_t
    {
        static const u16 NIL = 0xFFFF;
        inline bool is_linked() const { return m_prev != NIL && m_next != NIL; }
        llindex_t m_prev, m_next;
    };

    struct llhead_t
    {
        llindex_t m_index;
        inline llhead_t()
            : m_index(0xFFFF)
        {
        }

        void      reset() { m_index = 0xFFFF; }
        bool      is_nil() const { return m_index == 0xFFFF; }
        void      insert(u32 const sizeof_node, llnode_t* list, llindex_t item);      // Inserts 'item' at the head
        void      insert_tail(u32 const sizeof_node, llnode_t* list, llindex_t item); // Inserts 'item' at the tail end
        llnode_t* remove_item(u32 const sizeof_node, llnode_t* list, llindex_t item);
        llnode_t* remove_head(u32 const sizeof_node, llnode_t* list);
        llnode_t* remove_tail(u32 const sizeof_node, llnode_t* list);
        llindex_t remove_headi(u32 const sizeof_node, llnode_t* list);
        llindex_t remove_taili(u32 const sizeof_node, llnode_t* list);

        inline void operator=(u16 i) { m_index = i; }
        inline void operator=(const llindex_t& index) { m_index = index; }
        inline void operator=(const llhead_t& head) { m_index = head.m_index; }

        static llnode_t* idx2node(u32 const sizeof_node, llnode_t* list, llindex_t i)
        {
            if (i == 0xFFFF)
                return nullptr;
            return (llnode_t*)((uptr)list + ((uptr)sizeof_node * i));
        }

        static llindex_t node2idx(u32 const sizeof_node, llnode_t* list, llnode_t* node)
        {
            if (node == nullptr)
                return llindex_t();
            llindex_t const index = (u16)(((uptr)node - (uptr)list) / sizeof_node);
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

        void        initialize(u32 const sizeof_node, llnode_t* list, u16 start, u16 size, u16 max_size);
        inline void reset()
        {
            m_size = 0;
            m_head.reset();
        }

        void      insert(u32 const sizeof_node, llnode_t* list, llindex_t item);      // Inserts 'item' at the head
        void      insert_tail(u32 const sizeof_node, llnode_t* list, llindex_t item); // Inserts 'item' at the tail end
        llnode_t* remove_item(u32 const sizeof_node, llnode_t* list, llindex_t item);
        llnode_t* remove_head(u32 const sizeof_node, llnode_t* list);
        llnode_t* remove_tail(u32 const sizeof_node, llnode_t* list);
        llindex_t remove_headi(u32 const sizeof_node, llnode_t* list);
        llindex_t remove_taili(u32 const sizeof_node, llnode_t* list);

        llnode_t* idx2node(u32 const sizeof_node, llnode_t* list, llindex_t i) const
        {
            ASSERT(i < m_size_max);
            return m_head.idx2node(sizeof_node, list, i);
        }

        llindex_t node2idx(u32 const sizeof_node, llnode_t* list, llnode_t* node) const
        {
            llindex_t i = m_head.node2idx(sizeof_node, list, node);
            ASSERT(i < m_size_max);
            return i;
        }

        u16      m_size;
        u16      m_size_max;
        llhead_t m_head;
    };

} // namespace xcore

#endif // _X_XVMEM_DOUBLY_LINKED_LIST_H_