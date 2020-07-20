
#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"
#include "xbase/x_hibitset.h"

#include "xvmem/private/x_size_db.h"
#include "xvmem/private/x_addr_db.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    static inline void rescan_for_size_index(xaddr_db* db, u32 const addr_index, u8 const size_index, u32 const node_flag, xsize_db* size_db, xdexer* dexer)
    {
        if (db->has_size_index(addr_index, dexer, size_index, node_flag))
        {
            size_db->insert_size(size_index, addr_index);
        }
    }

	void xaddr_db::initialize(xalloc* main_heap, u64 memory_range, u32 addr_count)
	{
        ASSERT(xispo2(addr_count)); // The address node count should be a power-of-2
        m_addr_count = addr_count;
        m_addr_range = (u32)(memory_range / addr_count);
        m_nodes      = (u32*)main_heap->allocate(addr_count * sizeof(u32), sizeof(void*));
        reset();
	}

	void xaddr_db::release(xalloc* main_heap)
	{
		main_heap->deallocate(m_nodes);
		m_nodes = nullptr;
		m_addr_count = 0;
		m_addr_range = 0;
	}

    void xaddr_db::reset()
    {
        for (u32 i = 0; i < m_addr_count; ++i)
            m_nodes[i] = node_t::NIL;
    }

    void xaddr_db::alloc(u32 inode, node_t* pnode, xfsadexed* node_alloc, xsize_db* size_db, xsize_cfg const& size_cfg)
    {
        // Mark this node as 'used' and thus remove it from the size-db
        pnode->set_used();
        u32       node_sidx = pnode->get_size_index();
        u32 const node_aidx = pnode->get_addr_index(m_addr_range);

        // If node 'node_aidx' still has nodes with the same size-index then we do not need to
        // remove this from the size-db.
        if (!has_size_index(node_aidx, node_alloc, node_sidx, node_t::FLAG_FREE))
        {
            size_db->remove_size(node_sidx, node_aidx);
        }
    }

    void xaddr_db::alloc_by_split(u32 inode, node_t* pnode, u32 size, xfsadexed* node_alloc, xsize_db* size_db, xsize_cfg const& size_cfg)
    {
        u32 const node_sidx = pnode->get_size_index();
        u32 const node_aidx = pnode->get_addr_index(m_addr_range);
        size_db->remove_size(node_sidx, node_aidx);

        // we need to insert the new node between pnode and it's next node
        u32 const     inext = pnode->m_next_addr;
        node_t* const pnext = (node_t*)node_alloc->idx2ptr(inext);

        // create the new node
        node_t*   pnew = (node_t*)node_alloc->allocate();
        u32 const inew = node_alloc->ptr2idx(pnew);
        pnew->init();

        // Set the new size on node and next
        u32 const node_addr = pnode->get_addr();
        u32 const new_addr  = node_addr + size;
        u32 const new_size  = pnode->get_size() - size;
        pnode->set_used();
        pnode->set_size(size, size_cfg.size_to_index(size));
        pnew->set_size(new_size, size_cfg.size_to_index(new_size));
        pnew->set_addr(new_addr);

        // Link this new node into the address list
        // node <-> new <-> next
        pnew->m_prev_addr  = inode;
        pnew->m_next_addr  = inext;
        pnode->m_next_addr = inew;
        pnext->m_prev_addr = inew;

        // See if this new node is now part of a address node
        u32 const i = pnew->get_addr_index(m_addr_range);
        if (m_nodes[i] == node_t::NIL)
        {
            m_nodes[i] = inew;
        }
        else
        {
            // There already is a list at that address node, check to see if we are the new head
            u32 const     ihead = m_nodes[i];
            node_t* const phead = (node_t*)node_alloc->idx2ptr(ihead);
            if (pnew->get_addr() < phead->get_addr())
            {
                m_nodes[i] = inew;
            }
        }

        // our new node has to be marked in the size-db
        u32 const new_aidx = pnew->get_addr_index(m_addr_range);
        u32 const new_sidx = pnew->get_size_index();
        size_db->insert_size(new_sidx, new_aidx);
    }

    void xaddr_db::dealloc(u32 inode, node_t* pnode, bool merge_prev, bool merge_next, xsize_db* size_db, xsize_cfg const& size_cfg, xfsadexed* node_heap)
    {
        if (!merge_next && !merge_prev)
        {
            u32 const node_size_index = pnode->get_size_index();
            u32 const node_addr_index = pnode->get_addr_index(m_addr_range);
            size_db->remove_size(node_size_index, node_addr_index);

            remove_node(inode, pnode, node_heap); // we can safely remove 'node' from the address db
            node_heap->deallocate(pnode);
            
            rescan_for_size_index(this, node_addr_index, node_size_index, node_t::FLAG_FREE, size_db, node_heap);
        }
        else
        {
            if (merge_next)
            {
                u32 const inext = pnode->m_next_addr;
                node_t*   pnext = (node_t*)node_heap->idx2ptr(inext);

                u32 const node_size_index = pnode->get_size_index();
                u32 const node_addr_index = pnode->get_addr_index(m_addr_range);
                size_db->remove_size(node_size_index, node_addr_index);
                u32 const next_size_index = pnext->get_size_index();
                u32 const next_addr_index = pnext->get_addr_index(m_addr_range);
                size_db->remove_size(next_size_index, next_addr_index);

                u32 const next_size = pnext->get_size();
                u32 const node_size = pnode->get_size() + next_size;
                pnode->set_size(node_size, size_cfg.size_to_index(node_size));

                remove_node(inext, pnext, node_heap); // we can safely remove 'next' from the address db
                node_heap->deallocate(pnext);

                rescan_for_size_index(this, node_addr_index, node_size_index, node_t::FLAG_FREE, size_db, node_heap);
                rescan_for_size_index(this, next_addr_index, next_size_index, node_t::FLAG_FREE, size_db, node_heap);
                rescan_for_size_index(this, pnode->get_addr_index(m_addr_range), pnode->get_size_index(), node_t::FLAG_FREE, size_db, node_heap);
            }
            if (merge_prev)
            {
                u32 const iprev = pnode->m_prev_addr;
                node_t*   pprev = (node_t*)node_heap->idx2ptr(iprev);

                u32 const prev_size_index = pprev->get_size_index();
                u32 const prev_addr_index = pprev->get_addr_index(m_addr_range);
                size_db->remove_size(prev_size_index, prev_addr_index);
                u32 const node_size_index = pnode->get_size_index();
                u32 const node_addr_index = pnode->get_addr_index(m_addr_range);
                size_db->remove_size(node_size_index, node_addr_index);

                u32 const node_size = pnode->get_size();
                u32 const prev_size = pprev->get_size() + node_size;
                pprev->set_size(prev_size, size_cfg.size_to_index(prev_size));

                remove_node(inode, pnode, node_heap); // we can safely remove 'prev' from the address db
                node_heap->deallocate(pnode);

                rescan_for_size_index(this, prev_addr_index, prev_size_index, node_t::FLAG_FREE, size_db, node_heap);
                rescan_for_size_index(this, node_addr_index, node_size_index, node_t::FLAG_FREE, size_db, node_heap);
                rescan_for_size_index(this, pprev->get_addr_index(m_addr_range), pprev->get_size_index(), node_t::FLAG_FREE, size_db, node_heap);
            }
        }
    }

    void xaddr_db::remove_node(u32 inode, node_t* pnode, xdexer* dexer)
    {
        u32 const i = pnode->get_addr_index(m_addr_range);
        if (m_nodes[i] == inode)
        {
            u32 const inext = pnode->m_next_addr;
            node_t*   pnext = (node_t*)dexer->idx2ptr(inext);
            if (pnext->get_addr_index(m_addr_range) == i)
            {
                m_nodes[i] = inext;
            }
            else
            {
                m_nodes[i] = node_t::NIL;
            }
        }

        // Remove node from the address list
        u32 const iprev    = pnode->m_prev_addr;
        u32 const inext    = pnode->m_next_addr;
        node_t*   pprev    = (node_t*)dexer->idx2ptr(iprev);
        node_t*   pnext    = (node_t*)dexer->idx2ptr(inext);
        pprev->m_next_addr = inext;
        pnext->m_prev_addr = iprev;
        pnode->m_prev_addr = node_t::NIL;
        pnode->m_next_addr = node_t::NIL;
    }

    xaddr_db::node_t* xaddr_db::get_node_with_addr(u32 const i, xdexer* dexer, u32 addr)
    {
        u32 inode = m_nodes[i];
        if (inode != node_t::NIL)
        {
            node_t* pnode = (node_t*)dexer->idx2ptr(inode);
            do
            {
                if (pnode->is_used() && pnode->get_addr() == addr)
                {
                    return pnode;
                }
                inode = pnode->m_next_addr;
                pnode = (node_t*)dexer->idx2ptr(inode);
            } while (pnode->get_addr_index(m_addr_range) == i);
        }
        return nullptr;
    }

    xaddr_db::node_t* xaddr_db::get_node_with_size_index(u32 const i, xdexer* dexer, u32 size_index)
    {
        u32 inode = m_nodes[i];
        if (inode != node_t::NIL)
        {
            node_t* pnode = (node_t*)dexer->idx2ptr(inode);
            do
            {
                if (!pnode->is_used() && pnode->get_size_index() == size_index)
                {
                    return pnode;
                }
                inode = pnode->m_next_addr;
                pnode = (node_t*)dexer->idx2ptr(inode);
            } while (pnode->get_addr_index(m_addr_range) == i);
        }
        return nullptr;
    }

    bool xaddr_db::has_size_index(u32 const i, xdexer* dexer, u32 size_index, u32 node_flag) const
    {
        u32 inode = m_nodes[i];
        if (inode != node_t::NIL)
        {
            node_t* pnode = (node_t*)dexer->idx2ptr(inode);
            do
            {
                if (pnode->is_flag(node_flag) && pnode->get_size_index() == size_index)
                {
                    return true;
                }
                inode = pnode->m_next_addr;
                pnode = (node_t*)dexer->idx2ptr(inode);
            } while (pnode->get_addr_index(m_addr_range) == i);
        }
        return false;
    }

} // namespace xcore
