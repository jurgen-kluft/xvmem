#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/x_virtual_memory.h"
#include "xvmem/x_virtual_pages.h"

namespace xcore
{
    void init_page(xvpage_t* const page, u32 const page_size, u32 const element_size)
    {
        page->m_next       = xvpage_t::INDEX_NIL;
        page->m_prev       = xvpage_t::INDEX_NIL;
        page->m_flags      = xvpage_t::PAGE_PHYSICAL;
        page->m_free_list  = xvpage_t::INDEX16_NIL;
        page->m_free_index = 0;
        page->m_elem_used  = 0;
        page->m_elem_total = (u16)(page_size / element_size);
        page->m_elem_size  = element_size;
    }

    static void inline page_unlink(xvpage_t* const page)
    {
        page->m_next = xvpage_t::INDEX_NIL;
        page->m_prev = xvpage_t::INDEX_NIL;
    }

    static inline u32 index_of_elem(xvpage_t* const page, void* const page_base_address, void* const elem)
    {
        u32 const index = (u32)(((u64)elem - (u64)page_base_address) / page->m_elem_size);
        return index;
    }
    static inline u32* pointer_to_elem(xvpage_t* const page, void* const page_base_address, u32 const index)
    {
        u32* elem = (u32*)((xbyte*)page_base_address + ((u64)index * (u64)page->m_elem_size));
        return elem;
    }

    void* allocate_from_page(xvpage_t* const page, void* const page_base_address)
    {
        // if page == nullptr then first allocate a new page for this size
        // else use the page to allocate a new element.
        // Note: page should NOT be full when calling this function!
        if (page->m_free_list != xvpage_t::INDEX16_NIL)
        {
            u32  index        = page->m_free_list;
            u32* elem         = pointer_to_elem(page, page_base_address, index);
            page->m_free_list = elem[0];
            page->m_elem_used++;
            return (void*)elem;
        }
        else if (page->m_free_index < page->m_elem_total)
        {
            page->m_elem_used++;
            u32  index = page->m_free_index++;
            u32* elem  = pointer_to_elem(page, page_base_address, index);
            return (void*)elem;
        }
        else
        {
            return nullptr;
        }
    }

    void deallocate_from_page(xvpage_t* page, void* const page_base_address, void* const ptr)
    {
        u32  index        = index_of_elem(page, page_base_address, ptr);
        u32* elem         = pointer_to_elem(page, page_base_address, index);
        elem[0]           = page->m_free_list;
        page->m_free_list = index;
        page->m_elem_used -= 1;
    }

    static void insert_in_list(xvpages_t* pages, u32& head, u32 page);
    static void remove_from_list(xvpages_t* pages, u32& head, u32 page);

	void xvpage_t::init()
	{
        m_next       = xvpage_t::INDEX_NIL;
        m_prev       = xvpage_t::INDEX_NIL;
        m_flags      = xvpage_t::PAGE_PHYSICAL;
        m_free_list  = xvpage_t::INDEX16_NIL;
        m_free_index = 0;
        m_elem_used  = 0;
        m_elem_total = 0;
        m_elem_size  = 0;
	}

    xvpages_t::xvpages_t(xalloc* main_allocator, xvmem* vmem, void* memory_base, u64 memory_range, u32 pagesize, u32 pagecachecnt)
        : m_main_allocator(main_allocator)
		, m_vmem(vmem)
		, m_page_array(nullptr)
        , m_page_size(pagesize)
        , m_page_total_cnt((u32)(memory_range / pagesize))
		, m_page_cache_cnt(pagecachecnt)
        , m_memory_base(memory_base)
        , m_memory_range(memory_range)
		, m_free_pages_index(0)
        , m_free_pages_physical_head(xvpage_t::INDEX_NIL)
        , m_free_pages_physical_count(0)
        , m_free_pages_virtual_head(xvpage_t::INDEX_NIL)
        , m_free_pages_virtual_count(0)
    {
    }

	void xvpages_t::initialize()
	{
		ASSERT(m_page_array == nullptr);
        m_page_array = (xvpage_t*)m_main_allocator->allocate(sizeof(xvpage_t) * m_page_total_cnt, sizeof(void*));
	}

	void xvpages_t::release()
	{
        m_main_allocator->deallocate(m_page_array);
		m_page_array = nullptr;

		m_main_allocator->deallocate(this);
	}

    u64 xvpages_t::memory_range() const { return m_memory_range; }

    xvpage_t* xvpages_t::alloc_page(u32 const allocsize)
    {
        // Get a page from list of physical pages
        // If there are no free physical pages then take one from the list of 
		// virtual pages and commit the page.
		// If there are also no free virtual pages then we are out-of-memory!
        xvpage_t* ppage = nullptr;
        if (m_free_pages_physical_head == xvpage_t::INDEX_NIL)
        {
            if (m_free_pages_virtual_head != xvpage_t::INDEX_NIL)
            {
                // Get the page pointer and remove it from the list of virtual pages
                ppage = indexto_page(m_free_pages_virtual_head);
                remove_from_list(this, m_free_pages_virtual_head, m_free_pages_virtual_head);

                // Commit the virtual memory to physical memory
                void*  address = get_base_address(ppage);
                m_vmem->commit(address, m_page_size, 1);
            }
			else if (m_free_pages_index < m_page_total_cnt)
			{
				ppage = indexto_page(m_free_pages_index);
				ppage->init();
				m_free_pages_index += 1;

                // Commit the virtual memory to physical memory
                void*  address = get_base_address(ppage);
                m_vmem->commit(address, m_page_size, 1);
			}
			else
			{
				// All pages are used
				return nullptr;
			}
        }
        else
        {
            // Get the page pointer and remove it from the list of physical pages
            ppage = indexto_page(m_free_pages_physical_head);
            remove_from_list(this, m_free_pages_physical_head, m_free_pages_physical_head);
            m_free_pages_physical_count -= 1;
        }

        // Page is committed, so it is physical, mark it
        ppage->set_is_physical();

        // Initialize page with 'size' (alloc size)
        init_page(ppage, m_page_size, allocsize);

        return ppage;
    }

    void xvpages_t::free_page(xvpage_t* const ppage)
    {
        // If amount of physical pages crosses the maximum then decommit the memory of this page and add it to list of virtual pages
        // Otherwise add this page to the list of physical pages
        if (m_free_pages_physical_count < m_page_cache_cnt)
        {
            u32 const page = indexof_page(ppage);
            insert_in_list(this, m_free_pages_physical_head, page);
			m_free_pages_physical_count += 1;
        }
        else
        {
            void* const  address = get_base_address(ppage);
            m_vmem->decommit(address, m_page_size, 1);
            ppage->set_is_virtual();

            u32 const page = indexof_page(ppage);
            insert_in_list(this, m_free_pages_virtual_head, page);
        }
    }

    u32 xvpages_t::address_to_allocsize(void* const address) const
    {
        xvpage_t* const ppage = address_to_page(address);
        return ppage->m_elem_size;
    }

	xvpage_t* xvpages_t::address_to_page(void* const address) const
    {
        u32 const page_index = (u32)(((u64)address - (u64)m_memory_base) / m_page_size);
        return &m_page_array[page_index];
    }

    void* xvpages_t::get_base_address(xvpage_t* const page) const
    {
        u32 const page_index = indexof_page(page);
        return (xbyte*)m_memory_base + (page_index * m_page_size);
    }

    void* xvpages_t::idx2ptr(u32 const index, u32 const page_elem_cnt, u32 const alloc_size) const
    {
		ASSERT(index < 0xffffffff);
        u64 const   page = index / page_elem_cnt;
        u64 const   elem = index - (page * page_elem_cnt);
        void* const ptr  = (xbyte*)m_memory_base + (page * m_page_size) + (elem * alloc_size);
        return ptr;
    }

    u32 xvpages_t::ptr2idx(void* const ptr, u32 const page_elem_cnt, u32 const alloc_size) const
    {
        u64 const page = ((u64)ptr - (u64)m_memory_base) / m_page_size;
        u64 const base = (u64)m_memory_base + (page * m_page_size);
        u64 const elem = (page * page_elem_cnt) + (((u64)ptr - base) / alloc_size);
		ASSERT(elem < 0xffffffff);
        return (u32)elem;
    }

    xvpage_t* xvpages_t::next_page(xvpage_t* const page)
    {
        u32 const next = page->m_next;
        return &m_page_array[next];
    }
    xvpage_t* xvpages_t::prev_page(xvpage_t* const page)
    {
        u32 const prev = page->m_prev;
        return &m_page_array[prev];
    }

    xvpage_t* xvpages_t::indexto_page(u32 const page) const
    {
		if (page >= m_page_total_cnt)
		{
			ASSERT(page < m_page_total_cnt);
		}
        return &m_page_array[page];
    }

    u32 xvpages_t::indexof_page(xvpage_t* const ppage) const
    {
        if (ppage == nullptr)
            return xvpage_t::INDEX_NIL;
        u32 const page = (u32)(ppage - &m_page_array[0]);
        return page;
    }

    static inline void insert_in_list(xvpages_t* pages, u32& head, u32 page)
    {
		// TODO: Sort the free pages by address !!
        if (head == xvpage_t::INDEX_NIL)
        {
            xvpage_t* const ppage = pages->indexto_page(page);
            ppage->m_next         = page;
            ppage->m_prev         = page;
            head                  = page;
        }
        else
        {
            xvpage_t* const phead = pages->indexto_page(head);
            xvpage_t* const pnext = pages->indexto_page(phead->m_next);
            xvpage_t* const ppage = pages->indexto_page(page);
            ppage->m_prev         = head;
            ppage->m_next         = phead->m_next;
            phead->m_next         = page;
            pnext->m_prev         = page;
        }
    }

    static inline void remove_from_list(xvpages_t* pages, u32& head, u32 page)
    {
        xvpage_t* const phead = pages->indexto_page(head);
        xvpage_t* const ppage = pages->indexto_page(page);
        xvpage_t* const pprev = pages->indexto_page(ppage->m_prev);
        xvpage_t* const pnext = pages->indexto_page(ppage->m_next);
        pprev->m_next         = ppage->m_next;
        pnext->m_prev         = ppage->m_prev;
        page_unlink(ppage);

        if (phead == ppage)
        {
			if (pnext == ppage)
			{
				head = xvpage_t::INDEX_NIL;
			}
			else
			{
				head = pages->indexof_page(pnext);
			}
        }
    }

    void* xvpages_t::allocate(u32& page_list, u32 allocsize)
    {
        // If list is empty, request a new page and add it to the page_list
        // Using the page allocate a new element
        // return pointer to element
        // If page is full remove it from the list
        u32       page  = xvpage_t::INDEX_NIL;
        xvpage_t* ppage = nullptr;
        if (page_list == xvpage_t::INDEX_NIL)
        {
            ppage = alloc_page(allocsize);
            page  = indexof_page(ppage);
            insert_in_list(this, page_list, page);
        }
        else
        {
            page  = page_list;
            ppage = indexto_page(page);
        }

        void* ptr = nullptr;
        if (ppage != nullptr)
        {
            ptr = allocate_from_page(ppage, get_base_address(ppage));
            if (ppage->is_full())
            {
                remove_from_list(this, page_list, page);
            }
        }
        return ptr;
    }

    void xvpages_t::deallocate(u32& page_list, void* ptr)
    {
        // Find page that this pointer belongs to
        // Determine element index of this pointer
        // Add element to free element list of the page
        // If page is now empty, decide to deallocate this page
        // When deallocating this page, remove it from the free list
		// When page was full then now add it back to the list of 'usable' pages
        xvpage_t* ppage = address_to_page(ptr);
        u32 const page  = indexof_page(ppage);
		bool const was_full = ppage->is_full();
        deallocate_from_page(ppage, get_base_address(ppage), ptr);
        if (ppage->is_empty())
        {
            if (ppage->is_linked())
            {
                remove_from_list(this, page_list, page);
            }
            free_page(ppage);
        }
        else if (was_full)
        {
            insert_in_list(this, page_list, page);
        }
    }

    // An object that can alloc/free pages from a memory range
    xvpages_t* gCreateVPages(xalloc* main_allocator, xvmem* vmem, u64 memoryrange, u32 pagecachecnt)
    {
        u32    pagesize;
        void*  memory_base = nullptr;
        vmem->reserve(memoryrange, pagesize, 0, memory_base);

        xvpages_t* vpages = main_allocator->construct<xvpages_t>(main_allocator, vmem, memory_base, memoryrange, pagesize, pagecachecnt);
		vpages->initialize();
        return vpages;
    }

}; // namespace xcore
