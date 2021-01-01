#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_singly_linked_list.h"

namespace xcore
{
    void list_t::initialize(u32 const sizeof_node, lnode_t* list, u16 start, u16 size, u16 max_size)
    {
        ASSERT(max_size > 0);
        ASSERT(size <= max_size);
        m_size         = size;
        m_size_max     = max_size;
        m_head.m_index = start;

        const u16 end = start + size;
        for (u16 i = 0; i < size; ++i)
        {
            list_t::idx2node(sizeof_node, list, i)->link(i + 1);
        }
        list_t::idx2node(sizeof_node, list, end - 1)->unlink();
    }

    void lhead_t::insert(u32 const sizeof_node, lnode_t* list, lindex_t item)
    {
        lnode_t* const pitem = idx2node(sizeof_node, list, item);
        ASSERT(pitem->is_linked() == false);
        if (is_nil())
        {
            pitem->link(item);
        }
        else
        {
            lindex_t const inext = m_index;
            pitem->link(inext);
        }
        m_index = item;
    }

    static s32 s_remove_head(lhead_t& head, u32 const sizeof_node, lnode_t* nodes, lnode_t*& out_node)
    {
        if (head.is_nil())
        {
            out_node = nullptr;
            return 0;
        }

        lnode_t* const phead = lhead_t::idx2node(sizeof_node, nodes, head.m_index);
        if (!phead->is_linked())
        {
            head.reset();
        }
        else
        {
            lindex_t const inext = phead->m_next;
            lnode_t* const pnext = lhead_t::idx2node(sizeof_node, nodes, inext);
            head.m_index         = inext;
        }

        phead->unlink();
        out_node = phead;
        return 1;
    }

    lnode_t* lhead_t::remove(u32 const sizeof_node, lnode_t* list)
    {
        lnode_t* node;
        s_remove_head(*this, sizeof_node, list, node);
        return node;
    }

    lindex_t lhead_t::remove_i(u32 const sizeof_node, lnode_t* list)
    {
        lnode_t* node;
        s_remove_head(*this, sizeof_node, list, node);
        return node2idx(sizeof_node, list, node);
    }

    void list_t::insert(u32 const sizeof_node, lnode_t* list, lindex_t item)
    {
        ASSERT(m_size < m_size_max);
        m_head.insert(sizeof_node, list, item);
        m_size += 1;
    }

    lnode_t* list_t::remove(u32 const sizeof_node, lnode_t* list)
    {
        lnode_t* node;
        ASSERT(m_size > 0);
        m_size -= s_remove_head(m_head, sizeof_node, list, node);
        return node;
    }

    lindex_t list_t::remove_i(u32 const sizeof_node, lnode_t* list)
    {
        lnode_t* node;
        ASSERT(m_size > 0);
        m_size -= s_remove_head(m_head, sizeof_node, list, node);
        return node2idx(sizeof_node, list, node);
    }

} // namespace xcore