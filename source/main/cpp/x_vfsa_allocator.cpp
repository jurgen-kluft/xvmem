#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/x_vfsa_allocator.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    struct xvpage_t
    {
        enum
        {
            INDEX_NIL     = 0xffffffff,
			INDEX16_NIL   = 0xffff,
            PAGE_PHYSICAL = 1,
            PAGE_VIRTUAL  = 2,
            PAGE_EMPTY    = 4,
            PAGE_FULL     = 8
        };

		// Constraints:
		// - maximum number of elements is 32768
		// - minimum size of an element is 4 bytes
		// - maximum page-size is 32768 * sizeof-element
		// 
        u32 m_next;
        u32 m_prev;
        u16 m_free_list;
        u16 m_free_index;
        u16 m_elem_used;
        u16 m_elem_total;
        u16 m_elem_size;
        u16 m_flags;

        bool is_full() const { return (m_flags & PAGE_FULL) == PAGE_FULL; }
        bool is_empty() const { return (m_flags & PAGE_EMPTY) == PAGE_EMPTY; }
        bool is_physical() const { return (m_flags & PAGE_PHYSICAL) == PAGE_PHYSICAL; }
        bool is_virtual() const { return (m_flags & PAGE_VIRTUAL) == PAGE_VIRTUAL; }
        bool is_linked() const { return !(m_next == INDEX_NIL && m_prev == INDEX_NIL); }
    };

    void init_page(xvpage_t* page, u32 page_size, u32 element_size)
    {
        page->m_next       = xvpage_t::INDEX_NIL;
        page->m_prev       = xvpage_t::INDEX_NIL;
        page->m_flags      = xvpage_t::PAGE_PHYSICAL | xvpage_t::PAGE_EMPTY;
        page->m_free_list  = xvpage_t::INDEX16_NIL;
        page->m_free_index = 0;
        page->m_elem_used  = 0;
        page->m_elem_total = (u16)(page_size / element_size);
        page->m_elem_size  = element_size;
    }

    static void inline page_unlink(xvpage_t* page)
    {
        page->m_next = xvpage_t::INDEX_NIL;
        page->m_prev = xvpage_t::INDEX_NIL;
    }

    static inline u32  indexof_elem(xvpage_t* page, void* page_base_address, void* elem) 
    {
        u32 const index = (u32)(((u64)elem - (u64)page_base_address) / page->m_elem_size); 
        return index;
    }
    static inline u32* indexto_elem(xvpage_t* page, void* page_base_address, u32 index)
    {
        u32* elem = (u32*)((xbyte*)page_base_address + ((u64)index * (u64)page->m_elem_size));
        return elem;
    }

    void* allocate_from_page(xvpage_t* page, void* page_base_address)
    {
        // if page == nullptr then first allocate a new page for this size
        // else use the page to allocate a new element.
        // Note: page should NOT be full when calling this function!
        if (page == nullptr)
        {
            u32  index        = page->m_free_list;
            u32* elem         = indexto_elem(page, page_base_address, index);
            page->m_free_list = elem[0];
            
			page->m_elem_used++;
			if (page->m_elem_used == page->m_elem_total)
			{
				page->m_flags |= xvpage_t::PAGE_FULL;
			}
            return (void*)elem;
        }
        else if (page->m_free_index < page->m_elem_total)
        {
            page->m_elem_used++;
            u32  index = page->m_free_index++;
            u32* elem  = indexto_elem(page, page_base_address, index);
            return (void*)elem;
        }
        else
        {
            return nullptr;
        }
    }

    void deallocate_from_page(xvpage_t* page, void* page_base_address, void* ptr)
    {
        u32  index        = indexof_elem(page, page_base_address, ptr);
        u32* elem         = indexto_elem(page, page_base_address, index);
        elem[0]           = page->m_free_list;
        page->m_free_list = index;

		if (page->m_elem_used == page->m_elem_total)
		{
			page->m_flags &= ~xvpage_t::PAGE_FULL;
		}
        
		page->m_elem_used -= 1;
		if (page->m_elem_used == 0)
		{
			page->m_flags |= xvpage_t::PAGE_EMPTY;
		}
    }

    class xvpages_t
    {
    public:
        void initialize(xvpage_t* pages, u32 count, void* memory_base, u64 memory_range)
		{
			m_memory_base = memory_base;
			m_memory_range = memory_range;
		}

        u64 memory_range() const { return m_memory_range; }

        void* allocate(u32& freelist, u32 allocsize);
        void  deallocate(u32& freelist, void* ptr);

        xvpage_t* alloc_page(u32 size)
        {
            xvpage_t* ppage = nullptr;

            // Get a page from list of physical pages
            // If there are no free physical pages then take one from list of virtual pages and commit the page
            // If there are also no free virtual pages then we are out-of-memory!

            // Initialize page with 'size' (alloc size)

            return ppage;
        }

        void      free_page(xvpage_t* page)
        {
            // If amount of physical pages crosses the maximum then decommit the memory of this page and add it to list of virtual pages
            // Otherwise add this page to the list of physical pages
            
        }

        xvpage_t* find_page(void* address) const
        {
            u32 const page_index = ((u64)address - (u64)m_memory_base) / m_page_size;
            return &m_page[page_index];
        }

        void* idx2ptr(u64 index, u32 page_elem_cnt, u32 alloc_size) const
        {
            u64 const page = index / page_elem_cnt;
            u64 const elem = index - (page * page_elem_cnt);
            void* ptr = (xbyte*)m_memory_base + (page * m_page_size) + (elem * alloc_size);
            return ptr;
        }

        u64   ptr2idx(void* ptr, u32 page_elem_cnt, u32 alloc_size) const
        {
            u64 const page = ((u64)ptr - (u64)m_memory_base) / m_page_size;
            u64 const base = (u64)m_memory_base + (page * m_page_size);
            u64 const elem = (page * page_elem_cnt) + (((u64)ptr - base) / alloc_size);
            return elem;
        }

        void*     get_base_address(xvpage_t* page) const
        {
            u32 const page_index = indexof_page(page);
            return (xbyte*)m_memory_base + (page_index * m_page_size);
        }

        xvpage_t* next_page(xvpage_t* page)
        {
            u32 next = page->m_next;
            return &m_page[next];
        }
        xvpage_t* prev_page(xvpage_t* page)
        {
            u32 prev = page->m_prev;
            return &m_page[prev];
        }

        xvpage_t* indexto_page(u32 page) const
        {
            ASSERT(page < m_page_total_cnt);
            return &m_page[page];
        }
        u32       indexof_page(xvpage_t* ppage) const
        {
            if (ppage == nullptr) return xvpage_t::INDEX_NIL;
            u32 page = (u32)(ppage - &m_page[0]);
            return page;
        }

        void*     m_memory_base;
        u64       m_memory_range;
        u32       m_page_size;
        u32       m_page_total_cnt;
        u32       m_page_list_physical_free;
        u32       m_page_list_virtual_free;
        xvpage_t* m_page;
    };

    static inline void      insert_in_list(xvpages_t* pages, u32& head, u32 page)
    {
        if (head == xvpage_t::INDEX_NIL)
        {
            xvpage_t* ppage = &pages->m_page[page];
            ppage->m_next = page;
            ppage->m_prev = page;
            head = page;
        }
        else
        {
            xvpage_t* phead = &pages->m_page[head];
            xvpage_t* pnext = &pages->m_page[phead->m_next];
            xvpage_t* ppage = &pages->m_page[page];
            ppage->m_prev = head;
            ppage->m_next = phead->m_next;
            phead->m_next = page;
            pnext->m_prev = page;
        }
    }

    static inline void      remove_from_list(xvpages_t* pages, u32& head, u32 page)
    {
        xvpage_t* phead = &pages->m_page[head];
        xvpage_t* ppage = &pages->m_page[page];
        xvpage_t* pprev = &pages->m_page[ppage->m_prev];
        xvpage_t* pnext = &pages->m_page[ppage->m_next];
        pprev->m_next = ppage->m_next;
        pnext->m_prev = ppage->m_prev;
        page_unlink(ppage);

        if (phead == ppage)
        {
            phead = pnext;
            if (phead == ppage)
            {
                phead = nullptr;
            }
        }
        head = pages->indexof_page(phead);
    }

    void* xvpages_t::allocate(u32& page_list, u32 allocsize)
    {
        // If free list is empty, request a new page and add it to the freelist
        // Using the page allocate a new element
        // return pointer to element
        // If page is full remove it from the free list
        u32 page = xvpage_t::INDEX_NIL;
        xvpage_t* ppage = nullptr;
        if (page_list == xvpage_t::INDEX_NIL)
        {
            ppage = alloc_page(allocsize);
            page = indexof_page(ppage);
            insert_in_list(this, page_list, page);
        }
        else
        {
            page = page_list;
            ppage = indexto_page(page);
        }

        void* ptr = nullptr;
        if (ppage != nullptr)
        {
            void* ptr = allocate_from_page(ppage, get_base_address(ppage));
            if (ppage->is_full())
            {
                remove_from_list(this, page_list, page);
            }
        }
        return ptr;
    }

    void xvpages_t::deallocate(u32& freelist, void* ptr)
    {
        // Find page that this pointer belongs to
        // Determine element index of this pointer
        // Add element to free element list of the page
        // If page is now empty, decide to deallocate this page
        // When deallocating this page, remove it from the free list
        xvpage_t* ppage = find_page(ptr);
        u32 const page = indexof_page(ppage);
        deallocate_from_page(ppage, get_base_address(ppage), ptr);
        if (ppage->is_empty())
        {
            if (ppage->is_linked())
            {
                remove_from_list(this, freelist, page);
            }
            free_page(ppage);
        }
        else
        {
            insert_in_list(this, freelist, page);
        }
    }    

    // ----------------------------------------------------------------------------------------------
    // ----------------------------------------------------------------------------------------------
    // ----------------------------------------------------------------------------------------------
    class xvfsa : public xfsa
    {
    public:
        inline xvfsa(xvpages_t* pages, u32 allocsize) : m_pages(pages), m_pages_freelist(xvpage_t::INDEX_NIL), m_alloc_size(allocsize) {}

		virtual u32 size() const { return m_alloc_size; }

        virtual void* allocate()
        {
            return m_pages->allocate(m_pages_freelist, m_alloc_size);
        }

        virtual void deallocate(void* ptr)
        {
            return m_pages->deallocate(m_pages_freelist, ptr);
        }

		virtual void release()
		{
			
		}

		XCORE_CLASS_PLACEMENT_NEW_DELETE
    protected:
        xvpages_t* m_pages;
        u32        m_pages_freelist;
        u32        m_alloc_size;
    };

    xfsa* gCreateVMemBasedFsa(xalloc* main_allocator, xvpages_t* vpages, u32 allocsize) 
    {
        xvfsa* fsa = main_allocator->construct<xvfsa>(vpages, allocsize);
        return fsa;
    }

    // ----------------------------------------------------------------------------------------------
    // ----------------------------------------------------------------------------------------------
    // ----------------------------------------------------------------------------------------------
    class xvfsa_dexed : public xfsadexed
    {
    public:
        inline xvfsa_dexed(xvpages_t* pages, u32 allocsize) : m_pages(pages), m_pages_freelist(xvpage_t::INDEX_NIL), m_alloc_size(allocsize) 
		{
			m_page_elem_cnt = pages->m_page_size / allocsize;
		}

		virtual u32 size() const { return m_alloc_size; }

        virtual void* allocate()
        {
            return m_pages->allocate(m_pages_freelist, m_alloc_size);
        }

        virtual void deallocate(void* ptr)
        {
            return m_pages->deallocate(m_pages_freelist, ptr);
        }

        virtual void* idx2ptr(u32 index) const
        {
            return m_pages->idx2ptr(index, m_page_elem_cnt, m_alloc_size);
        }

        virtual u32   ptr2idx(void* ptr) const
        {
            u64 index = m_pages->ptr2idx(ptr, m_page_elem_cnt, m_alloc_size);
            return (u32)index;
        }

		virtual void release()
		{
			
		}

		XCORE_CLASS_PLACEMENT_NEW_DELETE
    protected:
        xvpages_t* m_pages;
        u32        m_pages_freelist;
        u32        m_alloc_size;
		u32		   m_page_elem_cnt;
    };

    // Constraints:
    // - xvpages is not allowed to manage a memory range more than (4G * allocsize) (32-bit indices)
    xfsadexed* gCreateVMemBasedDexedFsa(xalloc* main_allocator, xvpages_t* vpages, u32 allocsize)
    {
        ASSERT(vpages->memory_range() <= ((u64)4 * 1024 * 1024 * 1024));
        xvfsa_dexed* fsa = main_allocator->construct<xvfsa_dexed>(vpages, allocsize);
        return fsa;
    }

}; // namespace xcore
