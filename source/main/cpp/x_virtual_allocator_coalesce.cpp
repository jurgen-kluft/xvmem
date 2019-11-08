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
        u32 m_addr;  // [Allocated, Free, Locked] + (m_addr * size step) + base addr
        u32 m_nsize; // size node index (for coalesce)
        u32 m_prev;  // previous node in memory, can be free, can be allocated
        u32 m_next;  // next node in memory, can be free, can be allocated

        void init()
        {
            m_addr  = 0;
            m_nsize = 0xffffffff;
            m_prev  = 0xffffffff;
            m_next  = 0xffffffff;
        }

        void set_used(bool used) { m_addr = (m_addr & 0x7fffffff) | (used ? 0x80000000 : 0); }
        bool is_free() const { return (m_addr & 0x80000000) == 0; }
        bool is_locked() const { return m_next == 0xfffffffe || m_prev == 0xfffffffe; }
    };

    struct nsize_t
    {
        u32 m_size;  // cached size
        u32 m_naddr; // the address node that has this size
        u32 m_prev;  // either in the allocation slot array as a list node
        u32 m_next;  // or in the size slot array as a list node
    };

    class xcoalescee : public xalloc
    {
    public:
        xcoalescee();

        void          initialize(xalloc* main_heap, xfsadexed* node_heap, void* mem_addr, u64 mem_size, u32 size_min, u32 size_max, u32 size_step);
        void          release();
        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);
        inline u32    calc_size_slot(u32 size) const { u32 const slot = (size - m_alloc_size_min) / m_alloc_size_step; ASSERT(slot < m_size_nodes_cnt); return slot; }
        inline u32    calc_addr_slot(void* addr) const { u32 const slot = ((u64)addr - (u64)m_memory_addr) / m_addr_alignment; ASSERT(slot < m_addr_nodes_cnt); return slot; } 
        xalloc*       m_main_heap;
        xfsadexed*    m_node_heap;
        void*         m_memory_addr;
        u64           m_memory_size;
        u32           m_alloc_size_min;
        u32           m_alloc_size_max;
        u32           m_alloc_size_step;
        u32           m_size_nodes_cnt;
        u32*          m_size_nodes;
        xhibitset     m_size_nodes_occupancy;
		u64           m_addr_alignment;
        u32           m_addr_nodes_cnt;
        u32*          m_addr_nodes;
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
        , m_addr_nodes(nullptr)
		, m_addr_alignment(0)
    {
    }

    void xcoalescee::initialize(xalloc* main_heap, xfsadexed* node_heap, void* mem_addr, u64 mem_size, u32 size_min, u32 size_max, u32 size_step)
    {
        m_main_heap       = main_heap;
        m_node_heap       = node_heap;
        m_memory_addr     = mem_addr;
        m_memory_size     = mem_size;
        m_alloc_size_min  = size_min;
        m_alloc_size_max  = size_max;
        m_alloc_size_step = size_step;
		m_addr_alignment  = size_min * 16;

        m_size_nodes_cnt    = (m_alloc_size_max - m_alloc_size_min) / m_alloc_size_step;
        m_size_nodes        = (u32*)m_main_heap->allocate(num_sizes * sizeof(u32), sizeof(void*));
        x_memset(m_size_nodes, 0, num_sizes * sizeof(u32));

        // Which sizes are available in 'm_size_nodes' is known through
        // this hierarchical set of bits.
        m_size_nodes_occupancy.init(m_main_heap, num_sizes, xhibitset::FIND_1);

        // Please keep the number of addr slots low
        ASSERT((m_memory_size / m_addr_alignment) < (u64)64 * 1024);
        m_addr_nodes_cnt = (s32)(m_memory_size / m_addr_alignment);
        m_addr_nodes     = (u32*)m_main_heap->allocate(num_addrs * sizeof(u32), sizeof(void*));
        x_memset(m_addr_nodes, 0, num_addrs * sizeof(u32));
    }

    void xcoalescee::release()
    {
        m_main_heap->deallocate(m_size_nodes);
        m_main_heap->deallocate(m_addr_nodes);
        m_size_nodes_occupancy.release(m_main_heap);
    }

    void* xcoalescee::allocate(u32 _size, u32 _alignment)
    {
        // TODO: Alignment calculation by inflating the size

        // Find 'good-fit' [size, alignment]
        u32 size = _size;
        if (size < m_alloc_size_min)
            size = m_alloc_size_min;
        size          = (size + (m_alloc_size_step - 1)) & ~(m_alloc_size_step - 1);
        u32 size_slot = calc_size_slot(size);
        if (!m_size_nodes_occupancy.is_set(size_slot))
        {
            u32 larger_size_slot;
            if (!m_size_nodes_occupancy.upper(size_slot, larger_size_slot))
            {
                // There are no free blocks left in this allocator to satisfy
                // the request.
                return nullptr;
            }
        }
        size = m_alloc_size_min + (size_slot * m_alloc_size_step);

        u32      nsize_idx      = m_size_nodes[size_slot];
        nsize_t* nsize          = (nsize_t*)m_node_heap->idx2ptr(nsize_idx);
        m_size_nodes[size_slot] = nsize->m_next;

        // Determine addr node
        // See if we have a split-off part, if so insert an addr node
        //   after the current that is marked as 'Free' and also add it
        //   to the size DB
        naddr_t* naddr = (naddr_t*)m_node_heap->idx2ptr(nsize->m_naddr);
        if ((size - _size) > m_alloc_size_step)
        {
            // We can seperate this region
            u32 rest = size - _size;
            // TODO: Deal with rest
            // - Insert new addr node
            // - Insert new size node
        }

        // Add the current addr node in the addr DB
		void* addr = m_memory_addr + ((u64)naddr->m_addr * m_alloc_size_min);
		u32 const addr_slot = calc_addr_slot(addr);

		u32 naddr_head = m_addr_nodes[addr_slot];
		// Every addr nodes slot uses a nsize_t node
		// The naddr_t nodes are linked together representing memory seperation
		nsize->m_prev = 0xffffffff;
		nsize->m_next = naddr_head;
		m_addr_nodes[addr_slot] = nsize_idx;

        // Mark the current addr node as 'used'
        naddr->set_used(true);

        // Return the address
        return addr;
    }

    void xcoalescee::deallocate(void* p)
    {
        // Calculate the slot in the addr DB
        // Iterate through the list at that slot until we find 'p'
        // Remove the item from the list
        u32 const addr_slot = calc_addr_slot(p);

        // Determine the 'prev' and 'next' of the current addr node
        // If 'prev' is marked as 'Free' then coalesce (merge) with it
        // Remove the size node that belonged to 'prev' from the size DB
        // If 'next' is marked as 'Free' then coalesce (merge) with it
        // Remove the size node that belonged to 'next' from the size DB
        // Build the size node with the correct size and reference the 'merged' addr node
        // Add the size node to the size DB
        // Done
    }
    class xvmem_allocator_coalesce : public xalloc
    {
    public:
        xvmem_allocator_coalesce()
            : m_internal_heap(nullptr)
            , m_node_alloc(nullptr)
            , m_memory_addr(nullptr)
            , m_memory_size(0)
            , m_alloc_size_min(0)
            , m_alloc_size_max(0)
            , m_alloc_size_step(0)
            , m_size_nodes(nullptr)
            , m_size_nodes_occupancy(nullptr)
            , m_addr_nodes(nullptr)
        {
        }

        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);
        virtual void  release();

        xalloc*    m_internal_heap;
        xfsadexed* m_node_alloc; // For allocating naddr_t and nsize_t nodes

        void* m_memory_addr;
        u64   m_memory_size;
        u32   m_alloc_size_min;
        u32   m_alloc_size_max;
        u32   m_alloc_size_step;
        u32*  m_size_nodes;
        u32*  m_size_nodes_occupancy;
        u32*  m_addr_nodes;
    };

    void* xvmem_allocator_coalesce::allocate(u32 size, u32 alignment) { return nullptr; }

    void xvmem_allocator_coalesce::deallocate(void* p) {}

    void xvmem_allocator_coalesce::release() {}

    xalloc* gCreateVMemCoalesceAllocator(xalloc* internal_heap, xfsadexed* node_alloc, xvmem* vmem, u64 mem_size, u32 alloc_size_min, u32 alloc_size_max, u32 alloc_size_step, u32 alloc_addr_list_size) { return nullptr; }

}; // namespace xcore
