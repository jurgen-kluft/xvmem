#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/x_vfsa_allocator.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    struct xvpage_t
    {
        enum { INDEX_NIL = 0xffffffff, PAGE_PHYSICAL = 1, PAGE_VIRTUAL = 2, PAGE_EMPTY = 4, PAGE_FULL = 8 };

        u32       m_next;
        u32       m_prev;
        u32       m_free_list;
        u32       m_free_index;
        u32       m_elem_used;
        u32       m_elem_total;
        u16       m_elem_size;
        u16       m_flags;

        bool      is_full() const { return (m_flags & PAGE_FULL) == PAGE_FULL; }
        bool      is_empty() const { return (m_flags & PAGE_EMPTY) == PAGE_EMPTY; }
        bool      is_physical() const { return (m_flags & PAGE_PHYSICAL) == PAGE_PHYSICAL; }
        bool      is_virtual() const { return (m_flags & PAGE_VIRTUAL) == PAGE_VIRTUAL; }
    };

    void init_page(xvpage_t* page, u32 page_size, u32 element_size)
    {
        page->m_next = xvpage_t::INDEX_NIL;
        page->m_prev = xvpage_t::INDEX_NIL;
        page->m_flags = PAGE_PHYSICAL | PAGE_EMPTY;
        page->m_free_list = xvpage_t::INDEX_NIL;
        page->m_free_index = 0;
        page->m_elem_used = 0;
        page->m_elem_total = (u16)(page_size/element_size);
        page->m_elem_used = element_size;
    }

    struct xvpages_t
    {
    public:
        void        initialize(xvpage_t* pages, u32 count, void* memory_base, u64 memory_range);

        void*       allocate(xvpage_t*& page, u32 size);
        void        deallocate(void* ptr, xvpage_t*& page);

        xvpage_t*   alloc_page();
        void        free_page(xvpage_t* page);

        xvpage_t*   find_page(void* address) const;

        xvpage_t*   next_page(xvpage_t* page);
        xvpage_t*   prev_page(xvpage_t* page);

        u32         indexof_page(xvpage_t* page) const;

        void*       m_memory_base;
        u64         m_memory_range;
        u32         m_page_total_cnt;
        u32         m_page_list_physical_free;
        u32         m_page_list_virtual_free;
        xvpage_t*   m_page;
    };

    class xvfsa : public xfsa
    {
    public:
        inline          xvfsa(xvpages_t* pages) : m_pages(pages), m_pages_freelist(xvpage_t::INDEX32_NIL) {}

        virtual void*   allocate()
        {
            // If free list is empty, request a new page and add it to the freelist
            // Using the page allocate a new element
            // return pointer to element
            // If page is full remove it from the free list
        }

        virtual void    deallocate(void* p)
        {
            // Find page that this pointer belongs to
            // Determine element index of this pointer
            // Add element to free element list of the page
            // If page is now empty, decide to deallocate this page
            // When deallocating this page, remove it from the free list
        }

    private:
        xvpages_t*  m_pages;
        u32         m_pages_freelist;
    };

    void*   xvpages_t::allocate(xvpage_t*& page, u32 size)
    {
        // if page == nullptr then first allocate a new page for this size
        // else use the page to allocate a new element.
        // Note: page should NOT be full when calling this function!

    }

    void    xvpages_t::deallocate(void* ptr, xvpage_t*& page)
    {
        // Determine page that this pointer belongs to
        // Determine element index of @ptr in this page
        // Add this element to the free list
        // Decrement the elements used
        // If elements used == 0 then add this page to the fee list of physical pages

    }

};
