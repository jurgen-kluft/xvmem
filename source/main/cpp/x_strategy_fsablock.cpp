#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_fsablock.h"

namespace xcore
{
    static inline u32 index_of_elem(u32 const elem_size, void* const page_base_address, void* const elem)
    {
        u32 const index = (u32)(((u64)elem - (u64)page_base_address) / elem_size);
        return index;
    }

    static inline u32* pointer_to_elem(u32 const elem_size, void* const page_base_address, u32 const index)
    {
        u32* elem = (u32*)((xbyte*)page_base_address + ((u64)index * (u64)elem_size));
        return elem;
    }
    static const u16 INDEX16_NIL = 0xffff;

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

    void* xfsapage_t::allocate(void* const block_base_address)
    {
        // if page == nullptr then first allocate a new page for this size
        // else use the page to allocate a new element.
        // Note: page should NOT be full when calling this function!
        if (m_free_list != INDEX16_NIL)
        {
            u32 const ielem = m_free_list;
            ASSERT(ielem < INDEX16_NIL);
            u32* const pelem = pointer_to_elem(m_elem_size, block_base_address, ielem);
            m_free_list      = pelem[0];
            m_elem_used++;
            return (void*)pelem;
        }
        else if (m_free_index < m_elem_total)
        {
            m_elem_used++;
            u32 const ielem = m_free_index++;
            ASSERT(ielem < INDEX16_NIL);
            u32* const pelem = pointer_to_elem(m_elem_size, block_base_address, ielem);
            return (void*)pelem;
        }
        else
        {
            return nullptr;
        }
    }

    void xfsapage_t::deallocate(void* const block_base_address, void* const ptr)
    {
        u32 const ielem = index_of_elem(m_elem_size, block_base_address, ptr);
        ASSERT(ielem < INDEX16_NIL);
        u32* const pelem = pointer_to_elem(m_elem_size, block_base_address, ielem);
        pelem[0]         = m_free_list;
        m_free_list      = ielem;
        m_elem_used -= 1;
    }

    struct xfsapages_t
    {
        xfsapages_t(u32 page_size, xfsapage_t* const pages, u16 const cnt)
            : m_page_cnt(cnt)
            , m_free_page_index(0)
            , m_free_page_count(0)
            , m_free_page_head(0)
            , m_pages(pages)
        {
        }

        xfsapage_t* alloc_page(u32 const elem_size);
        void        free_page(xfsapage_t* const ppage);
        void        free_page_list(u16& page_list);

        u32         address_to_elem_size(void* const base_address, void* const address) const;
        xfsapage_t* address_to_page(void* const base_address, void* const address) const;
        void*       get_page_address(void* const base_address, xfsapage_t* const page) const;

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

    void xfsapages_t::free_page(xfsapage_t* const ppage) {}

    void xfsapages_t::free_page_list(u16& page_list) {}

    xfsapages_t* create(xalloc* main_allocator, u64 memory_range, u32 page_size)
    {
        ASSERT(main_allocator != nullptr);
        return nullptr;
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
}; // namespace xcore
