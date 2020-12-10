#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_doubly_linked_list.h"

namespace xcore
{
    void llist_t::initialize(llnode_t* list, u16 start, u16 size, u16 max_size)
    {
        ASSERT(max_size > 0);
        ASSERT(size <= max_size);
        m_size     = size;
        m_size_max = max_size;
        m_head     = start;

        const u16 end = start + size;
        for (u16 i = 0; i < size; ++i)
        {
            u16 const t = start + i;
            list[t].link(t - 1, t + 1);
        }
        list[start].link(end - 1, start + 1);
        list[end - 1].link(end - 2, start);
    }

    void llhead_t::insert(llnode_t* list, llindex_t item)
    {
        llnode_t* const pitem = idx2node(list, item);
        if (is_nil())
        {
            pitem->link(item, item);
        }
        else
        {
            llindex_t const inext = m_index;
            llnode_t* const pnext = idx2node(list, inext);
            llindex_t const iprev = pnext->m_prev;
            llnode_t* const pprev = idx2node(list, iprev);
            pitem->link(iprev, inext);
            pnext->m_prev = item;
            pprev->m_next = item;
        }
        m_index = item.get();
    }

    void llhead_t::insert_tail(llnode_t* list, llindex_t item)
    {
        llnode_t* const pitem = idx2node(list, item);
        if (is_nil())
        {
            pitem->link(item, item);
            m_index = item.get();
        }
        else
        {
            llindex_t const inext = m_index;
            llnode_t* const pnext = idx2node(list, inext);
            llindex_t const iprev = pnext->m_prev;
            llnode_t* const pprev = idx2node(list, iprev);
            pitem->link(iprev, inext);
            pnext->m_prev = item;
            pprev->m_next = item;
        }
    }

    static s32 s_remove_item(llhead_t& head, llnode_t* nodes, llindex_t item, llnode_t*& out_node)
    {
        llnode_t* const pitem = llhead_t::idx2node(nodes, item);
        llnode_t* const pprev = llhead_t::idx2node(nodes, pitem->m_prev);
        llnode_t* const pnext = llhead_t::idx2node(nodes, pitem->m_next);
        pprev->m_next       = pitem->m_next;
        pnext->m_prev       = pitem->m_prev;

        if (head.is_nil())
        {
            ASSERT(false);  // Should not happen!
            pitem->unlink();
            return 0;
        }
        else
        {
            llnode_t* const phead = llhead_t::idx2node(nodes, head);
            if (phead->is_last())
            {
                ASSERT(head.get() == item.get());
                head.reset();
                pitem->unlink();
            }
            else
            {
                pprev->m_next = pitem->m_next;
                pnext->m_prev = pitem->m_prev;
                pitem->unlink();
                head = pprev->m_next;
        }
        out_node = pitem;
        return 1;
        }
    }

    static s32 s_remove_head(llhead_t& head, llnode_t* nodes, llnode_t*& out_node)
    {
        if (head.is_nil())
        {
            out_node = nullptr;
            return 0;
        }

        llnode_t* const phead = llhead_t::idx2node(nodes, head);
        if (phead->is_last())
        {
            head.reset();
        }
        else
        {
            llindex_t const inext = phead->m_next;
            llnode_t* const pnext = llhead_t::idx2node(nodes, inext);
            llindex_t const iprev = phead->m_prev;
            llnode_t* const pprev = llhead_t::idx2node(nodes, iprev);
            pprev->m_next = inext;
            pnext->m_prev = iprev;
            head          = inext;
        }

        phead->unlink();
        out_node = phead;
        return 1;
    }

    static s32 s_remove_tail(llhead_t& head, llnode_t* nodes, llnode_t*& out_node)
    {
        if (head.is_nil())
        {
            out_node = nullptr;
            return false;
        }
        llindex_t const inext = head;
        llnode_t* const pnext = llhead_t::idx2node(nodes, inext);
        if (pnext->is_last())
        {
            head.reset();
            pnext->unlink();
            out_node = pnext;
        }
        else
        {
            llindex_t const iitem = pnext->m_prev;
            llnode_t* const pitem = llhead_t::idx2node(nodes, iitem);
            llindex_t const iprev = pitem->m_prev;
            llnode_t* const pprev = llhead_t::idx2node(nodes, iprev);
            pprev->m_next = inext;
            pnext->m_prev = iprev;
            pitem->unlink();
            out_node = pitem;
            head = pprev->m_next;
        }
        return true;
    }

    llnode_t* llhead_t::remove_item(llnode_t* list, llindex_t item)
    {
        llnode_t* node;
        s_remove_item(*this, list, item, node);
        return node;
    }

    llnode_t* llhead_t::remove_head(llnode_t* list)
    {
        llnode_t* node;
        s_remove_head(*this, list, node);
        return node;
    }

    llnode_t* llhead_t::remove_tail(llnode_t* list)
    {
        llnode_t* node;
        s_remove_tail(*this, list, node);
        return node;
    }

    llindex_t llhead_t::remove_headi(llnode_t* list)
    {
        llnode_t* node;
        s_remove_head(*this, list, node);
        return node2idx(list, node);
    }

    llindex_t llhead_t::remove_taili(llnode_t* list)
    {
        llnode_t* node;
        s_remove_tail(*this, list, node);
        return node2idx(list, node);
    }

    void llist_t::insert(llnode_t* list, llindex_t item)
    {
        ASSERT(m_size < m_size_max);
        m_head.insert(list, item);
        m_size += 1;
    }

    void llist_t::insert_tail(llnode_t* list, llindex_t item)
    {
        ASSERT(m_size < m_size_max);
        m_head.insert_tail(list, item);
        m_size += 1;
    }

    llnode_t* llist_t::remove_item(llnode_t* list, llindex_t item)
    {
        llnode_t* node;
        ASSERT(m_size > 0);
        m_size -= s_remove_item(m_head, list, item, node);
        return node;
    }

    llnode_t* llist_t::remove_head(llnode_t* list)
    {
        llnode_t* node;
        ASSERT(m_size > 0);
        m_size -= s_remove_head(m_head, list, node);
        return node;
    }

    llnode_t* llist_t::remove_tail(llnode_t* list)
    {
        llnode_t* node;
        ASSERT(m_size > 0);
        m_size -= s_remove_tail(m_head, list, node);
        return node;
    }

    llindex_t llist_t::remove_headi(llnode_t* list)
    {
        llnode_t* node;
        ASSERT(m_size > 0);
        m_size -= s_remove_head(m_head, list, node);
        return node2idx(list, node);
    }

    llindex_t llist_t::remove_taili(llnode_t* list)
    {
        llnode_t* node;
        ASSERT(m_size > 0);
        m_size -= s_remove_tail(m_head, list, node);
        return node2idx(list, node);
    }

} // namespace xcore