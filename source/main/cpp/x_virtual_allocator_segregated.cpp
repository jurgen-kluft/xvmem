#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/x_virtual_allocator.h"
#include "xvmem/x_virtual_pages.h"
#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_bst.h"

namespace xcore
{
    struct nblock_t : public xbst::index_based::node_t
    {
        static u32 const NIL            = 0xffffffff;
        static u32 const FLAG_COLOR_RED = 0x10000000;
        static u32 const FLAG_FREE      = 0x20000000;
        static u32 const FLAG_USED      = 0x40000000;
        static u32 const FLAG_LOCKED    = 0x80000000;
        static u32 const FLAG_MASK      = 0xF0000000;

        u32 m_addr;      // addr = base_addr(m_addr * size_step)
        u16 m_flags;     // [Allocated, Free, Locked, Color]
		u16 m_level_idx; // Level index (size)
        u32 m_page_cnt;  // Number of pages committed
        u32 m_prev_addr; // previous node in memory, can be free, can be allocated
        u32 m_next_addr; // next node in memory, can be free, can be allocated

        void init()
        {
            clear();
            m_addr      = 0;
            m_flags     = 0;
			m_level_idx = 0;
            m_page_cnt  = 0;
            m_prev_addr = NIL;
            m_next_addr = NIL;
        }

        inline void* get_addr(void* baseaddr, u64 size_step) const { return (void*)((u64)baseaddr + ((u64)m_addr * size_step)); }
        inline void  set_addr(void* baseaddr, u64 size_step, void* addr) { m_addr = (u32)(((u64)addr - (u64)baseaddr) / size_step); }
		inline void  set_level_idx(u16 level_index) { m_level_idx = level_index; }
		inline u32   get_level_idx() const { return (u32)m_level_idx; }
        inline u64   get_page_cnt(u64 page_size) const { return (u64)m_page_cnt * page_size; }
        inline void  set_page_cnt(u64 size, u32 page_size) { m_page_cnt = (u32)(size / page_size); }

        inline void set_locked() { m_flags = m_flags | FLAG_LOCKED; }
        inline void set_used(bool used) { m_flags = m_flags | FLAG_USED; }
        inline bool is_free() const { return (m_flags & FLAG_MASK) == FLAG_FREE; }
        inline bool is_locked() const { return (m_flags & FLAG_MASK) == FLAG_LOCKED; }

        inline void set_color_red() { m_flags = m_flags | FLAG_COLOR_RED; }
        inline void set_color_black() { m_flags = m_flags & ~FLAG_COLOR_RED; }
        inline bool is_color_red() const { return (m_flags & FLAG_COLOR_RED) == FLAG_COLOR_RED; }
        inline bool is_color_black() const { return (m_flags & FLAG_COLOR_RED) == 0; }

        XCORE_CLASS_PLACEMENT_NEW_DELETE
    };


    struct xvmem_range_t
    {
        struct range_t
        {
            void reset() { m_num_allocs = 0; m_next = 0xffff; m_prev = 0xffff; }
            bool is_unused() const { return m_num_allocs == 0; }
            void register_alloc() { m_num_allocs += 1; }
            void register_dealloc() { m_num_allocs -= 1; }

            u16 m_num_allocs;
            u16 m_next;
            u16 m_prev;
        };

        void*    m_mem_address;
        u64      m_sub_range;
        u32      m_sub_max;
        u16      m_sub_freelist;
        range_t* m_sub_ranges;

        void    init(xalloc* main_heap, void* mem_address, u64 mem_range, u64 sub_range)
        {
            m_mem_address = mem_address;
            m_sub_range = sub_range;
            m_sub_max = mem_range / sub_range;
            m_sub_ranges = (range_t*)main_heap->allocate(sizeof(u16) * m_sub_max, sizeof(void*));
            for (s32 i=0; i<m_sub_max; i++)
            {
                m_sub_ranges[i].reset();
            }
            for (s32 i=1; i<m_sub_max; i++)
            {
                u16 icurr = i-1;
                u16 inext = i;
                range_t* pcurr = &m_sub_ranges[icurr];
                range_t* pnext = &m_sub_ranges[inext];
                pcurr->m_next = inext;
                pnext->m_prev = icurr;
            }
        }
            
        void    release(xalloc* main_heap)
        {
            m_mem_address = nullptr;
            m_sub_range = 0;
            m_sub_max = 0;
            main_heap->deallocate(m_sub_ranges);
        }
            
        bool    is_range_empty(void* addr) const
        {
            u32 const idx = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_sub_range);
            return m_sub_ranges[idx].is_unused();
        }

        bool    obtain_range(void*& addr)
        {
			// Do we still have some sub-ranges free ?
            if (m_sub_freelist != 0xffff)
            {
                u32 item = m_sub_freelist;
                range_t* pcurr = &m_sub_ranges[m_sub_freelist];
                m_sub_freelist = pcurr->m_next;
                pcurr->m_next = 0xffff;
                pcurr->m_prev = 0xffff;
                addr = (void*)((u64)m_mem_address + (m_sub_range * item));
                return true;
            }
            return false;
        }
        void    release_range(void*& addr)
        {
			// Put this sub-range back into the free-list !
            u32 const idx = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_sub_range);
            range_t* pcurr = &m_sub_ranges[m_sub_freelist];
            pcurr->m_prev = idx;
            range_t* pitem = &m_sub_ranges[idx];
            pitem->m_next = m_sub_freelist;
            m_sub_freelist = idx;
        }
		// We register allocations and de-allocations on every sub-range to know if 
		// a sub-range is used. We are interested to know when a sub-range is free 
		// so that we can release it back.
        void    register_alloc(void* addr)
        {
            u32 const idx = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_sub_range);
            m_sub_ranges[idx].register_alloc();
        }
        void    register_dealloc(void* addr)
        {
            u32 const idx = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_sub_range);
            m_sub_ranges[idx].register_dealloc();
        }
    };

    // Segregated Allocator uses BST's for free blocks and allocated blocks.
	// Every level starts of with one free block that covers the memory range of that level.
	// So we can split nodes and we also coalesce.

    class xvmem_allocator_segregated : public xalloc
    {
    public:
        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);
        virtual void  release();

        struct level_t
        {
            inline  level_t() : m_mem_address(nullptr), m_mem_range(0), m_free_bst(nblock_t::NIL) {}
            void*   m_mem_address;
			u64     m_mem_range;
			u32     m_free_bst;
        };

		void	initialize(xalloc* main_alloc, xfsadexed* node_alloc, xvmem* vmem, u64 vmem_range, u64 level_range, u32 allocsize_min, u32 allocsize_max, u32 allocsize_step, u32 pagesize);
		void*	allocate_from_level(u32 level_index, u32 size, u32 alignment)
		{
			// If 'free_bst' == NIL, retrieve an additional range from vrange_manager and add it to free_bst
			ASSERT(level_index < m_level_cnt);
			level_t* level = &m_level[level_index];
			if (level->m_free_bst == nblock_t::NIL)
			{
				void* range_base_addr;
				if (!m_vrange_manager.obtain_range(range_base_addr))
				{
					return nullptr;
				}

				// Create nblock_t and add it
				nblock_t* pnode = (nblock_t*)m_node_alloc->allocate();
				u32 inode = m_node_alloc->ptr2idx(pnode);
				pnode->init();
				pnode->set_addr(m_vmem_base_addr, m_allocsize_step, range_base_addr);
				pnode->set_page_cnt(m_vrange_manager.m_sub_range, m_pagesize);
				xbst::index_based::insert(level->m_free_bst, &m_free_bst_config, (void*)m_vrange_manager.m_sub_range, inode);
			}

			// Take a node from the 'free' BST
			u32 inode;
			xbst::index_based::find(level->m_free_bst, &m_free_bst_config, (void*)size, inode);
			nblock_t* pnode = (nblock_t*)m_node_alloc->idx2ptr(inode);

			// If necessary, split the node
		}
		void	deallocate_from_level(u32 iblk, nblock_t* pblk, void* ptr)
		{
			// If 'free_bst' == NIL, retrieve an additional range from vrange_manager and add it to free_bst
			// Take a node from the 'free' BST
			// If necessary, split the node
		}

        xalloc*       m_main_alloc;
        xfsadexed*    m_node_alloc;
        xvmem*        m_vmem;
		void*         m_vmem_base_addr;
        u32           m_allocsize_min;
        u32           m_allocsize_max;
        u32           m_allocsize_step;
        u32           m_pagesize;
        u32           m_level_cnt;
        level_t*      m_level;
		xbst::index_based::tree_t m_free_bst_config;
		xbst::index_based::tree_t m_allocated_bst_config;
		u32			  m_allocated_bst;
		xvmem_range_t m_vrange_manager;
    };

    void  xvmem_allocator_segregated::initialize(xalloc* main_alloc, xfsadexed* node_alloc, xvmem* vmem, u64 vmem_range, u64 level_range, u32 allocsize_min, u32 allocsize_max, u32 allocsize_step, u32 pagesize)
    {
        m_main_alloc = main_alloc;
        m_node_alloc = node_alloc;
        m_vmem = vmem;
		m_vmem_base_addr = nullptr;

        m_allocsize_min = allocsize_min;
        m_allocsize_max = allocsize_max;
        m_allocsize_step = allocsize_step;
        m_pagesize = pagesize;
        m_level_cnt = (m_allocsize_max - m_allocsize_min) / m_allocsize_step;
        m_level = (level_t*)m_main_alloc->allocate(sizeof(level_t) * m_level_cnt, sizeof(void*));
        for (s32 i=0; i<m_level_cnt; ++i)
        {
            m_level[i] = level_t();
        }

		m_allocated_bst = nblock_t::NIL;

		// Reserve the address space
		m_vmem_base_addr = nullptr;

		// Initialize the vmem address range manager
		m_vrange_manager.init(main_alloc, m_vmem_base_addr, vmem_range, level_range);
    }

    void* xvmem_allocator_segregated::allocate(u32 size, u32 alignment)
    {
        ASSERT(size >= m_allocsize_min && size < m_allocsize_max);
        ASSERT(alignment <= m_pagesize);
        u32 aligned_size = (size + m_allocsize_step - 1) & (~m_allocsize_step - 1);
        u32 level_index = (aligned_size - m_allocsize_min) / m_level_cnt;
		return allocate_from_level(level_index, aligned_size, alignment);
    }

    void  xvmem_allocator_segregated::deallocate(void* p)
    {
		// Find nblock_t in the allocated BST
        u32 const addr = (u32)(((uptr)p - (uptr)m_vmem_base_addr) / (uptr)m_allocsize_step);
		u32 iblk;
		if (xbst::index_based::find(m_allocated_bst, &m_allocated_bst_config, (void*)addr, iblk))
		{
			nblock_t* pblk = (nblock_t*)m_node_alloc->idx2ptr(iblk);
			deallocate_from_level(iblk, pblk, p);
		}
    }

    void  xvmem_allocator_segregated::release()
    {

    }

}; // namespace xcore
