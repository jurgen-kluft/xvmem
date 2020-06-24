#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_doubly_linked_list.h"

namespace xcore
{
    const u16 xalist_t::NIL = 0xffff;

    void xalist_t::initialize(node_t* list, u16 max_count)
    {
        for (u32 i = 0; i < max_count; ++i)
        {
            list[i].link(i - 1, i + 1);
        }
        list[0].link(max_count - 1, 1);
        list[max_count - 1].link(max_count - 2, 0);

        m_count = max_count;
        m_head  = 0;
    }

    void xalist_t::insert(node_t* list, u16 item)
    {
        node_t* const pitem = idx2node(list, item);
        if (m_head == NIL)
        {
            pitem->link(item, item);
        }
        else
        {
            u16 const     inext = m_head;
            node_t* const pnext = idx2node(list, inext);
            u16 const     iprev = pnext->m_prev;
            node_t* const pprev = idx2node(list, iprev);
            pitem->link(iprev, inext);
            pnext->m_prev = item;
            pprev->m_next = item;
        }
        m_head = item;
        m_count += 1;
    }

    void xalist_t::insert_tail(node_t* list, u16 item)
    {
        node_t* const pitem = idx2node(list, item);
        if (m_head == NIL)
        {
            pitem->link(item, item);
            m_head = item;
        }
        else
        {
            u16 const     inext = m_head;
            node_t* const pnext = idx2node(list, inext);
            u16 const     iprev = pnext->m_prev;
            node_t* const pprev = idx2node(list, iprev);
            pitem->link(iprev, inext);
            pnext->m_prev = item;
            pprev->m_next = item;
        }
        m_count += 1;
    }

    xalist_t::node_t* xalist_t::remove_item(node_t* list, u16 item)
    {
        node_t* const pitem = idx2node(list, item);
        node_t* const pprev = idx2node(list, pitem->m_prev);
        node_t* const pnext = idx2node(list, pitem->m_next);
        pprev->m_next       = pitem->m_next;
        pnext->m_prev       = pitem->m_prev;
        pitem->unlink();
        if (m_count == 1)
        {
            m_head  = NIL;
            m_count = 0;
        }
        else
        {
            if (m_head == item)
            {
                m_head = node2idx(list, pnext);
            }
            m_count--;
        }
        return pitem;
    }

    xalist_t::node_t* xalist_t::remove_head(node_t* list)
    {
        if (m_head == NIL)
        {
            return nullptr;
        }
        u16 const     iitem = m_head;
        node_t* const pitem = idx2node(list, iitem);
        u16 const     inext = pitem->m_next;
        node_t* const pnext = idx2node(list, inext);
        u16 const     iprev = pitem->m_prev;
        node_t* const pprev = idx2node(list, iprev);
        pprev->m_next       = inext;
        pnext->m_prev       = iprev;
        pitem->link(NIL, NIL);
        if (m_count == 1)
        {
            m_head = NIL;
        }
        else
        {
            m_head = inext;
        }
        m_count -= 1;
        return pitem;
    }

    xalist_t::node_t* xalist_t::remove_tail(node_t* list)
    {
        if (m_head == NIL)
        {
            return nullptr;
        }
        u16 const     inext = m_head;
        node_t* const pnext = idx2node(list, inext);
        u16 const     iitem = pnext->m_prev;
        node_t* const pitem = idx2node(list, iitem);
        u16 const     iprev = pitem->m_prev;
        node_t* const pprev = idx2node(list, iprev);
        pprev->m_next       = inext;
        pnext->m_prev       = iprev;
        pitem->link(NIL, NIL);
        if (m_count == 1)
        {
            m_head = NIL;
        }
        m_count -= 1;
        return pitem;
    }

    xalist_t::node_t* xalist_t::idx2node(node_t* list, u16 i)
    {
        if (i == NIL)
            return nullptr;
        return &list[i];
    }

    u16 xalist_t::node2idx(node_t* list, node_t* node)
    {
        if (node == nullptr)
            return NIL;
        u16 const index = (u16)(((u64)node - (u64)&list[0]) / sizeof(node_t));
        return index;
    }

} // namespace xcore