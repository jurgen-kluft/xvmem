#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_doubly_linked_list.h"

namespace xcore
{
    const u16 xalist_t::NIL = 0xffff;

    void xalist_t::initialize(node_t* list, u16 size, u16 max_size)
    {
        ASSERT(max_size > 0);
        ASSERT(size <= max_size);
        m_size     = size;
        m_size_max = max_size;
        m_head     = NIL;

        m_head = 0;
        for (u32 i = 0; i < max_size; ++i)
        {
            list[i].link(i - 1, i + 1);
        }
        list[0].link(max_size - 1, 1);
        list[max_size - 1].link(max_size - 2, 0);
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
        m_size += 1;
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
        m_size += 1;
    }

    static void s_remove_item(xalist_t& list, xalist_t::node_t* nodes, u16 item, xalist_t::node_t*& out_node, u16& out_idx)
    {
        xalist_t::node_t* const pitem = list.idx2node(nodes, item);
        xalist_t::node_t* const pprev = list.idx2node(nodes, pitem->m_prev);
        xalist_t::node_t* const pnext = list.idx2node(nodes, pitem->m_next);
        pprev->m_next                 = pitem->m_next;
        pnext->m_prev                 = pitem->m_prev;
        pitem->unlink();
        ASSERT(list.m_size >= 1);
        if (list.m_size == 1)
        {
            list.m_head = xalist_t::NIL;
            list.m_size = 0;
        }
        else
        {
            if (list.m_head == item)
            {
                list.m_head = list.node2idx(nodes, pnext);
            }
            list.m_size--;
        }
        out_node = pitem;
        out_idx  = item;
    }

    static bool s_remove_head(xalist_t& list, xalist_t::node_t* nodes, xalist_t::node_t*& out_node, u16& out_idx)
    {
        if (list.m_head == xalist_t::NIL)
        {
            out_node = nullptr;
            out_idx  = xalist_t::NIL;
        }

        u16 const               iitem = list.m_head;
        xalist_t::node_t* const pitem = list.idx2node(nodes, iitem);
        u16 const               inext = pitem->m_next;
        xalist_t::node_t* const pnext = list.idx2node(nodes, inext);
        u16 const               iprev = pitem->m_prev;
        xalist_t::node_t* const pprev = list.idx2node(nodes, iprev);
        pprev->m_next                 = inext;
        pnext->m_prev                 = iprev;
        pitem->link(xalist_t::NIL, xalist_t::NIL);

        ASSERT(list.m_size >= 1);
        if (list.m_size == 1)
        {
            list.m_head = xalist_t::NIL;
        }
        else
        {
            list.m_head = inext;
        }
        list.m_size -= 1;

        out_node = pitem;
        out_idx  = iitem;
        return true;
    }

    static bool s_remove_tail(xalist_t& list, xalist_t::node_t* nodes, xalist_t::node_t*& out_node, u16& out_idx)
    {
        if (list.m_head == xalist_t::NIL)
        {
            out_node = nullptr;
            out_idx  = xalist_t::NIL;
            return false;
        }
        u16 const               inext = list.m_head;
        xalist_t::node_t* const pnext = list.idx2node(nodes, inext);
        u16 const               iitem = pnext->m_prev;
        xalist_t::node_t* const pitem = list.idx2node(nodes, iitem);
        u16 const               iprev = pitem->m_prev;
        xalist_t::node_t* const pprev = list.idx2node(nodes, iprev);
        pprev->m_next                 = inext;
        pnext->m_prev                 = iprev;
        pitem->link(xalist_t::NIL, xalist_t::NIL);

        ASSERT(list.m_size >= 1);
        if (list.m_size == 1)
        {
            list.m_head = xalist_t::NIL;
        }
        list.m_size -= 1;

        out_node = pitem;
        out_idx  = iitem;
        return true;
    }

    xalist_t::node_t* xalist_t::remove_item(node_t* list, u16 item)
    {
        xalist_t::node_t* node;
        u16               index;
        s_remove_item(*this, list, item, node, index);
        return node;
    }

    xalist_t::node_t* xalist_t::remove_head(node_t* list)
    {
        xalist_t::node_t* node;
        u16               index;
        s_remove_head(*this, list, node, index);
        return node;
    }

    xalist_t::node_t* xalist_t::remove_tail(node_t* list)
    {
        xalist_t::node_t* node;
        u16               index;
        s_remove_tail(*this, list, node, index);
        return node;
    }

    u16 xalist_t::remove_headi(node_t* list)
    {
        xalist_t::node_t* node;
        u16               index;
        s_remove_head(*this, list, node, index);
        return index;
    }

    u16 xalist_t::remove_taili(node_t* list)
    {
        xalist_t::node_t* node;
        u16               index;
        s_remove_tail(*this, list, node, index);
        return index;
    }

} // namespace xcore