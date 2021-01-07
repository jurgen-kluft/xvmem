#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_doubly_linked_list.h"

namespace xcore
{
    void llist_t::initialize(u32 const sizeof_node, llnode_t* list, u16 start, u16 size, u16 max_size)
    {
        ASSERT(max_size > 0);
        ASSERT(size <= max_size);
        m_size         = size;
        m_size_max     = max_size;
        m_head.m_index = start;

        const u16 end = start + size;
        for (u16 i = 0; i < size; ++i)
        {
            u16 const t    = start + i;
            llnode_t* node = llist_t::idx2node(sizeof_node, list, t);
            node->m_prev = (t - 1);
            node->m_next = (t + 1);
        }
        llnode_t* snode = llist_t::idx2node(sizeof_node, list, start);
        snode->m_prev = end - 1;
        snode->m_next = start + 1;
        llnode_t* enode = llist_t::idx2node(sizeof_node, list, end - 1);
        enode->m_prev = end - 2;
        enode->m_next = start;

    }

    void llhead_t::insert(u32 const sizeof_node, llnode_t* list, llindex_t item)
    {
        llnode_t* const pitem = idx2node(sizeof_node, list, item);
        if (is_nil())
        {
            pitem->m_prev = item;
            pitem->m_next = item;
        }
        else
        {
            llindex_t const inext = m_index;
            llnode_t* const pnext = idx2node(sizeof_node, list, inext);
            llindex_t const iprev = pnext->m_prev;
            llnode_t* const pprev = idx2node(sizeof_node, list, iprev);
            pitem->m_prev = iprev;
            pitem->m_next = inext;
            pnext->m_prev = item;
            pprev->m_next = item;
        }
        m_index = item;
    }

    void llhead_t::insert_tail(u32 const sizeof_node, llnode_t* list, llindex_t item)
    {
        llnode_t* const pitem = idx2node(sizeof_node, list, item);
        if (is_nil())
        {
            pitem->m_prev = item;
            pitem->m_next = item;
            m_index = item;
        }
        else
        {
            llindex_t const inext = m_index;
            llnode_t* const pnext = idx2node(sizeof_node, list, inext);
            llindex_t const iprev = pnext->m_prev;
            llnode_t* const pprev = idx2node(sizeof_node, list, iprev);
            pitem->m_prev = iprev;
            pitem->m_next = inext;
            pnext->m_prev = item;
            pprev->m_next = item;
        }
    }

    static s32 s_remove_item(llhead_t& head, u32 const sizeof_node, llnode_t* list, llindex_t item, llnode_t*& out_node)
    {
        llnode_t* const pitem = llhead_t::idx2node(sizeof_node, list, item);
        llnode_t* const pprev = llhead_t::idx2node(sizeof_node, list, pitem->m_prev);
        llnode_t* const pnext = llhead_t::idx2node(sizeof_node, list, pitem->m_next);

        if (head.is_nil())
        {
            ASSERT(false); // Should not happen!
            pitem->m_prev = 0xFFFF;
            pitem->m_next = 0xFFFF;
            return 0;
        }
        else
        {
            llnode_t* const phead = llhead_t::idx2node(sizeof_node, list, head.m_index);
            if (phead->m_prev == head.m_index && phead->m_next == head.m_index)
            {
                ASSERT(head.m_index == item);
                head.reset();
                pitem->m_prev = 0xFFFF;
                pitem->m_next = 0xFFFF;
            }
            else
            {
                pprev->m_next = pitem->m_next;
                pnext->m_prev = pitem->m_prev;
                pitem->m_prev = 0xFFFF;
                pitem->m_next = 0xFFFF;
                head.m_index = pprev->m_next;
            }
            out_node = pitem;
            return 1;
        }
    }

    static s32 s_remove_head(llhead_t& head, u32 const sizeof_node, llnode_t* list, llnode_t*& out_node)
    {
        if (head.is_nil())
        {
            out_node = nullptr;
            return 0;
        }

        llnode_t* const phead = llhead_t::idx2node(sizeof_node, list, head.m_index);
        if (phead->m_prev == head.m_index && phead->m_next == head.m_index)
        {
            head.reset();
        }
        else
        {
            llindex_t const inext = phead->m_next;
            llnode_t* const pnext = llhead_t::idx2node(sizeof_node, list, inext);
            llindex_t const iprev = phead->m_prev;
            llnode_t* const pprev = llhead_t::idx2node(sizeof_node, list, iprev);
            pprev->m_next         = inext;
            pnext->m_prev         = iprev;
            head.m_index          = inext;
        }
        phead->m_prev = 0xFFFF;
        phead->m_next = 0xFFFF;
        out_node = phead;
        return 1;
    }

    static s32 s_remove_tail(llhead_t& head, u32 const sizeof_node, llnode_t* list, llnode_t*& out_node)
    {
        if (head.is_nil())
        {
            out_node = nullptr;
            return false;
        }
        llindex_t const inext = head.m_index;
        llnode_t* const pnext = llhead_t::idx2node(sizeof_node, list, inext);
        if (pnext->m_prev == head.m_index && pnext->m_next == head.m_index)
        {
            head.reset();
            pnext->m_prev = 0xFFFF;
            pnext->m_next = 0xFFFF;
            out_node = pnext;
        }
        else
        {
            llindex_t const iitem = pnext->m_prev;
            llnode_t* const pitem = llhead_t::idx2node(sizeof_node, list, iitem);
            llindex_t const iprev = pitem->m_prev;
            llnode_t* const pprev = llhead_t::idx2node(sizeof_node, list, iprev);
            pprev->m_next         = inext;
            pnext->m_prev         = iprev;
            pitem->m_prev = 0xFFFF;
            pitem->m_next = 0xFFFF;
            out_node     = pitem;
            head.m_index = pprev->m_next;
        }
        return true;
    }

    llnode_t* llhead_t::remove_item(u32 const sizeof_node, llnode_t* list, llindex_t item)
    {
        llnode_t* node;
        s_remove_item(*this, sizeof_node, list, item, node);
        return node;
    }

    llnode_t* llhead_t::remove_head(u32 const sizeof_node, llnode_t* list)
    {
        llnode_t* node;
        s_remove_head(*this, sizeof_node, list, node);
        return node;
    }

    llnode_t* llhead_t::remove_tail(u32 const sizeof_node, llnode_t* list)
    {
        llnode_t* node;
        s_remove_tail(*this, sizeof_node, list, node);
        return node;
    }

    llindex_t llhead_t::remove_headi(u32 const sizeof_node, llnode_t* list)
    {
        llnode_t* node;
        s_remove_head(*this, sizeof_node, list, node);
        return node2idx(sizeof_node, list, node);
    }

    llindex_t llhead_t::remove_taili(u32 const sizeof_node, llnode_t* list)
    {
        llnode_t* node;
        s_remove_tail(*this, sizeof_node, list, node);
        return node2idx(sizeof_node, list, node);
    }

    void llist_t::insert(u32 const sizeof_node, llnode_t* list, llindex_t item)
    {
        ASSERT(m_size < m_size_max);
        m_head.insert(sizeof_node, list, item);
        m_size += 1;
    }

    void llist_t::insert_tail(u32 const sizeof_node, llnode_t* list, llindex_t item)
    {
        ASSERT(m_size < m_size_max);
        m_head.insert_tail(sizeof_node, list, item);
        m_size += 1;
    }

    llnode_t* llist_t::remove_item(u32 const sizeof_node, llnode_t* list, llindex_t item)
    {
        llnode_t* node;
        ASSERT(m_size > 0);
        m_size -= s_remove_item(m_head, sizeof_node, list, item, node);
        return node;
    }

    llnode_t* llist_t::remove_head(u32 const sizeof_node, llnode_t* list)
    {
        llnode_t* node;
        ASSERT(m_size > 0);
        m_size -= s_remove_head(m_head, sizeof_node, list, node);
        return node;
    }

    llnode_t* llist_t::remove_tail(u32 const sizeof_node, llnode_t* list)
    {
        llnode_t* node;
        ASSERT(m_size > 0);
        m_size -= s_remove_tail(m_head, sizeof_node, list, node);
        return node;
    }

    llindex_t llist_t::remove_headi(u32 const sizeof_node, llnode_t* list)
    {
        llnode_t* node;
        ASSERT(m_size > 0);
        m_size -= s_remove_head(m_head, sizeof_node, list, node);
        return node2idx(sizeof_node, list, node);
    }

    llindex_t llist_t::remove_taili(u32 const sizeof_node, llnode_t* list)
    {
        llnode_t* node;
        ASSERT(m_size > 0);
        m_size -= s_remove_tail(m_head, sizeof_node, list, node);
        return node2idx(sizeof_node, list, node);
    }

} // namespace xcore