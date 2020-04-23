#ifndef _X_XVMEM_DOUBLY_LINKED_LIST_H_
#define _X_XVMEM_DOUBLY_LINKED_LIST_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    struct xarray_list_t
    {
        static const u16 NIL = 0xffff;

        xarray_list_t()
            : m_count(0)
            , m_head(NIL)
        {
        }

        struct node_t
        {
            void link(u16 p, u16 n)
            {
                m_prev = p;
                m_next = n;
            }
            bool is_linked(u16 self) const { return m_prev == self && m_next == self; }
            u16  m_prev, m_next;
        };

		bool    is_empty() const { return m_count == 0; }
        void    insert(node_t* list, u16 item); // Inserts 'item' at the head
		void    insert_tail(node_t* list, u16 item); // Inserts 'item' at the tail end
        void    remove_item(node_t* list, u16 item);
        node_t* remove_head(node_t* list);
		node_t* remove_tail(node_t* list);
        node_t* idx2node(node_t* list, u16 i);
        u16     node2idx(node_t* list, node_t* n);
        u16     m_count;
        u16     m_head;
    };

} // namespace xcore

#endif // _X_XVMEM_DOUBLY_LINKED_LIST_H_