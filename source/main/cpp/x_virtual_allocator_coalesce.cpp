#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"
#include "xbase/x_hibitset.h"

#include "xvmem/x_virtual_allocator.h"
#include "xvmem/x_virtual_pages.h"
#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_bst.h"

namespace xcore
{
	using namespace xbst::index_based;

    struct naddr_t : public node_t
    {
        static u32 const NIL            = 0xffffffff;
        static u32 const FLAG_COLOR_RED = 0x10000000;
        static u32 const FLAG_FREE      = 0x20000000;
        static u32 const FLAG_USED      = 0x40000000;
        static u32 const FLAG_LOCKED    = 0x80000000;
        static u32 const FLAG_MASK      = 0xF0000000;

        u32    m_addr;      // addr = base_addr(m_addr * size_step)
		u32    m_flags;     // [Allocated, Free, Locked, Color]
        u32    m_size;      // [m_size = (m_size & ~FLAG_MASK) * size_step]
        u32    m_prev_addr; // previous node in memory, can be free, can be allocated
        u32    m_next_addr; // next node in memory, can be free, can be allocated

        void init()
        {
            clear();
            m_addr      = 0;
			m_flags     = 0;
            m_size      = 0;
            m_prev_addr = naddr_t::NIL;
            m_next_addr = naddr_t::NIL;
        }

        inline void* get_addr(void* baseaddr, u64 size_step) const { return (void*)((u64)baseaddr + ((u64)m_addr * size_step)); }
        inline void  set_addr(void* baseaddr, u64 size_step, void* addr) { m_addr = (u32)(((u64)addr - (u64)baseaddr) / size_step); }
		inline u32   get_size(u64 size_step) const { return (u64)m_size * size_step; }
		inline void  set_size(u64 size, u64 size_step) { m_size = (u32)(size / size_step); }

        inline void  set_locked() { m_flags = m_flags | FLAG_LOCKED; }
        inline void  set_used(bool used) { m_flags = m_flags | FLAG_USED; }
        inline bool  is_free() const { return (m_flags & FLAG_MASK) == FLAG_FREE; }
        inline bool  is_locked() const { return (m_flags & FLAG_MASK) == FLAG_LOCKED; }

		inline void  set_color_red() { m_flags = m_flags | FLAG_COLOR_RED; }
		inline void  set_color_black() { m_flags = m_flags & ~FLAG_COLOR_RED; }
		inline bool  is_color_red() const { return (m_flags & FLAG_COLOR_RED) == FLAG_COLOR_RED; }
		inline bool  is_color_black() const { return (m_flags & FLAG_COLOR_RED) == 0; }

		XCORE_CLASS_PLACEMENT_NEW_DELETE
    };

	static inline void* advance_ptr(void* ptr, u64 size)
	{
		return (void*)((uptr)ptr + size);
	}
	static inline void* align_ptr(void* ptr, u32 alignment)
	{
		return (void* )(((uptr)ptr + (alignment - 1)) & ~((uptr)alignment - 1));
	}
	static uptr diff_ptr(void* ptr, void* next_ptr)
	{
		return (size_t)((uptr)next_ptr - (uptr)ptr);
	}

	static u64 adjust_size_for_alignment(u64 requested_size, u32 requested_alignment, u32 default_alignment)
	{
		// Compute the 'size' that taking into account the alignment. This is done by taking
		// the 'external mem ptr' of the current node and the 'external mem ptr'
		// of the next node and substracting them.
		ASSERT(xispo2(default_alignment));
		ASSERT(xispo2(requested_alignment));

		u64 out_size = 0;

		// Verify the alignment
		if (requested_alignment > default_alignment)
		{
			u64 align_mask = requested_alignment - 1;
			u64 align_shift = (default_alignment & align_mask);

			// How many bytes do we have to add to reach the requested size & alignment?
			align_shift = requested_alignment - align_shift;
			out_size = requested_size + align_shift;
		}
		else
		{
			// The default alignment is already enough to satisfy the requested alignment
			out_size = requested_size;
		}
		return out_size;
	}

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

        void        split_node(u32 inode, naddr_t* pnode);
		bool		find_bestfit(u64 size, u32 alignment, naddr_t *& out_pnode, u32& out_inode, u32 &out_nodeSize);

		void        add_to_size_db(u32 inode, naddr_t* pnode);
        void        remove_from_size_db(u32 inode, naddr_t* pnode);

        inline void alloc_node(u32& inode, naddr_t*& node)
        {
            node = (naddr_t*)m_node_heap->allocate();
			inode = m_node_heap->ptr2idx(node);
        }
        
		inline void dealloc_node(u32 inode, naddr_t* pnode)
        {
            ASSERT(m_node_heap->idx2ptr(inode) == pnode);
            m_node_heap->deallocate(pnode);
        }

        inline naddr_t* idx2naddr(u32 idx)
        {
            naddr_t* pnode = nullptr;
            if (idx != naddr_t::NIL)
                pnode = (naddr_t*)m_node_heap->idx2ptr(idx);
            return pnode;
        }

        inline u32 calc_size_slot(u64 size) const
        {
            ASSERT(size >= m_alloc_size_min);
            u32 const slot = (size - m_alloc_size_min) / m_alloc_size_step;
            ASSERT(slot < m_size_db_cnt);
            return slot;
        }

        xalloc*    m_main_heap;
        xfsadexed* m_node_heap;
        void*      m_memory_addr;
        u64        m_memory_size;
        
		u32        m_alloc_size_min;
        u32        m_alloc_size_max;
        u32        m_alloc_size_step;
		tree_t	   m_size_db_config;
		u32		   m_size_db_cnt;
        u32*       m_size_db;
		xhibitset  m_size_db_occupancy;

		u64        m_addr_alignment;
		tree_t	   m_addr_db_config;
        u32        m_addr_db;
    };

    xcoalescee::xcoalescee()
        : m_main_heap(nullptr)
        , m_node_heap(nullptr)
        , m_memory_addr(nullptr)
        , m_memory_size(0)
        , m_alloc_size_min(0)
        , m_alloc_size_max(0)
        , m_alloc_size_step(0)
		, m_size_db_cnt(0)
        , m_size_db(nullptr)
        , m_addr_alignment(0)
        , m_addr_db(0)
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

        m_size_db_cnt = (m_alloc_size_max - m_alloc_size_min) / m_alloc_size_step;
        m_size_db     = (u32*)m_main_heap->allocate(m_size_db_cnt * sizeof(u32), sizeof(void*));
        for (u32 i = 0; i < m_size_db_cnt; i++)
        {
            m_size_db[i] = naddr_t::NIL;
        }
        // Which sizes are available in 'm_size_db' is known through this hierarchical set of bits.
        m_size_db_occupancy.init(m_main_heap, m_size_db_cnt, xhibitset::FIND_1);

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
		u32 inode = 0;
		while (clear(m_addr_db, &m_addr_db_config, inode))
		{
			naddr_t* pnode = idx2naddr(inode);
			dealloc_node(inode, pnode);
		}
		for (s32 i=0; i<m_size_db_cnt; i++)
		{
			while (clear(m_size_db[i], &m_size_db_config, inode))
			{
				naddr_t* pnode = idx2naddr(inode);
				dealloc_node(inode, pnode);
			}
		}
    }

    void* xcoalescee::allocate(u32 _size, u32 _alignment)
    {
		// Align the size up with 'm_alloc_size_step'
		// Align the alignment up with 'm_alloc_size_step'
		u32 size = xalignUp(size, m_alloc_size_step);
		u32 alignment = xmax(_alignment, m_alloc_size_step);

		// Find the node in the size db that has the same or larger size
		naddr_t * node;
		u32       inode;
		u32       nodeSize;
		if (find_bestfit(size, alignment, node, inode, nodeSize) == false)
			return nullptr;
		ASSERT(node != nullptr);
	
		// Remove 'node' from the size tree since it is not available/free anymore
		remove_from_size_db(inode, node);

		void* ptr = node->get_addr(m_memory_addr, m_alloc_size_step);
		node->set_addr(m_memory_addr, m_alloc_size_step, align_ptr(ptr, alignment));

		//
		split_node(inode, node);

		// Mark our node as used
		node->set_used(true);

		// Insert our alloc node into the address tree so that we can find it when
		// deallocate is called.
		node->clear();
		insert(m_addr_db, &m_addr_db_config, node, inode);

		// Done...
		return node->get_addr(m_memory_addr, m_alloc_size_step);
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
        void* addr  = pnode->get_addr(m_memory_addr, m_alloc_size_min);
		insert(m_addr_db, &m_addr_db_config, addr, inode);
    }

    bool xcoalescee::pop_from_addr_db(void* ptr, u32& inode, naddr_t*& pnode)
    {
		if (find(m_addr_db, &m_addr_db_config, ptr, inode))
		{
			pnode = (naddr_t*)m_addr_db_config.idx2ptr(inode);
			remove(m_addr_db, &m_addr_db_config, ptr, inode);
			return true;
		}
		return false;
    }

	void xcoalescee::add_to_size_db(u32 inode, naddr_t* pnode)
	{
        void* addr  = pnode->get_addr(m_memory_addr, m_alloc_size_min);
		u32 const size = pnode->get_size(m_alloc_size_step);
		u32 const size_db_slot = calc_size_slot(size);
		insert(m_size_db[size_db_slot], &m_size_db_config, addr, inode);
	}

	void xcoalescee::remove_from_size_db(u32 inode, naddr_t* pnode)
	{
        void* addr  = pnode->get_addr(m_memory_addr, m_alloc_size_min);
		u32 const size = pnode->get_size(m_alloc_size_step);
		u32 const size_db_slot = calc_size_slot(size);
		remove(m_size_db[size_db_slot], &m_size_db_config, addr, inode);
	}

    void xcoalescee::split_node(u32 inode, naddr_t* pnode)
    {
        // Construct new naddr node and link it into the physical address doubly linked list
        rpnode = m_node_heap->construct<naddr_t>();
        rinode = m_node_heap->ptr2idx(rpnode);
        rpnode->init();
        rpnode->m_size          = pnode->m_size;
		rpnode->set_size(pnode->get_size(m_alloc_size_step), m_alloc_size_step);
        rpnode->m_prev_addr     = inode;
        rpnode->m_next_addr     = pnode->m_next_addr;
        pnode->m_next_addr      = rinode;
        naddr_t* pnode_next     = idx2naddr(pnode->m_next_addr);
        pnode_next->m_prev_addr = rinode;
        void* node_addr         = pnode->get_addr(m_memory_addr, m_memory_size);
        void* rest_addr         = advance_ptr(node_addr, size);
        rpnode->set_addr(m_memory_addr, m_memory_size, rest_addr);
    }

	bool xcoalescee::find_bestfit(u64 size, u32 alignment, naddr_t *& out_pnode, u32& out_inode, u32 &out_nodeSize)
	{
		if (m_size_db == 0)
			return false;

		// Adjust the size and compute size slot
		// - If the requested alignment is less-or-equal to m_alloc_size_step then we can align_up the size to m_alloc_size_step
		// - If the requested alignment is greater than m_alloc_size_step then we need to calculate the necessary size needed to
		//   include the alignment request
		u32 size_to_alloc = adjust_size_for_alignment(size, alignment, m_alloc_size_step);
		u32 size_db_slot = calc_size_slot(size_to_alloc);
		u32 icur = m_size_db[size_db_slot];
		if (icur == naddr_t::NIL)
		{
			// We need to find a size greater since current slot is empty
		}
		
		// Remove the 'minimum' from the BST

		return false;
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
