#ifndef _X_XVMEM_DOUBLY_LINKED_LIST_H_
#define _X_XVMEM_DOUBLY_LINKED_LIST_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    struct xalist_t
    {
        typedef u16      index;
        typedef u16      head;
        static const u16 NIL;

        xalist_t()
            : m_size(0)
            , m_size_max(0)
            , m_head(NIL)
        {
        }
        xalist_t(u16 size, u16 size_max)
            : m_size(size)
            , m_size_max(size_max)
            , m_head(NIL)
        {
        }

        struct node_t
        {
            inline void link(u16 p, u16 n)
            {
                m_prev = p;
                m_next = n;
            }
            inline void unlink()
            {
                m_prev = NIL;
                m_next = NIL;
            }
            inline bool is_linked() const { return m_prev != NIL && m_next != NIL; }
            u16         m_prev, m_next;
        };

        void initialize(node_t* list, u16 size, u16 max_size);

        inline u32  size() const { return m_size; }
        inline bool is_empty() const { return m_size == 0; }
        inline bool is_full() const { return m_size == m_size_max; }
        inline void reset()
        {
            m_size = 0;
            m_head = NIL;
        }
        void    insert(node_t* list, u16 item);      // Inserts 'item' at the head
        void    insert_tail(node_t* list, u16 item); // Inserts 'item' at the tail end
        node_t* remove_item(node_t* list, u16 item);
        node_t* remove_head(node_t* list);
        node_t* remove_tail(node_t* list);
        u16     remove_headi(node_t* list);
        u16     remove_taili(node_t* list);

        node_t* idx2node(node_t* list, u16 i) const
        {
            ASSERT(i < m_size_max);
            if (i == xalist_t::NIL)
                return nullptr;
            return &list[i];
        }

        u16 node2idx(node_t* list, node_t* node) const
        {
            if (node == nullptr)
                return xalist_t::NIL;
            u16 const index = (u16)(((u64)node - (u64)&list[0]) / sizeof(xalist_t::node_t));
            ASSERT(index < m_size_max);
            return index;
        }

        u16 m_size;
        u16 m_size_max;
        u16 m_head;
    };

} // namespace xcore

#endif // _X_XVMEM_DOUBLY_LINKED_LIST_H_