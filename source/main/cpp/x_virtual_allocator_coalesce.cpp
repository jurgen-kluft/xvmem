#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_hibitset.h"

#include "xvmem/x_virtual_allocator.h"
#include "xvmem/x_virtual_pages.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    struct naddr_t
    {
        static u32 const NIL            = 0xffffffff;
        static u32 const FLAG_COLOR_RED = 0x1;
        static u32 const FLAG_FREE      = 0x2;
        static u32 const FLAG_USED      = 0x4;
        static u32 const FLAG_LOCKED    = 0x8;
        static u32 const FLAG_MASK      = 0xF;

        u32 m_addr;      // addr = base_addr(m_addr * size_step)
        u32 m_flags;     // [Allocated, Free, Locked, Color]
        u32 m_size;      // size = m_size * size_step
        u32 m_prev_addr; // previous node in memory, can be free, can be allocated
        u32 m_next_addr; // next node in memory, can be free, can be allocated
        
        // This is going to be changed into a BST node
        u32 m_prev_db;   // as a linked-list node in the addr_db or size_db
        u32 m_next_db;   // as a linked-list node in the addr_db or size_db

        void init()
        {
            m_addr      = 0;
            m_flags     = 0;
            m_size      = 0;
            m_prev_addr = naddr_t::NIL;
            m_next_addr = naddr_t::NIL;
            m_prev_db   = naddr_t::NIL;
            m_next_db   = naddr_t::NIL;
        }

        inline void* get_addr(void* baseaddr, u64 size_step) const { return (void*)((u64)baseaddr + ((u64)m_addr * size_step)); }
        inline void  set_addr(void* baseaddr, u64 size_step, void* addr) { m_addr = (u32)(((u64)addr - (u64)baseaddr) / size_step); }
        inline void  set_locked() { m_flags = m_flags | FLAG_LOCKED; }
        inline void  set_used(bool used) { m_flags = m_flags | FLAG_USED; }
        inline bool  is_free() const { return (m_flags & FLAG_MASK) == FLAG_FREE; }
        inline bool  is_locked() const { return (m_flags & FLAG_MASK) == FLAG_LOCKED; }

		XCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    static inline void* addr_add(void* addr, u64 offset) { return (void*)((u64)addr + offset); }

    class xcoalescee : public xalloc
    {
    public:
        xcoalescee();

        // Main API
        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);

        // Coalesce related functions
        void        initialize(xalloc* main_heap, xfsadexed* node_heap, void* mem_addr, u64 mem_size, u32 size_min, u32 size_max, u32 size_step, u32 list_size);
        void        release();
        void        remove_from_addr(u32 inode, naddr_t* pnode);
        void        add_to_addr_db(u32 inode, naddr_t* pnode);
        bool        pop_from_addr_db(void* ptr, u32& inode, naddr_t*& pnode);
        bool        can_split_node(u32 size, u32 alignment, u32 inode, naddr_t* pnode);
        void        split_node(u32 size, u32 alignment, u32 inode, naddr_t* pnode, u32& rinode, naddr_t*& rpnode);
        bool        pop_from_size_db(u32 size, u32& inode, naddr_t*& pnode);
        void        add_to_size_db(u32 inode, naddr_t* pnode);
        void        remove_from_size_db(u32 inode, naddr_t* pnode);
        inline void dealloc_node(u32 inode, naddr_t* pnode)
        {
            ASSERT(m_node_heap->idx2ptr(inode) == pnode);
            m_node_heap->deallocate(pnode);
        }
        inline u32 calc_size_slot(u32 size) const
        {
            ASSERT(size >= m_alloc_size_min);
            u32 const slot = (size - m_alloc_size_min) / m_alloc_size_step;
            ASSERT(slot < m_size_nodes_cnt);
            return slot;
        }
        inline u32 calc_addr_slot(void* addr) const
        {
            u32 const slot = (u32)(((u64)addr - (u64)m_memory_addr) / m_addr_alignment);
            ASSERT(slot < m_addr_nodes_cnt);
            return slot;
        }
        inline naddr_t* idx2naddr(u32 idx)
        {
            naddr_t* pnode = nullptr;
            if (idx != naddr_t::NIL)
                pnode = (naddr_t*)m_node_heap->idx2ptr(idx);
            return pnode;
        }

        xalloc*    m_main_heap;
        xfsadexed* m_node_heap;
        void*      m_memory_addr;
        u64        m_memory_size;
        u32        m_alloc_size_min;
        u32        m_alloc_size_max;
        u32        m_alloc_size_step;
        u32        m_size_nodes_cnt;
        u32*       m_size_nodes;
        u32        m_size_nodes_large;
        xhibitset  m_size_nodes_occupancy;
        u64        m_addr_alignment;
        u32        m_addr_nodes_cnt;
        u32*       m_addr_nodes;
    };

    xcoalescee::xcoalescee()
        : m_main_heap(nullptr)
        , m_node_heap(nullptr)
        , m_memory_addr(nullptr)
        , m_memory_size(0)
        , m_alloc_size_min(0)
        , m_alloc_size_max(0)
        , m_alloc_size_step(0)
        , m_size_nodes(nullptr)
        , m_size_nodes_large(naddr_t::NIL)
        , m_addr_nodes(nullptr)
        , m_addr_alignment(0)
    {
    }

    void xcoalescee::initialize(xalloc* main_heap, xfsadexed* node_heap, void* mem_addr, u64 mem_size, u32 size_min, u32 size_max, u32 size_step, u32 list_size)
    {
        m_main_heap       = main_heap;
        m_node_heap       = node_heap;
        m_memory_addr     = mem_addr;
        m_memory_size     = mem_size;
        m_alloc_size_min  = size_min;
        m_alloc_size_max  = size_max;
        m_alloc_size_step = size_step;
        m_addr_alignment  = size_min * list_size;

        m_size_nodes_cnt = (m_alloc_size_max - m_alloc_size_min) / m_alloc_size_step;
        m_size_nodes     = (u32*)m_main_heap->allocate(m_size_nodes_cnt * sizeof(u32), sizeof(void*));
        for (u32 i = 0; i < m_size_nodes_cnt; i++)
        {
            m_size_nodes[i] = naddr_t::NIL;
        }
        m_size_nodes_large = naddr_t::NIL;

        // Which sizes are available in 'm_size_nodes' is known through
        // this hierarchical set of bits.
        m_size_nodes_occupancy.init(m_main_heap, m_size_nodes_cnt, xhibitset::FIND_1);

        // Please keep the number of addr slots low
        ASSERT((m_memory_size / m_addr_alignment) < (u64)64 * 1024);
        m_addr_nodes_cnt = (s32)(m_memory_size / m_addr_alignment);
        m_addr_nodes     = (u32*)m_main_heap->allocate(m_addr_nodes_cnt * sizeof(u32), sizeof(void*));
        for (u32 i = 0; i < m_addr_nodes_cnt; i++)
        {
            m_addr_nodes[i] = naddr_t::NIL;
        }

        naddr_t* head_node = m_node_heap->construct<naddr_t>();
        naddr_t* tail_node = m_node_heap->construct<naddr_t>();
        head_node->init();
        head_node->set_locked();
        tail_node->init();
        head_node->set_locked();
        naddr_t*  main_node  = m_node_heap->construct<naddr_t>();
        u32 const main_inode = m_node_heap->ptr2idx(main_node);
        main_node->init();
        main_node->m_addr      = 0;
        main_node->m_size      = (u32)(mem_size / m_alloc_size_min);
        main_node->m_prev_addr = m_node_heap->ptr2idx(head_node);
        main_node->m_next_addr = m_node_heap->ptr2idx(tail_node);
        head_node->m_next_addr = main_inode;
        tail_node->m_prev_addr = main_inode;
        add_to_size_db(main_inode, main_node);
    }

    void xcoalescee::release()
    {
        // TODO: Iterate over the size_db and addr_db releasing all the allocated nodes

        m_main_heap->deallocate(m_size_nodes);
        m_main_heap->deallocate(m_addr_nodes);
        m_size_nodes_occupancy.release(m_main_heap);
    }

    void* xcoalescee::allocate(u32 size, u32 alignment)
    {
        u32      inode = naddr_t::NIL;
        naddr_t* pnode = nullptr;
        if (!pop_from_size_db(size, inode, pnode))
        {
            // Could not find a size to fullfill the request
            return nullptr;
        }
        // See if we have a split-off part, if so insert an addr node
        //   after the current that is marked as 'Free' and also add it
        //   to the size DB
        if (can_split_node(size, alignment, inode, pnode))
        {
            u32      rinode;
            naddr_t* rpnode;
            split_node(size, alignment, inode, pnode, rinode, rpnode);
            add_to_size_db(rinode, rpnode);
        }

        // Add it to the address-db so that we can find it when deallocating
        add_to_addr_db(inode, pnode);

        // Mark the current addr node as 'used'
        pnode->set_used(true);

        // Return the address
        return pnode->get_addr(m_memory_addr, m_alloc_size_min);
    }

    void xcoalescee::deallocate(void* p)
    {
        u32      inode = naddr_t::NIL;
        naddr_t* pnode = nullptr;
        if (!pop_from_addr_db(p, inode, pnode))
        {
            // Could not find address in the addr_db
            return;
        }

        // Coalesce:
        //   Determine the 'prev' and 'next' of the current addr node
        //   If 'prev' is marked as 'Free' then coalesce (merge) with it
        //     and remove the size node that belonged to 'prev' from the size DB
        //   If 'next' is marked as 'Free' then coalesce (merge) with it
        //     and remove the size node that belonged to 'next' from the size DB
        //   Build the size node with the correct size and reference the 'merged' addr node
        //   Add the size node to the size DB
        //   Done

        u32      inode_prev = pnode->m_prev_addr;
        u32      inode_next = pnode->m_next_addr;
        naddr_t* pnode_prev = idx2naddr(inode_prev);
        naddr_t* pnode_next = idx2naddr(inode_next);

        if (pnode_prev->is_free() && pnode_next->is_free())
        {
            // prev and next are marked as 'free'.
            // - remove size of prev from size DB
            // - increase prev size with current and next size
            // - remove size of current and next from size DB
            // - remove from addr DB
            // - remove current and next addr from physical addr list
            // - add prev size back to size DB
            // - deallocate current and next
            remove_from_size_db(inode_prev, pnode_prev);
            remove_from_size_db(inode_next, pnode_next);
            pnode_prev->m_size += pnode->m_size + pnode_next->m_size;
            add_to_size_db(inode_prev, pnode_prev);
            remove_from_addr(inode, pnode);
            remove_from_addr(inode_next, pnode_next);
            dealloc_node(inode, pnode);
            dealloc_node(inode_next, pnode_next);
        }
        else if (!pnode_prev->is_free() && pnode_next->is_free())
        {
            // next is marked as 'free' (prev is 'used')
            // - remove next from size DB and physical addr list
            // - deallocate 'next'
            // - add the size of 'next' to 'current'
            // - add size to size DB
            remove_from_size_db(inode_next, pnode_next);
            pnode->m_size += pnode_next->m_size;
            add_to_size_db(inode, pnode);
            remove_from_addr(inode_next, pnode_next);
            dealloc_node(inode_next, pnode_next);
        }
        else if (pnode_prev->is_free() && !pnode_next->is_free())
        {
            // prev is marked as 'free'. (next is 'used')
            // - remove this addr/size node
            // - rem/add size node of 'prev', adding the size of 'current'
            // - deallocate 'current'
            remove_from_size_db(inode_prev, pnode_prev);
            pnode_prev->m_size += pnode->m_size;
            add_to_size_db(inode_prev, pnode_prev);
            remove_from_addr(inode, pnode);
            dealloc_node(inode, pnode);
        }
        else if (!pnode_prev->is_free() && !pnode_next->is_free())
        {
            // prev and next are marked as 'used'.
            // - add current to size DB
            add_to_size_db(inode, pnode);
        }
    }

    void xcoalescee::remove_from_addr(u32 idx, naddr_t* pnode)
    {
        naddr_t* pnode_prev     = idx2naddr(pnode->m_prev_addr);
        naddr_t* pnode_next     = idx2naddr(pnode->m_next_addr);
        pnode_prev->m_next_addr = pnode->m_next_addr;
        pnode_next->m_prev_addr = pnode->m_prev_addr;
    }

    void xcoalescee::add_to_addr_db(u32 inode, naddr_t* pnode)
    {
        void*     addr  = pnode->get_addr(m_memory_addr, m_alloc_size_min);
        u32 const slot  = calc_addr_slot(addr);
        u32 const ihead = m_addr_nodes[slot];
        if (ihead == naddr_t::NIL)
        {
            m_addr_nodes[slot] = inode;
            pnode->m_next_db   = inode;
            pnode->m_prev_db   = inode;
        }
        else
        {
            naddr_t* phead     = idx2naddr(ihead);
            naddr_t* pprev     = idx2naddr(phead->m_prev_db);
            pnode->m_prev_db   = phead->m_prev_db;
            phead->m_prev_db   = inode;
            pprev->m_next_db   = inode;
            pnode->m_next_db   = ihead;
            m_addr_nodes[slot] = inode;
        }
    }

    bool xcoalescee::pop_from_addr_db(void* ptr, u32& inode, naddr_t*& pnode)
    {
        // Calculate the slot in the addr DB
        // Iterate through the list at that slot until we find 'p'
        // Remove the item from the list
        u32 const addr_slot = calc_addr_slot(ptr);
        u32*      pihead    = &m_addr_nodes[addr_slot];
        pnode               = idx2naddr(*pihead);
        while (pnode != nullptr)
        {
            if (ptr == pnode->get_addr(m_memory_addr, m_alloc_size_step))
            {
                // Now we have the addr node pointer and index
                *pihead = pnode->m_next_db;
                return true;
            }
            pihead = &pnode->m_next_db;
            pnode  = idx2naddr(*pihead);
        }
        return false;
    }

    bool xcoalescee::can_split_node(u32 size, u32 alignment, u32 inode, naddr_t* pnode) { return ((pnode->m_size - size) >= m_alloc_size_step); }

    void xcoalescee::split_node(u32 size, u32 alignment, u32 inode, naddr_t* pnode, u32& rinode, naddr_t*& rpnode)
    {
        u32 alloc_size = size;

        // TODO: Compute alloc size including alignment

        // Construct new naddr node and link it into the physical address doubly linked list
        rpnode = m_node_heap->construct<naddr_t>();
        rinode = m_node_heap->ptr2idx(rpnode);
        rpnode->init();
        rpnode->m_size          = pnode->m_size - alloc_size;
        rpnode->m_flags         = pnode->m_flags;
        rpnode->m_prev_addr     = inode;
        rpnode->m_next_addr     = pnode->m_next_addr;
        pnode->m_next_addr      = rinode;
        naddr_t* pnode_next     = idx2naddr(pnode->m_next_addr);
        pnode_next->m_prev_addr = rinode;
        void* node_addr         = pnode->get_addr(m_memory_addr, m_memory_size);
        void* rest_addr         = addr_add(node_addr, alloc_size);
        rpnode->set_addr(m_memory_addr, m_memory_size, rest_addr);
    }

    bool xcoalescee::pop_from_size_db(u32 _size, u32& inode, naddr_t*& pnode)
    {
        // TODO: Alignment calculation by inflating the size

        // Find 'good-fit' [size, alignment]
        u32 size = _size;
        if (size < m_alloc_size_min)
            size = m_alloc_size_min;
        size     = (size + (m_alloc_size_step - 1)) & ~(m_alloc_size_step - 1);
        u32 slot = calc_size_slot(size);
        if (!m_size_nodes_occupancy.is_set(slot))
        {
            u32 larger_slot;
            if (!m_size_nodes_occupancy.upper(slot, larger_slot))
            {
                // There are no free min/max sized blocks left in the db.
                // See if we have large free blocks.
                if (m_size_nodes_large != naddr_t::NIL)
                {
                    inode              = m_size_nodes_large;
                    pnode              = (naddr_t*)m_node_heap->idx2ptr(inode);
                    m_size_nodes_large = pnode->m_next_db;
                    if (m_size_nodes_large == inode)
                    {
                        m_size_nodes_large = naddr_t::NIL;
                    }

                    // Remove list pointers
                    pnode->m_next_db = naddr_t::NIL;
                    pnode->m_prev_db = naddr_t::NIL;

                    return true;
                }

                // Also no large blocks left, so we are unable to allocate
                return false;
            }
            slot = larger_slot;
        }
        size = m_alloc_size_min + (slot * m_alloc_size_step);

        inode              = m_size_nodes[slot];
        pnode              = (naddr_t*)m_node_heap->idx2ptr(inode);
        m_size_nodes[slot] = pnode->m_next_db;
        if (m_size_nodes[slot] == naddr_t::NIL)
        {
            m_size_nodes_occupancy.clr(slot);
        }

        return true;
    }

    void xcoalescee::add_to_size_db(u32 inode, naddr_t* pnode)
    {
        u32 const size = pnode->m_size;
        if (size > (m_alloc_size_max + m_alloc_size_min))
        {
            // These are the larger free blocks
            // TODO: Sort them in a rough way (by adress?)
            u32 const ihead    = m_size_nodes_large;
            naddr_t*  phead    = idx2naddr(ihead);
            naddr_t*  pprev    = idx2naddr(phead->m_prev_db);
            pnode->m_prev_db   = phead->m_prev_db;
            phead->m_prev_db   = inode;
            pprev->m_next_db   = inode;
            pnode->m_next_db   = ihead;
            m_size_nodes_large = inode;
        }
        else
        {
            u32 const slot     = calc_size_slot(size);
            u32 const ihead    = m_size_nodes[slot];
            naddr_t*  phead    = idx2naddr(ihead);
            naddr_t*  pprev    = idx2naddr(phead->m_prev_db);
            pnode->m_prev_db   = phead->m_prev_db;
            phead->m_prev_db   = inode;
            pprev->m_next_db   = inode;
            pnode->m_next_db   = ihead;
            bool const first   = m_size_nodes[slot] == naddr_t::NIL;
            m_size_nodes[slot] = inode;
            if (first)
            {
                m_size_nodes_occupancy.set(slot);
            }
        }
    }

    class xvmem_allocator_coalesce : public xalloc
    {
    public:
        xvmem_allocator_coalesce()
            : m_internal_heap(nullptr)
            , m_node_alloc(nullptr)
        {
        }

        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);
        virtual void  release();

        xalloc*    m_internal_heap;
        xfsadexed* m_node_alloc; // For allocating naddr_t and nsize_t nodes
        xvmem*     m_vmem;
        xcoalescee m_coalescee;

		XCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    void* xvmem_allocator_coalesce::allocate(u32 size, u32 alignment) { return nullptr; }

    void xvmem_allocator_coalesce::deallocate(void* p) {}

    void xvmem_allocator_coalesce::release()
    {
        m_coalescee.release();

        // release virtual memory

        m_internal_heap->destruct<>(this);
    }

    xalloc* gCreateVMemCoalesceAllocator(xalloc* internal_heap, xfsadexed* node_alloc, xvmem* vmem, u64 mem_size, u32 alloc_size_min, u32 alloc_size_max, u32 alloc_size_step, u32 alloc_addr_list_size)
    {
        xvmem_allocator_coalesce* allocator = internal_heap->construct<xvmem_allocator_coalesce>();
        allocator->m_internal_heap          = internal_heap;
        allocator->m_node_alloc             = node_alloc;
        allocator->m_vmem                   = vmem;

        void* memory_addr = nullptr;
        u32   page_size;
        u32   attr = 0;
        vmem->reserve(mem_size, page_size, attr, memory_addr);

        allocator->m_coalescee.initialize(internal_heap, node_alloc, memory_addr, mem_size, alloc_size_min, alloc_size_max, alloc_size_step, alloc_addr_list_size);

        return allocator;
    }

}; // namespace xcore
