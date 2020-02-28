#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"
#include "xbase/x_hibitset.h"

#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_binarysearch_tree.h"
#include "xvmem/private/x_strategy_segregated.h"

namespace xcore
{
	// Two different versions of segregated:
	// 1. Size 64 KB to 1 MB (64,128,256,512,1024)
	// In this size range it is easy to manage the free and used pages by mere bits.
	// struct xmedseg_t
	// {
    //     u32  m_page_bits;  // The bits that track used/free pages
    //     u16  m_alloc_cnt;  // The number of allocations that are active
    //     u16  m_level_idx;  // Which level is using us
	// };
	// struct xseg_t
	// {
	//     u16  m_next;
	//     u16  m_prev;
	// };
	// 
	// 2. Size 1 MB to 32 MB (1,2,4,8,16,32)
	// In the size range it is necessary for every allocation to track the number of
	// pages that are actually committed.
	// struct xbigseg_t
	// {
    //     u32  m_page_bits;  // The bits that track used/free pages
    //     u16  m_alloc_cnt;  // The number of allocations that are active
    //     u16  m_level_idx;  // Which level is using us
	// };

    namespace xsegregatedstrat
    {
        static inline void* advance_ptr(void* ptr, u64 size) { return (void*)((uptr)ptr + size); }

        static u16 const NIL16 = 0xffff;
        static u32 const NIL32 = 0xffffffff;

        struct xspace_t;
        struct xspaces_t
        {
            void*     m_mem_address;
            u64       m_space_size;  // The size of a range (e.g. 1 GiB)
            u32       m_space_count; // The full address range is divided into 'n' spaces
            u32       m_page_size;
            u16       m_freelist; // The doubly linked list spaces that are 'empty'
            xspace_t* m_spaces;   // The array of memory spaces that make up the full address range

            void init(xalloc* main_heap, void* mem_address, u64 mem_range, u64 space_size, u32 page_size);
            void release(xalloc* main_heap);

            bool         is_space_empty(void* addr) const;
            bool         is_space_full(void* addr) const;
            inline void* get_base_address() const { return m_mem_address; }
            inline u32   get_page_size() const { return m_page_size; }
            inline u64   get_space_size() const { return m_space_size; }
            void*        get_space_addr(void* addr) const;

            inline u16 ptr2addr(void* p) const
            {
                u16 const idx = (u16)(((u64)p - (u64)get_space_addr(p)) / get_page_size());
                ASSERT(idx < m_space_count);
                return idx;
            }

            void      insert_space_into_list(u16& head, u16 item);
            xspace_t* remove_space_from_list(u16& head, u16 item);
            bool      obtain_space(void*& addr, u16& ispace, xspace_t*& pspace);
            void      release_space(void*& addr);
            void      release_space(u16 ispace, xspace_t* pspace);
            u16       ptr2level(void* ptr) const;
            u16       ptr2space(void* ptr) const;
            void      register_alloc(void* addr);
            u16       register_dealloc(void* addr);

            XCORE_CLASS_PLACEMENT_NEW_DELETE
        };

		// Note: We can also simplify this by not using a BST but just a single 64-bit integer.
		// This means that the size of a space is kinda fixed: ``basesize * 64``
		// Example:
		//    - minimumsize =  2.5 MB
		//    - maximumsize = 32   MB
		//    - stepsize    =  2   MB
		//  Calculate 
		//    -> basesize  =  2.5 MB aligned down with 2 MB = 2 MB
		//    -> spacesize =  64 * 2 MB = 128 MB
		//
		// Also, the maximumsize has to be smaller that the spacesize, in this case we can fit
		// 4 maximumsize allocations in a single space.
		// 

        struct xspace_t
        {
            void reset()
            {
                m_block_bits = 0;
                m_alloc_cnt = 0;
                m_level_idx = NIL16;
                m_next      = NIL16;
                m_prev      = NIL16;
            }

            bool is_unused() const { return m_alloc_cnt == 0; }
            bool is_empty() const { return m_alloc_cnt == 0; }
            bool is_full() const { return m_block_bits == (u64)0xffffffffffffffffUL; }
			
			void set_link(u16 p, u16 n) { m_prev = p; m_next = n; }
			bool is_linked(u16 i) const { return (m_prev==i) && (m_next==i); }

            void register_alloc() { m_alloc_cnt += 1; }
            void register_dealloc()
            {
                ASSERT(m_alloc_cnt > 0);
                m_alloc_cnt -= 1;
            }

            void init(void* vmem_base_addr, u64 allocsize_step, void* range_base_addr, u64 space_size, u32 page_size)
            {
				// Find '0' bit and compute address
				// Set bit to '1'
                
            }

			void* allocate(u32 size)
			{
				return nullptr;
			}

            void deallocate(void* ptr, xfsadexed* nodes, xspaces_t* spaces)
            {
                
                
            }

            u32 m_block_bits; // The bits that track used/free blocks
            u16 m_alloc_cnt;  // The number of allocations that are active
            u16 m_level_idx;  // Which level is using us
            u16 m_next;       // Used by level to keep track of spaces that are not full
            u16 m_prev;
        };

        void xspaces_t::init(xalloc* main_heap, void* mem_address, u64 mem_range, u64 space_size, u32 page_size)
        {
            m_mem_address = mem_address;
            m_page_size   = page_size;
            m_space_size  = space_size;
            m_space_count = (u32)(mem_range / space_size);
            m_spaces      = (xspace_t*)main_heap->allocate(sizeof(xspace_t) * m_space_count, sizeof(void*));
            for (u32 i = 0; i < m_space_count; i++)
            {
                m_spaces[i].reset();
            }

			m_spaces[0].set_link(m_space_count-1, 1);
			for (u32 i = 1; i < m_space_count; i++)
            {
				m_spaces[i].set_link(i-1, i+1);
            }
			m_spaces[m_space_count-1].set_link(m_space_count-2, 0);
            m_freelist = 0;
        }

        inline xspace_t* idx2space(xspaces_t* self, u16 idx)
        {
            ASSERT(idx < self->m_space_count);
            return &self->m_spaces[idx];
        }

        void xspaces_t::release(xalloc* main_heap)
        {
            m_mem_address = nullptr;
            m_space_size  = 0;
            m_space_count = 0;
            m_freelist    = NIL16;
            main_heap->deallocate(m_spaces);
        }

        bool xspaces_t::is_space_empty(void* addr) const
        {
            u32 const idx = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            return m_spaces[idx].is_unused();
        }

        bool xspaces_t::is_space_full(void* addr) const
        {
            u32 const idx = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            return m_spaces[idx].is_full();
        }

        void* xspaces_t::get_space_addr(void* addr) const
        {
            u64 const space_idx = (u64)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            ASSERT(space_idx < m_space_count);
            return advance_ptr(m_mem_address, (space_idx * m_space_size));
        }

        bool xspaces_t::obtain_space(void*& addr, u16& ispace, xspace_t*& pspace)
        {
            // Do we still have some free spaces ?
            if (m_freelist != NIL16)
            {
                ispace = m_freelist;
				pspace = idx2space(this, ispace);
                remove_space_from_list(m_freelist, ispace);
                pspace->m_level_idx = NIL16;
                pspace->m_next      = NIL16;
                pspace->m_prev      = NIL16;
                addr                = (void*)((u64)m_mem_address + (m_space_size * ispace));
                return true;
            }
			else
			{
				ispace = NIL16;
				pspace = nullptr;
				return false;
			}
        }

        void xspaces_t::insert_space_into_list(u16& head, u16 item)
        {
            xspace_t* pitem = idx2space(this, item);
			ASSERT(pitem != nullptr);

			if (head == NIL16)
			{
				head = item;
				pitem->set_link(item, item);
				return;
			}

            xspace_t* phead = idx2space(this, head);
			xspace_t* pprev = idx2space(this, phead->m_prev);
            pitem->m_prev   = phead->m_prev;
            pitem->m_next   = head;
            phead->m_prev   = item;
			pprev->m_next   = item;
            head            = item;
        }

        xspace_t* xspaces_t::remove_space_from_list(u16& head, u16 item)
        {
            xspace_t* pitem = idx2space(this, item);
			if (pitem->is_linked(item))
			{
				ASSERT(head == item);
				head = NIL16;
				return pitem;
			}

            xspace_t* pprev = idx2space(this, pitem->m_prev);
            xspace_t* pnext = idx2space(this, pitem->m_next);
            pprev->m_next = pitem->m_next;
            pnext->m_prev = pitem->m_prev;
			head = pitem->m_next;
			pitem->set_link(NIL16, NIL16);
            return pitem;
        }

        void xspaces_t::release_space(void*& addr)
        {
            // Put this space back into the free-list !
            u32 const ispace = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            xspace_t* pspace = idx2space(this, ispace);
            pspace->m_level_idx = NIL16;
            insert_space_into_list(m_freelist, ispace);
        }

        void xspaces_t::release_space(u16 ispace, xspace_t* pspace)
        {
            // Put this space back into the free-list !
            pspace->m_level_idx = NIL16;
            insert_space_into_list(m_freelist, ispace);
        }

        u16 xspaces_t::ptr2level(void* ptr) const
        {
            u16 const ispace = (u16)(((uptr)ptr - (uptr)m_mem_address) / (uptr)m_space_size);
            ASSERT(m_spaces[ispace].m_level_idx != NIL16);
            return m_spaces[ispace].m_level_idx;
        }

        u16 xspaces_t::ptr2space(void* addr) const
        {
            u16 const ispace = (u16)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            return ispace;
        }

        // We register allocations and de-allocations on every space to know if
        // it is stil used. We are interested to know when a space is free
        // so that we can release it back.
        void xspaces_t::register_alloc(void* addr)
        {
            u16 const ispace = (u16)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            m_spaces[ispace].register_alloc();
        }

        // Returns the level index
        u16 xspaces_t::register_dealloc(void* addr)
        {
            u16 const ispace = (u16)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            m_spaces[ispace].register_dealloc();
            return m_spaces[ispace].m_level_idx;
        }

        struct xlevel_t
        {
            void  reset();
            void* allocate(u32 size_in_pages, u32 lvl_size_in_pages, xspaces_t* spaces, void* vmem_base_addr, u32 allocsize_step);
            void  deallocate(void* ptr, xfsadexed* node_alloc, xspaces_t* spaces);
            u16   m_spaces_not_full; // Linked list of spaces that are not full and not empty
        };

        void xlevel_t::reset() { m_spaces_not_full = NIL16; }

        void* xlevel_t::allocate(u32 size_in_pages, u32 lvl_size_in_pages, xspaces_t* spaces, void* vmem_base_addr, u32 allocsize_step)
        {
            u16 const  ispace   = spaces->ptr2space(ptr);
            xspace_t*  pspace   = idx2space(spaces, ispace);

            return ptr;
        }

        void xlevel_t::deallocate(void* ptr, xfsadexed* node_alloc, xspaces_t* spaces)
        {
            u16 const  ispace   = spaces->ptr2space(ptr);
            xspace_t*  pspace   = idx2space(spaces, ispace);
            bool const was_full = pspace->is_full();
            pspace->deallocate(ptr, node_alloc, spaces);
            bool const is_empty = pspace->is_empty();
            if (was_full)
            {
                spaces->insert_space_into_list(m_spaces_not_full, ispace);
            }
            else if (is_empty)
            {
                spaces->remove_space_from_list(m_spaces_not_full, ispace);
                spaces->release_space(ispace, pspace);
            }
        }

        struct xinstance_t
        {
            xalloc*    m_main_alloc;
            xfsadexed* m_node_alloc;
            void*      m_vmem_base_addr;
            u32        m_allocsize_min;
            u32        m_allocsize_max;
            u32        m_allocsize_step;
            u32        m_pagesize;
            u32        m_level_cnt;
            xlevel_t*  m_levels;
            xspaces_t* m_spaces;

            XCORE_CLASS_PLACEMENT_NEW_DELETE
        };

        inline u16 lvl2size_in_pages(xinstance_t* self, u16 ilevel) 
		{
            ASSERT(ilevel < self->m_level_cnt);
			return (((ilevel+1) * self->m_allocsize_step) + (self->m_pagesize - 1)) / self->m_pagesize; 
		}

        inline u16 ptr2lvl(xinstance_t* self, void* ptr)
        {
            u16 const ilevel = self->m_spaces->ptr2level(ptr);
            ASSERT(ilevel < self->m_level_cnt);
            return ilevel;
        }

        inline xlevel_t* idx2lvl(xinstance_t* self, u16 ilevel)
        {
            ASSERT(ilevel < self->m_level_cnt);
            return &self->m_levels[ilevel];
        }

        xinstance_t* create(xalloc* main_alloc, xfsadexed* node_alloc, void* vmem_address, u64 vmem_space, u64 space_size, u32 allocsize_min, u32 allocsize_max, u32 allocsize_step, u32 page_size)
        {
			ASSERT(xispo2(allocsize_step));
			ASSERT(xispo2(page_size));

            // Example: min=640KB, max=32MB, step=1MB
            // Levels -> 1MB, 2MB, 3MB ... 30MB 31MB 32MB, in total 32 levels
			const u32     base_size  = xalignDown(allocsize_min, allocsize_step);
            const u32     num_levels = ((allocsize_max + (allocsize_step - 1) - base_size) / allocsize_step);
            const u32     mem_size   = sizeof(xlevel_t) * num_levels + sizeof(xspaces_t) + sizeof(xinstance_t);
            xbyte*        mem_block  = (xbyte*)main_alloc->allocate(mem_size);
            xallocinplace aip(mem_block, mem_size);
            xinstance_t*  instance    = aip.construct<xinstance_t>();
            xspaces_t*    spaces      = aip.construct<xspaces_t>();
            xlevel_t*     level_array = (xlevel_t*)aip.allocate(sizeof(xlevel_t) * num_levels);

            instance->m_main_alloc     = main_alloc;
            instance->m_node_alloc     = node_alloc;
            instance->m_vmem_base_addr = vmem_address;

            instance->m_allocsize_min  = allocsize_min;
            instance->m_allocsize_max  = allocsize_max;
            instance->m_allocsize_step = allocsize_step;
            instance->m_pagesize       = page_size;
            instance->m_level_cnt      = num_levels;
            instance->m_levels         = level_array;
			instance->m_spaces         = spaces;
            for (u32 i = 0; i < instance->m_level_cnt; ++i)
            {
                instance->m_levels[i].reset();
            }

            // Initialize the spaces manager
            instance->m_spaces->init(main_alloc, instance->m_vmem_base_addr, vmem_space, space_size, page_size);

            return instance;
        }

        void destroy(xinstance_t* self)
        {
            self->m_spaces->release(self->m_main_alloc);
            self->m_spaces = nullptr;
            self->m_levels = nullptr;
            self->m_main_alloc->deallocate(self);
        }


        void* allocate(xinstance_t* self, u32 size, u32 alignment)
        {
            ASSERT(size >= self->m_allocsize_min && size <= self->m_allocsize_max);
            ASSERT(alignment <= self->m_allocsize_step);
			u32 const base_size = xalignDown(self->m_allocsize_min, self->m_allocsize_step);
            u16 const ilevel = ((size - base_size) / self->m_allocsize_step);
			xlevel_t* plevel = idx2lvl(self, ilevel);
            u16 const size_in_pages = (u16)((size + (self->m_pagesize - 1)) / self->m_pagesize);
            void*     ptr    = plevel->allocate(size_in_pages, lvl2size_in_pages(self, ilevel), self->m_spaces, self->m_vmem_base_addr, self->m_allocsize_step);
            ASSERT(ptr2lvl(self, ptr) == ilevel);
            self->m_spaces->register_alloc(ptr);
            return ptr;
        }

        void deallocate(xinstance_t* self, void* ptr)
        {
            u16 const ilevel = self->m_spaces->register_dealloc(ptr);
			ASSERT(ilevel < self->m_level_cnt);
            xlevel_t* plevel = &self->m_levels[ilevel];
            plevel->deallocate(ptr, self->m_node_alloc, self->m_spaces);
        }
    } // namespace xsegregatedstrat
} // namespace xcore

