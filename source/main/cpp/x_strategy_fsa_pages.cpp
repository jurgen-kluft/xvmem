#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_fsa_small.h"
#include "xvmem/private/x_strategy_fsa_pages.h"
#include "xvmem/private/x_strategy_fsa_page.h"
#include "xvmem/private/x_doubly_linked_list.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    struct xpages_t
    {
        xpages_t(xalloc* main_allocator, void* base_address, u32 page_size, u16 const page_cnt, llnode* list_data, xpage_t* const page_array)
            : m_main_allocator(main_allocator)
            , m_base_address(base_address)
            , m_page_size(page_size)
            , m_free_page_index(0)
            , m_free_page_list()
            , m_page_cnt(page_cnt)
            , m_page_list(list_data)
            , m_pages(page_array)
        {
            m_free_page_list.initialize(list_data, page_cnt, page_cnt);
        }

        xpage_t* alloc_page(u32 const elem_size);
        void     free_page(xpage_t* const ppage);

        u32      address_to_elem_size(void* const address) const;
        xpage_t* address_to_page(void* const address) const;
        void*    address_of_page(xpage_t* const page) const;

        void*    idx2ptr(u32 const index) const;
        u32      ptr2idx(void* const ptr) const;
        xpage_t* next_page(xpage_t* const page);
        xpage_t* prev_page(xpage_t* const page);
        xpage_t* indexto_page(u16 const page) const;
        u16      indexof_page(xpage_t* const page) const;

        llnode* next_node(llnode* const node);
        llnode* prev_node(llnode* const node);
        llindex next_node(llindex const node) const;
        llindex prev_node(llindex const node) const;
        llnode* indexto_node(llindex const node) const;
        llindex indexof_node(llnode* const node) const;

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        xalloc*           m_main_allocator;
        void*             m_base_address;
        u32               m_page_size;
        u16               m_free_page_index;
        llist             m_free_page_list;
        u16 const         m_page_cnt;
        llnode*           m_page_list;
        xpage_t*          m_pages;
    };

    xpage_t* xpages_t::alloc_page(u32 const elem_size)
    {
        // Get a page from list of physical pages
        // If there are no free physical pages then take one from the list of
        // virtual pages and commit the page.
        // If there are also no free virtual pages then we are out-of-memory!
        xpage_t* ppage = nullptr;
        if (!m_free_page_list.is_empty())
        {
            // Get an empty page by removing it from the list
            llindex const ipage = m_free_page_list.remove_headi(m_page_list);
            ppage = indexto_page(ipage.get());

        }
        else if (m_free_page_index < m_page_cnt)
        {
            ppage = indexto_page(m_free_page_index);
            ppage->init(m_page_size, elem_size);
            m_free_page_index += 1;
        }
        else
        {
            // All pages are used
            return nullptr;
        }

		// TODO: Commit page

        // Initialize page with 'size' (alloc size)
        ppage->init(m_page_size, elem_size);
        return ppage;
    }

    void xpages_t::free_page(xpage_t* const ppage)
    {
		// TODO: Decommit page

        u16 const ipage = indexof_page(ppage);
        m_free_page_list.insert(m_page_list, ipage);
    }

    u32 xpages_t::address_to_elem_size(void* const address) const
    {
        xpage_t* ppage = address_to_page(address);
        return ppage->m_elem_size;
    }

    xpage_t* xpages_t::address_to_page(void* const address) const
    {
        u16 const ipage = (u16)(((u64)address - (u64)m_base_address) / m_page_size);
        return indexto_page(ipage);
    }

    void* xpages_t::address_of_page(xpage_t* const page) const
    {
        u64 const ipage = indexof_page(page);
        return x_advance_ptr(m_base_address, ipage * m_page_size);
    }

    static inline u32 index_of_elem(xpage_t const* const page, void* const page_base_address, void* const elem)
    {
        u32 const index = (u32)(((u64)elem - (u64)page_base_address) / page->m_elem_size);
        return index;
    }

    static inline u32* pointer_to_elem(xpage_t const* const page, void* const page_base_address, u32 const index)
    {
        u32* elem = (u32*)((xbyte*)page_base_address + ((u64)index * (u64)page->m_elem_size));
        return elem;
    }

    void* xpages_t::idx2ptr(u32 const index) const
    {
        if (index == 0xffffffff)
            return nullptr;
        u16 const ipage = (index >> 16) & 0xffff;
        u16 const ielem = (index >> 0) & 0xffff;
        xpage_t*  ppage = indexto_page(ipage);
        return pointer_to_elem(ppage, m_base_address, ielem);
    }

    u32 xpages_t::ptr2idx(void* const ptr) const
    {
        xpage_t*    ppage = address_to_page(ptr);
        u32 const   ipage = indexof_page(ppage);
        void* const apage = address_of_page(ppage);
        u32 const   ielem = index_of_elem(ppage, apage, ptr);
        return (ipage << 16) | (ielem);
    }

    llnode* xpages_t::next_node(llnode* const node)
    {
        llindex const inext = node->m_next;
        return indexto_node(inext);
    }

    llnode* xpages_t::prev_node(llnode* const node)
    {
        llindex const iprev = node->m_prev;
        return indexto_node(iprev);
    }

    llindex xpages_t::next_node(llindex const node) const
    {
        if (node.is_nil())
            return llindex();
        ASSERT(node.get() < m_page_cnt);
        return m_page_list[node.get()].m_next;
    }

    llindex xpages_t::prev_node(llindex const node) const
    {
        if (node.is_nil())
            return llindex();
        ASSERT(node.get() < m_page_cnt);
        return m_page_list[node.get()].m_prev;
    }

    llnode* xpages_t::indexto_node(llindex const node) const
    {
        if (node.is_nil())
            return nullptr;
        ASSERT(node.get() < m_page_cnt);
        return &m_page_list[node.get()];
    }

    llindex xpages_t::indexof_node(llnode* const node) const
    {
        if (node == nullptr)
            return llindex();
        llindex const index = (u16)(((u64)node - (u64)&m_page_list[0]) / sizeof(llnode));
        ASSERT(index.get() < m_page_cnt);
        return index;
    }

    xpage_t* xpages_t::next_page(xpage_t* const page)
    {
        if (page == nullptr)
            return nullptr;
        llindex const index = indexof_page(page);
        llindex const next  = next_node(index);
        if (next.is_nil())
            return nullptr;
        return &m_pages[next.get()];
    }

    xpage_t* xpages_t::prev_page(xpage_t* const page)
    {
        if (page == nullptr)
            return nullptr;
        llindex const index = indexof_page(page);
        llindex const prev  = prev_node(index);
        if (prev.is_nil())
            return nullptr;
        return &m_pages[prev.get()];
    }

    xpage_t* xpages_t::indexto_page(u16 const page) const
    {
        if (page == 0xffff)
            return nullptr;
        ASSERT(page < m_page_cnt);
        return &m_pages[page];
    }

    u16 xpages_t::indexof_page(xpage_t* const page) const
    {
        if (page == nullptr)
            return 0xffff;
        u16 const index = (u16)(((u64)page - (u64)&m_pages[0]) / sizeof(xpage_t));
        ASSERT(index < m_page_cnt);
        return index;
    }

    void* allocate(xpages_t* pages, u16& page_list, u32 const elem_size)
    {
        ASSERT(pages != nullptr);
        return nullptr;
    }

    void deallocate(xpages_t* pages, u16& page_list, void* const ptr)
    {
        ASSERT(pages != nullptr);
        ASSERT(ptr != nullptr);
    }

    xpages_t* create_fsa_pages(xalloc* main_allocator, void* base_address, u64 memory_range, u32 page_size)
    {
        ASSERT(main_allocator != nullptr);
        u32 const         page_cnt       = (u32)(memory_range / page_size);
        xpage_t*          page_array     = (xpage_t*)main_allocator->allocate(sizeof(xpage_t) * page_cnt, sizeof(void*));
        llnode* page_list_data = (llnode*)main_allocator->allocate(sizeof(llnode) * page_cnt, sizeof(void*));
        xpages_t*         pages          = main_allocator->construct<xpages_t>(main_allocator, base_address, page_size, page_cnt, page_list_data, page_array);

        return pages;
    }

    void destroy(xpages_t* pages)
    {
        ASSERT(pages != nullptr);
        ASSERT(pages->m_pages != nullptr);
        ASSERT(pages->m_page_list != nullptr);
        ASSERT(pages->m_main_allocator != nullptr);
        pages->m_main_allocator->deallocate(pages->m_page_list);
        pages->m_main_allocator->deallocate(pages->m_pages);
        pages->m_main_allocator->deallocate(pages);
    }

	llist  init_list(xpages_t* pages)
	{
		return llist(0, pages->m_page_cnt);
	}

    void* alloc_page(xpages_t* pages, llist& page_list, u32 const elem_size)
    {
        xpage_t*  ppage = pages->alloc_page(elem_size);
        u16 const ipage = pages->indexof_page(ppage);
        page_list.insert(pages->m_page_list, ipage);
        return pages->address_of_page(ppage);
    }

    void* free_one_page(xpages_t* pages, llist& page_list)
    {
        llhead const ipage = page_list.m_head;
        if (ipage.is_nil())
            return nullptr;
        page_list.remove_item(pages->m_page_list, ipage);
        xpage_t*    ppage = pages->indexto_page(ipage.get());
        void* const apage = pages->address_of_page(ppage);
        pages->free_page(ppage);
        return apage;
    }

    void free_all_pages(xpages_t* pages, llist& page_list)
    {
        while (!page_list.is_empty())
        {
            llhead const ipage = page_list.m_head;
            page_list.remove_item(pages->m_page_list, ipage);
            xpage_t* ppage = pages->indexto_page(ipage.get());
            pages->free_page(ppage);
        }
    }

    void* alloc_elem(xpages_t* pages, llist& page_list, llist& pages_empty_list, u32 const elem_size)
    {
        // If list is empty, request a new page and add it to the page_list
        // Using the page allocate a new element
        // return pointer to element
        // If page is full remove it from the list
        llindex  ipage;
        xpage_t* ppage = nullptr;
        if (page_list.is_empty())
        {
			if (pages_empty_list.is_empty())
			{
				ppage = pages->alloc_page(elem_size);
				ipage = pages->indexof_page(ppage);
			}
			else
			{
				ipage = pages_empty_list.remove_headi(pages->m_page_list);
				ppage = pages->indexto_page(ipage.get());
				ppage->init(pages->m_page_size, elem_size);
			}
			page_list.insert(pages->m_page_list, ipage);
        }
        else
        {
            ipage = page_list.m_head;
            ppage = pages->indexto_page(ipage.get());
        }

        void* const apage = pages->address_of_page(ppage);
        void*       ptr   = nullptr;
        if (ppage != nullptr)
        {
            ptr = ppage->allocate(apage);
            if (ppage->is_full())
            {
                page_list.remove_item(pages->m_page_list, ipage);
            }
        }
        return ptr;
    }

    u32 sizeof_elem(xpages_t* pages, void* const ptr)
    {
        xpage_t* ppage = pages->address_to_page(ptr);
        return ppage->m_elem_size;
    }

    u32   idx_of_elem(xpages_t* pages, void* const ptr) { return pages->ptr2idx(ptr); }
    void* ptr_of_elem(xpages_t* pages, u32 const index) { return pages->idx2ptr(index); }

    void free_elem(xpages_t* pages, llist& page_list, void* const ptr, llist& page_empty_list)
    {
        // Find page that this pointer belongs to
        // Determine element index of this pointer
        // Add element to free element list of the page
        // When page is empty remove it from the free list and add it to the 'pages_empty_list'
        // When page was full then now add it back to the list of 'usable' pages
        xpage_t*   ppage    = pages->address_to_page(ptr);
        u16 const  ipage    = pages->indexof_page(ppage);
        bool const was_full = ppage->is_full();
        ppage->deallocate(pages->address_of_page(ppage), ptr);
        if (ppage->is_empty())
        {
            ASSERT(pages->indexto_node(ipage)->is_linked());
            page_list.remove_item(pages->m_page_list, ipage);
            page_empty_list.insert(pages->m_page_list, ipage);
        }
        else if (was_full)
        {
            page_list.insert(pages->m_page_list, ipage);
        }
    }

    void* xpage_t::allocate(void* const block_base_address)
    {
        // if page == nullptr then first allocate a new page for this size
        // else use the page to allocate a new element.
        // Note: page should NOT be full when calling this function!
        if (m_free_list != 0xffff)
        {
            u16 const  ielem = m_free_list;
            u16* const pelem = (u16*)pointer_to_elem(this, block_base_address, ielem);
            m_free_list      = pelem[0];
            m_elem_used++;
            return (void*)pelem;
        }
        else if (m_free_index < m_elem_total)
        {
            m_elem_used++;
            u16 const  ielem = m_free_index++;
            u16* const pelem = (u16*)pointer_to_elem(this, block_base_address, ielem);
            return (void*)pelem;
        }
        else
        {
            return nullptr;
        }
    }

    void xpage_t::deallocate(void* const block_base_address, void* const ptr)
    {
        u16 const  ielem = index_of_elem(this, block_base_address, ptr);
        u16* const pelem = (u16*)pointer_to_elem(this, block_base_address, ielem);
        pelem[0]         = m_free_list;
        m_free_list      = ielem;
        m_elem_used -= 1;
    }

}; // namespace xcore
