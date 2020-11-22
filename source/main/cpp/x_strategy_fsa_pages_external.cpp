#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_fsa_small_external.h"
#include "xvmem/private/x_strategy_fsa_pages_external.h"
#include "xvmem/private/x_strategy_fsa_page_external.h"
#include "xvmem/private/x_doubly_linked_list.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    struct xpages_ext_t
    {
        xpages_ext_t(xalloc* main_allocator, void* base_address, u32 page_size, u16 const page_cnt, xalist_t::node_t* list_data, xpage_ext_t* const page_array)
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

        xpage_ext_t* alloc_page(u32 const elem_size);
        void         free_page(xpage_ext_t* const ppage);

        u32          address_to_elem_size(void* const address) const;
        xpage_ext_t* address_to_page(void* const address) const;
        void*        address_of_page(xpage_ext_t* const page) const;

        void*        idx2ptr(u32 const index) const;
        u32          ptr2idx(void* const ptr) const;
        xpage_ext_t* next_page(xpage_ext_t* const page);
        xpage_ext_t* prev_page(xpage_ext_t* const page);
        xpage_ext_t* indexto_page(u16 const page) const;
        u16          indexof_page(xpage_ext_t* const page) const;

        xalist_t::node_t* next_node(xalist_t::node_t* const node);
        xalist_t::node_t* prev_node(xalist_t::node_t* const node);
        u16               next_node(u16 const node) const;
        u16               prev_node(u16 const node) const;
        xalist_t::node_t* indexto_node(u16 const node) const;
        u16               indexof_node(xalist_t::node_t* const node) const;

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        xalloc*           m_main_allocator;
        void*             m_base_address;
        u32               m_page_size;
        u16               m_free_page_index;
        xalist_t          m_free_page_list;
        u16 const         m_page_cnt;
        xalist_t::node_t* m_page_list;
        xpage_ext_t*      m_pages;
    };

    xpage_ext_t* xpages_ext_t::alloc_page(u32 const elem_size)
    {
        // Get a page from list of physical pages
        // If there are no free physical pages then take one from the list of
        // virtual pages and commit the page.
        // If there are also no free virtual pages then we are out-of-memory!
        xpage_ext_t* ppage = nullptr;
        if (!m_free_page_list.is_empty())
        {
            // Get an empty page by removing it from the list
            u16 const ipage = m_free_page_list.remove_headi(m_page_list);
            ppage           = indexto_page(ipage);
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

    void xpages_ext_t::free_page(xpage_ext_t* const ppage)
    {
        // TODO: Decommit page

        u16 const ipage = indexof_page(ppage);
        m_free_page_list.insert(m_page_list, ipage);
    }

    u32 xpages_ext_t::address_to_elem_size(void* const address) const
    {
        xpage_ext_t* ppage = address_to_page(address);
        return ppage->m_elem_size;
    }

    xpage_ext_t* xpages_ext_t::address_to_page(void* const address) const
    {
        u16 const ipage = (u16)(((u64)address - (u64)m_base_address) / m_page_size);
        return indexto_page(ipage);
    }

    void* xpages_ext_t::address_of_page(xpage_ext_t* const page) const
    {
        u64 const ipage = indexof_page(page);
        return x_advance_ptr(m_base_address, ipage * m_page_size);
    }

    static inline u32 index_of_elem(xpage_ext_t const* const page, void* const page_base_address, void* const elem)
    {
        u32 const index = (u32)(((u64)elem - (u64)page_base_address) / page->m_elem_size);
        return index;
    }

    static inline u32* pointer_to_elem(xpage_ext_t const* const page, void* const page_base_address, u32 const index)
    {
        u32* elem = (u32*)((xbyte*)page_base_address + ((u64)index * (u64)page->m_elem_size));
        return elem;
    }

    void* xpages_ext_t::idx2ptr(u32 const index) const
    {
        if (index == 0xffffffff)
            return nullptr;
        u16 const    ipage = (index >> 16) & 0xffff;
        u16 const    ielem = (index >> 0) & 0xffff;
        xpage_ext_t* ppage = indexto_page(ipage);
        return pointer_to_elem(ppage, m_base_address, ielem);
    }

    u32 xpages_ext_t::ptr2idx(void* const ptr) const
    {
        xpage_ext_t* ppage = address_to_page(ptr);
        u32 const    ipage = indexof_page(ppage);
        void* const  apage = address_of_page(ppage);
        u32 const    ielem = index_of_elem(ppage, apage, ptr);
        return (ipage << 16) | (ielem);
    }

    xalist_t::node_t* xpages_ext_t::next_node(xalist_t::node_t* const node)
    {
        u16 const inext = node->m_next;
        return indexto_node(inext);
    }

    xalist_t::node_t* xpages_ext_t::prev_node(xalist_t::node_t* const node)
    {
        u16 const iprev = node->m_prev;
        return indexto_node(iprev);
    }

    u16 xpages_ext_t::next_node(u16 const node) const
    {
        if (node == xalist_t::NIL)
            return xalist_t::NIL;
        ASSERT(node < m_page_cnt);
        return m_page_list[node].m_next;
    }

    u16 xpages_ext_t::prev_node(u16 const node) const
    {
        if (node == xalist_t::NIL)
            return xalist_t::NIL;
        ASSERT(node < m_page_cnt);
        return m_page_list[node].m_prev;
    }

    xalist_t::node_t* xpages_ext_t::indexto_node(u16 const node) const
    {
        if (node == xalist_t::NIL)
            return nullptr;
        ASSERT(node < m_page_cnt);
        return &m_page_list[node];
    }

    u16 xpages_ext_t::indexof_node(xalist_t::node_t* const node) const
    {
        if (node == nullptr)
            return xalist_t::NIL;
        u16 const index = (u16)(((u64)node - (u64)&m_page_list[0]) / sizeof(xalist_t::node_t));
        ASSERT(index < m_page_cnt);
        return index;
    }

    xpage_ext_t* xpages_ext_t::next_page(xpage_ext_t* const page)
    {
        if (page == nullptr)
            return nullptr;
        u16 const index = indexof_page(page);
        u16 const next  = next_node(index);
        if (next == xalist_t::NIL)
            return nullptr;
        return &m_pages[next];
    }

    xpage_ext_t* xpages_ext_t::prev_page(xpage_ext_t* const page)
    {
        if (page == nullptr)
            return nullptr;
        u16 const index = indexof_page(page);
        u16 const prev  = prev_node(index);
        if (prev == xalist_t::NIL)
            return nullptr;
        return &m_pages[prev];
    }

    xpage_ext_t* xpages_ext_t::indexto_page(u16 const page) const
    {
        if (page == xalist_t::NIL)
            return nullptr;
        ASSERT(page < m_page_cnt);
        return &m_pages[page];
    }

    u16 xpages_ext_t::indexof_page(xpage_ext_t* const page) const
    {
        if (page == nullptr)
            return xalist_t::NIL;
        u16 const index = (u16)(((u64)page - (u64)&m_pages[0]) / sizeof(xpage_ext_t));
        ASSERT(index < m_page_cnt);
        return index;
    }

    void* allocate(xpages_ext_t* pages, u16& page_list, u32 const elem_size)
    {
        ASSERT(pages != nullptr);
        return nullptr;
    }

    void deallocate(xpages_ext_t* pages, u16& page_list, void* const ptr)
    {
        ASSERT(pages != nullptr);
        ASSERT(ptr != nullptr);
    }

    xpages_ext_t* create_fsa_pages(xalloc* main_allocator, void* base_address, u64 memory_range, u32 page_size)
    {
        ASSERT(main_allocator != nullptr);
        u32 const         page_cnt       = (u32)(memory_range / page_size);
        xpage_ext_t*      page_array     = (xpage_ext_t*)main_allocator->allocate(sizeof(xpage_ext_t) * page_cnt, sizeof(void*));
        xalist_t::node_t* page_list_data = (xalist_t::node_t*)main_allocator->allocate(sizeof(xalist_t::node_t) * page_cnt, sizeof(void*));
        xpages_ext_t*     pages          = main_allocator->construct<xpages_ext_t>(main_allocator, base_address, page_size, page_cnt, page_list_data, page_array);

        return pages;
    }

    void destroy(xpages_ext_t* pages)
    {
        ASSERT(pages != nullptr);
        ASSERT(pages->m_pages != nullptr);
        ASSERT(pages->m_page_list != nullptr);
        ASSERT(pages->m_main_allocator != nullptr);
        pages->m_main_allocator->deallocate(pages->m_page_list);
        pages->m_main_allocator->deallocate(pages->m_pages);
        pages->m_main_allocator->deallocate(pages);
    }

    xalist_t init_list(xpages_ext_t* pages) { return xalist_t(0, pages->m_page_cnt); }

    void* alloc_page(xpages_ext_t* pages, xalist_t& page_list, u32 const elem_size)
    {
        xpage_ext_t* ppage = pages->alloc_page(elem_size);
        u16 const    ipage = pages->indexof_page(ppage);
        page_list.insert(pages->m_page_list, ipage);
        return pages->address_of_page(ppage);
    }

    void* free_one_page(xpages_ext_t* pages, xalist_t& page_list)
    {
        u16 const ipage = page_list.m_head;
        if (ipage == xalist_t::NIL)
            return nullptr;
        page_list.remove_item(pages->m_page_list, ipage);
        xpage_ext_t* ppage = pages->indexto_page(ipage);
        void* const  apage = pages->address_of_page(ppage);
        pages->free_page(ppage);
        return apage;
    }

    void free_all_pages(xpages_ext_t* pages, xalist_t& page_list)
    {
        while (!page_list.is_empty())
        {
            u16 const ipage = page_list.m_head;
            page_list.remove_item(pages->m_page_list, ipage);
            xpage_ext_t* ppage = pages->indexto_page(ipage);
            pages->free_page(ppage);
        }
    }

    void* alloc_elem(xpages_ext_t* pages, xalist_t& page_list, xalist_t& pages_empty_list, u32 const elem_size)
    {
        // If list is empty, request a new page and add it to the page_list
        // Using the page allocate a new element
        // return pointer to element
        // If page is full remove it from the list
        u16          ipage = xalist_t::NIL;
        xpage_ext_t* ppage = nullptr;
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
                ppage = pages->indexto_page(ipage);
                ppage->init(pages->m_page_size, elem_size);
            }
            page_list.insert(pages->m_page_list, ipage);
        }
        else
        {
            ipage = page_list.m_head;
            ppage = pages->indexto_page(ipage);
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

    u32 sizeof_elem(xpages_ext_t* pages, void* const ptr)
    {
        xpage_ext_t* ppage = pages->address_to_page(ptr);
        return ppage->m_elem_size;
    }

    u32   idx_of_elem(xpages_ext_t* pages, void* const ptr) { return pages->ptr2idx(ptr); }
    void* ptr_of_elem(xpages_ext_t* pages, u32 const index) { return pages->idx2ptr(index); }

    void free_elem(xpages_ext_t* pages, xalist_t& page_list, void* const ptr, xalist_t& page_empty_list)
    {
        // Find page that this pointer belongs to
        // Determine element index of this pointer
        // Add element to free element list of the page
        // When page is empty remove it from the free list and add it to the 'pages_empty_list'
        // When page was full then now add it back to the list of 'usable' pages
        xpage_ext_t* ppage    = pages->address_to_page(ptr);
        u16 const    ipage    = pages->indexof_page(ppage);
        bool const   was_full = ppage->is_full();
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

    void* xpage_ext_t::allocate(void* const block_base_address)
    {
        // if page == nullptr then first allocate a new page for this size
        // else use the page to allocate a new element.
        // Note: page should NOT be full when calling this function!
        if (m_free_list != xalist_t::NIL)
        {
            u32 const  ielem = m_free_list;
            u32* const pelem = pointer_to_elem(this, block_base_address, ielem);
            m_free_list      = pelem[0];
            m_elem_used++;
            return (void*)pelem;
        }
        else if (m_free_index < m_elem_total)
        {
            m_elem_used++;
            u32 const  ielem = m_free_index++;
            u32* const pelem = pointer_to_elem(this, block_base_address, ielem);
            return (void*)pelem;
        }
        else
        {
            return nullptr;
        }
    }

    void xpage_ext_t::deallocate(void* const block_base_address, void* const ptr)
    {
        u32 const  ielem = index_of_elem(this, block_base_address, ptr);
        u32* const pelem = pointer_to_elem(this, block_base_address, ielem);
        pelem[0]         = m_free_list;
        m_free_list      = ielem;
        m_elem_used -= 1;
    }

}; // namespace xcore