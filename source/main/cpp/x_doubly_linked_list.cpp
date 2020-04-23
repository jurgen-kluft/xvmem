#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_doubly_linked_list.h"

namespace xcore
{
    void xarray_list_t::insert(node_t* list, u16& head, u16 item)
    {
        node_t* const phead = idx2node(list, head);
        node_t* const pitem = idx2node(list, item);
        if (phead == nullptr)
        {
            pitem->link(item, item);
        }
        else
        {
            pitem->link(phead->m_prev, head);
            phead->m_prev = item;
        }
        head = item;
    }

    void xarray_list_t::remove(node_t* list, u16& head, u16 item)
    {
        node_t* const phead = idx2node(list, head);
        node_t* const pitem = idx2node(list, item);
        node_t* const pprev = idx2node(list, pitem->m_prev);
        node_t* const pnext = idx2node(list, pitem->m_next);
        pprev->m_next                   = pitem->m_next;
        pnext->m_prev                   = pitem->m_prev;
        pitem->link(item, item);

        if (head == item)
        {
            head = node2idx(list, pnext);
            if (head == item)
            {
                head = NIL;
            }
        }
    }

	xarray_list_t::node_t* xarray_list_t::idx2node(node_t* list, u16 i)
	{
        if (i == NIL)
            return nullptr;
        return &list[i];
	}
		
	u16     xarray_list_t::node2idx(node_t* list, node_t* node)
	{
        if (node == nullptr)
            return NIL;
        u16 const index = (u16)(((u64)node - (u64)&list[0]) / sizeof(node_t));
        return index;
	}

} // namespace xcore