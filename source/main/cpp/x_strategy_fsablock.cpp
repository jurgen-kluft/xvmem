#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_fsablock.h"

namespace xcore
{
    static const u16 INDEX16_NIL = 0xffff;

    static inline void* advance_ptr(void* ptr, u64 size) { return (void*)((uptr)ptr + size); }
    static inline void* align_ptr(void* ptr, u32 alignment) { return (void*)(((uptr)ptr + (alignment - 1)) & ~((uptr)alignment - 1)); }
    static uptr         diff_ptr(void* ptr, void* next_ptr) { return (size_t)((uptr)next_ptr - (uptr)ptr); }

    struct xfsapage_t
    {

        // Constraints:
        // - maximum number of elements is (65535-1)
        // - minimum size of an element is 4 bytes
        // - maximum page-size is (65535-1) * sizeof-element
        //
        u16 m_free_list;
        u16 m_free_index;
        u16 m_elem_used;
        u16 m_elem_total;
        u16 m_elem_size;

        void init(u32 pool_size, u32 elem_size)
        {
            m_free_list  = INDEX16_NIL;
            m_free_index = 0;
            m_elem_used  = 0;
            m_elem_total = pool_size / elem_size;
            m_elem_size  = elem_size;
        }

        bool is_full() const { return m_elem_used == m_elem_total; }
        bool is_empty() const { return m_elem_used == 0; }

        void* allocate(void* const block_base_address);
        void  deallocate(void* const block_base_address, void* const p);

        XCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    static inline u32 index_of_elem(xfsapage_t const* const page, void* const page_base_address, void* const elem)
    {
        u32 const index = (u32)(((u64)elem - (u64)page_base_address) / page->m_elem_size);
        return index;
    }

    static inline u32* pointer_to_elem(xfsapage_t const* const page, void* const page_base_address, u32 const index)
    {
        u32* elem = (u32*)((xbyte*)page_base_address + ((u64)index * (u64)page->m_elem_size));
        return elem;
    }

    void* xfsapage_t::allocate(void* const block_base_address)
    {
        // if page == nullptr then first allocate a new page for this size
        // else use the page to allocate a new element.
        // Note: page should NOT be full when calling this function!
        if (m_free_list != INDEX16_NIL)
        {
            u32 const ielem = m_free_list;
            ASSERT(ielem < INDEX16_NIL);
            u32* const pelem = pointer_to_elem(this, block_base_address, ielem);
            m_free_list      = pelem[0];
            m_elem_used++;
            return (void*)pelem;
        }
        else if (m_free_index < m_elem_total)
        {
            m_elem_used++;
            u32 const ielem = m_free_index++;
            ASSERT(ielem < INDEX16_NIL);
            u32* const pelem = pointer_to_elem(this, block_base_address, ielem);
            return (void*)pelem;
        }
        else
        {
            return nullptr;
        }
    }

    void xfsapage_t::deallocate(void* const block_base_address, void* const ptr)
    {
        u32 const ielem = index_of_elem(this, block_base_address, ptr);
        ASSERT(ielem < INDEX16_NIL);
        u32* const pelem = pointer_to_elem(this, block_base_address, ielem);
        pelem[0]         = m_free_list;
        m_free_list      = ielem;
        m_elem_used -= 1;
    }

    struct xfsapages_t
    {
        struct llnode_t;

        xfsapages_t(void* base_address, u32 page_size, u16 const page_cnt, llnode_t* llnode_array, xfsapage_t* const page_array)
            : m_base_address(base_address)
            , m_page_size(page_size)
            , m_free_page_index(0)
            , m_free_page_count(0)
            , m_free_page_head(0)
            , m_page_cnt(page_cnt)
            , m_page_list(llnode_array)
            , m_pages(page_array)
        {
        }

        xfsapage_t* alloc_page(u32 const elem_size);
        void        free_page(xfsapage_t* const ppage);
        void        free_page_list(u16& page_list);

        u32         address_to_elem_size(void* const address) const;
        xfsapage_t* address_to_page(void* const address) const;
        void*       address_of_page(xfsapage_t* const page) const;

        void*       idx2ptr(u32 const index) const;
        u32         ptr2idx(void* const ptr) const;
        xfsapage_t* next_page(xfsapage_t* const page);
        xfsapage_t* prev_page(xfsapage_t* const page);
        xfsapage_t* indexto_page(u16 const page) const;
        u16         indexof_page(xfsapage_t* const page) const;

        struct llnode_t
        {
            u16 m_prev, m_next;
        };
        llnode_t* next_node(llnode_t* const node);
        llnode_t* prev_node(llnode_t* const node);
        u16       next_node(u16 const node) const;
        u16       prev_node(u16 const node) const;
        llnode_t* indexto_node(u16 const node) const;
        u16       indexof_node(llnode_t* const node) const;

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        void*       m_base_address;
        u32         m_page_size;
        u16         m_free_page_index;
        u16         m_free_page_count;
        u16         m_free_page_head;
        u16 const   m_page_cnt;
        llnode_t*   m_page_list;
        xfsapage_t* m_pages;
    };

    static void insert_in_list(xfsapages_t* pages, u16& head, u16 page);
    static void remove_from_list(xfsapages_t* pages, u16& head, u16 page);

    xfsapage_t* xfsapages_t::alloc_page(u32 const elem_size)
    {
        // Get a page from list of physical pages
        // If there are no free physical pages then take one from the list of
        // virtual pages and commit the page.
        // If there are also no free virtual pages then we are out-of-memory!
        xfsapage_t* ppage = nullptr;
        if (m_free_page_head != INDEX16_NIL)
        {
            // Get the page pointer and remove it from the list of virtual pages
            ppage = indexto_page(m_free_page_head);
            remove_from_list(this, m_free_page_head, m_free_page_head);

            // Decommit the physical memory
        }
        else if (m_free_page_index < m_page_cnt)
        {
            ppage = indexto_page(m_free_page_index);
            ppage->init(m_page_size, elem_size);
            m_free_page_index += 1;

            // Commit the virtual memory to physical memory
        }
        else
        {
            // All pages are used
            return nullptr;
        }

        // Initialize page with 'size' (alloc size)
        ppage->init(m_page_size, elem_size);
        return ppage;
    }

    void xfsapages_t::free_page(xfsapage_t* const ppage)
    {
        u16 const ipage = indexof_page(ppage);
        insert_in_list(this, m_free_page_head, ipage);
    }

    u32 xfsapages_t::address_to_elem_size(void* const address) const
    {
        xfsapage_t* ppage = address_to_page(address);
        return ppage->m_elem_size;
    }

    xfsapage_t* xfsapages_t::address_to_page(void* const address) const
    {
        u16 const ipage = (u16)(((u64)address - (u64)m_base_address) / m_page_size);
        return indexto_page(ipage);
    }

    void* xfsapages_t::address_of_page(xfsapage_t* const page) const
    {
        u64 const ipage = indexof_page(page);
        return advance_ptr(m_base_address, ipage * m_page_size);
    }

    void* xfsapages_t::idx2ptr(u32 const index) const
    {
        if (index == 0xffffffff)
            return nullptr;
        u16 const   ipage = (index >> 16) & 0xffff;
        u16 const   ielem = (index >> 0) & 0xffff;
        xfsapage_t* ppage = indexto_page(ipage);
        return pointer_to_elem(ppage, m_base_address, ielem);
    }

    u32 xfsapages_t::ptr2idx(void* const ptr) const
    {
        xfsapage_t* ppage = address_to_page(ptr);
        u32 const   ipage = indexof_page(ppage);
        void* const apage = address_of_page(ppage);
        u32 const   ielem = index_of_elem(ppage, apage, ptr);
        return (ipage << 16) | (ielem);
    }

    xfsapages_t::llnode_t* xfsapages_t::next_node(xfsapages_t::llnode_t* const node)
    {
        u16 const inext = node->m_next;
        return indexto_node(inext);
    }

    xfsapages_t::llnode_t* xfsapages_t::prev_node(xfsapages_t::llnode_t* const node)
    {
        u16 const iprev = node->m_prev;
        return indexto_node(iprev);
    }

    u16 xfsapages_t::next_node(u16 const node) const
    {
        if (node == INDEX16_NIL)
            return INDEX16_NIL;
        return m_page_list[node].m_next;
    }

    u16 xfsapages_t::prev_node(u16 const node) const
    {
        if (node == INDEX16_NIL)
            return INDEX16_NIL;
        return m_page_list[node].m_prev;
    }

    xfsapages_t::llnode_t* xfsapages_t::indexto_node(u16 const node) const
    {
        if (node == INDEX16_NIL)
            return nullptr;
        return &m_page_list[node];
    }

    u16 xfsapages_t::indexof_node(llnode_t* const node) const
    {
        if (node == nullptr)
            return INDEX16_NIL;
        u16 const index = (u16)(((u64)node - (u64)&m_page_list[0]) / sizeof(xfsapages_t::llnode_t));
        return index;
    }

    xfsapage_t* xfsapages_t::next_page(xfsapage_t* const page)
    {
        if (page == nullptr)
            return nullptr;
        u16 const index = indexof_page(page);
        u16 const next  = next_node(index);
        if (next == INDEX16_NIL)
            return nullptr;
        return &m_pages[next];
    }

    xfsapage_t* xfsapages_t::prev_page(xfsapage_t* const page)
    {
        if (page == nullptr)
            return nullptr;
        u16 const index = indexof_page(page);
        u16 const prev  = prev_node(index);
        if (prev == INDEX16_NIL)
            return nullptr;
        return &m_pages[prev];
    }

    xfsapage_t* xfsapages_t::indexto_page(u16 const page) const
    {
        if (page == INDEX16_NIL)
            return nullptr;
        return &m_pages[page];
    }

    u16 xfsapages_t::indexof_page(xfsapage_t* const page) const
    {
        if (page == nullptr)
            return INDEX16_NIL;
        u16 const index = (u16)(((u64)page - (u64)&m_pages[0]) / sizeof(xfsapages_t::llnode_t));
        return index;
    }

    void destroy(xalloc* main_allocator, xfsapages_t* pages)
    {
        ASSERT(main_allocator != nullptr);
        ASSERT(pages != nullptr);
    }

    void* allocate(xfsapages_t* pages, u16& page_list, u32 const elem_size)
    {
        ASSERT(pages != nullptr);
        return nullptr;
    }

    void deallocate(xfsapages_t* pages, u16& page_list, void* const ptr)
    {
        ASSERT(pages != nullptr);
        ASSERT(ptr != nullptr);
    }

    static inline bool is_page_linked(xfsapages_t* pages, u16 page)
    {
        if (page == INDEX16_NIL)
            return false;
        xfsapages_t::llnode_t* const ppage = pages->indexto_node(page);
        return ppage->m_next != INDEX16_NIL || ppage->m_prev != INDEX16_NIL;
    }

    static inline void insert_in_list(xfsapages_t* pages, u16& head, u16 page)
    {
        // TODO: Sort the free pages by address !!
        if (head == INDEX16_NIL)
        {
            xfsapages_t::llnode_t* const ppage = pages->indexto_node(page);
            ppage->m_next                      = page;
            ppage->m_prev                      = page;
            head                               = page;
        }
        else
        {
            xfsapages_t::llnode_t* const phead = pages->indexto_node(head);
            xfsapages_t::llnode_t* const pnext = pages->indexto_node(phead->m_next);
            xfsapages_t::llnode_t* const ppage = pages->indexto_node(page);
            ppage->m_prev                      = head;
            ppage->m_next                      = phead->m_next;
            phead->m_next                      = page;
            pnext->m_prev                      = page;
        }
    }

    static inline void remove_from_list(xfsapages_t* pages, u16& head, u16 page)
    {
        xfsapages_t::llnode_t* const phead = pages->indexto_node(head);
        xfsapages_t::llnode_t* const ppage = pages->indexto_node(page);
        xfsapages_t::llnode_t* const pprev = pages->indexto_node(ppage->m_prev);
        xfsapages_t::llnode_t* const pnext = pages->indexto_node(ppage->m_next);
        pprev->m_next                      = ppage->m_next;
        pnext->m_prev                      = ppage->m_prev;

        ppage->m_next = INDEX16_NIL;
        ppage->m_prev = INDEX16_NIL;

        if (phead == ppage)
        {
            if (pnext == ppage)
            {
                head = INDEX16_NIL;
            }
            else
            {
                head = pages->indexof_node(pnext);
            }
        }
    }

    xfsapages_t* create(xalloc* main_allocator, void* base_address, u64 memory_range, u32 page_size)
    {
        ASSERT(main_allocator != nullptr);
        u32 const              page_cnt     = memory_range / page_size;
        xfsapage_t*            page_array   = (xfsapage_t*)main_allocator->allocate(sizeof(xfsapage_t) * page_cnt, sizeof(void*));
        xfsapages_t::llnode_t* llnode_array = (xfsapages_t::llnode_t*)main_allocator->allocate(sizeof(xfsapages_t::llnode_t) * page_cnt, sizeof(void*));
        xfsapages_t*           pages        = main_allocator->construct<xfsapages_t>(base_address, page_size, page_cnt, llnode_array, page_array);
        return pages;
    }

    void destroy(xalloc* main_allocator, xfsapages_t* pages)
    {
        main_allocator->deallocate(pages->m_page_list);
        main_allocator->deallocate(pages->m_pages);
        main_allocator->deallocate(pages);
    }

    void* alloc_page(xfsapages_t* pages, xfsapage_list_t& page_list, u32 const elem_size)
    {
        xfsapage_t* ppage = pages->alloc_page(elem_size);
        u16 const   ipage = pages->indexof_page(ppage);
        insert_in_list(pages, page_list.m_list, ipage);
        page_list.m_count += 1;
        return pages->address_of_page(ppage);
    }

    void* free_one_page(xfsapages_t* pages, xfsapage_list_t& page_list)
    {
        u16 const ipage = page_list.m_list;
        if (ipage == INDEX16_NIL)
            return nullptr;
        remove_from_list(pages, page_list.m_list, ipage);
        page_list.m_count -= 1;
        xfsapage_t* ppage = pages->indexto_page(ipage);
        void* const apage = pages->address_of_page(ppage);
        pages->free_page(ppage);
        return apage;
    }

    void free_all_pages(xfsapages_t* pages, xfsapage_list_t& page_list)
    {
        while (page_list.m_count > 0)
        {
            u16 const ipage = page_list.m_list;
            remove_from_list(pages, page_list.m_list, ipage);
            xfsapage_t* ppage = pages->indexto_page(ipage);
            pages->free_page(ppage);
            page_list.m_count -= 1;
        }
    }

    void* alloc_elem(xfsapages_t* pages, xfsapage_list_t& page_list, u32 const elem_size)
    {
        // If list is empty, request a new page and add it to the page_list
        // Using the page allocate a new element
        // return pointer to element
        // If page is full remove it from the list
        u16         ipage = INDEX16_NIL;
        xfsapage_t* ppage = nullptr;
        if (page_list.m_list == INDEX16_NIL)
        {
            ppage = pages->alloc_page(elem_size);
            ipage = pages->indexof_page(ppage);
            insert_in_list(pages, page_list.m_list, ipage);
            page_list.m_count += 1;
        }
        else
        {
            ipage = page_list.m_list;
            ppage = pages->indexto_page(ipage);
        }

        void* const apage = pages->address_of_page(ppage);
        void*       ptr   = nullptr;
        if (ppage != nullptr)
        {
            ptr = ppage->allocate(apage);
            if (ppage->is_full())
            {
                remove_from_list(pages, page_list.m_list, ipage);
                page_list.m_count -= 1;
            }
        }
        return ptr;
    }

    void free_elem(xfsapages_t* pages, xfsapage_list_t& page_list, void* const ptr, xfsapage_list_t& pages_empty_list)
    {
        // Find page that this pointer belongs to
        // Determine element index of this pointer
        // Add element to free element list of the page
        // When page is empty remove it from the free list and add it to the 'pages_empty_list'
        // When page was full then now add it back to the list of 'usable' pages
        xfsapage_t* ppage    = pages->address_to_page(ptr);
        u16 const   ipage    = pages->indexof_page(ppage);
        bool const  was_full = ppage->is_full();
        ppage->deallocate(pages->address_of_page(ppage), ptr);
        if (ppage->is_empty())
        {
            ASSERT(is_page_linked(pages, ipage));
            remove_from_list(pages, page_list.m_list, ipage);
            page_list.m_count -= 1;
            insert_in_list(pages, pages_empty_list.m_list, ipage);
            pages_empty_list.m_count += 1;
        }
        else if (was_full)
        {
            insert_in_list(pages, page_list.m_list, ipage);
            page_list.m_count += 1;
        }
    }

}; // namespace xcore
