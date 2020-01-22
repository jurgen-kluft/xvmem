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

    struct xfsapage_t
    {
        enum
        {
            INDEX16_NIL = 0xffff,
        };

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
        if (m_free_list != xfsapage_t::INDEX16_NIL)
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

    struct xfspages_t
    {
        xfspages_t(u32 page_size, xfsapage_t* const pages, u16 const cnt)
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
        xfsapage_t* address_to_block(void* const base_address, void* const address) const;
        void*       get_block_address(void* const base_address, xfsapage_t* const block) const;

        void*       idx2ptr(u32 const index) const;
        u32         ptr2idx(void* const ptr) const;
        xfsapage_t* next_block(xfsapage_t* const block);
        xfsapage_t* prev_block(xfsapage_t* const block);
        xfsapage_t* indexto_block(u16 const block) const;
        u16         indexof_block(xfsapage_t* const block) const;

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        u32       m_page_size;
        u16       m_free_page_index;
        u16       m_free_page_count;
        u16       m_free_page_head;
        u16 const m_page_cnt;
        struct llnode_t
        {
            u16 m_prev;
            u16 m_next;
        };
        llnode_t*   m_page_list;
        xfsapage_t* m_pages;
    };

    xfsapage_t* xfsapages_t::alloc_page(u32 const elem_size)
    {
        // Get a page from list of physical pages
        // If there are no free physical pages then take one from the list of
        // virtual pages and commit the page.
        // If there are also no free virtual pages then we are out-of-memory!
        xfsapage_t* ppage = nullptr;
        if (m_free_page_head != xvpage_t::INDEX16_NIL)
        {
            // Get the page pointer and remove it from the list of virtual pages
            ppage = indexto_page(m_free_page_head);
            remove_from_list(this, m_free_page_head, m_free_page_head);

            // Commit the virtual memory to physical memory
            void* address = get_base_address(ppage);
            m_vmem->commit(address, m_page_size, 1);
        }
        else if (m_free_pages_index < m_page_total_cnt)
        {
            ppage = indexto_page(m_free_pages_index);
            ppage->init();
            m_free_pages_index += 1;

            // Commit the virtual memory to physical memory
            void* address = get_base_address(ppage);
            m_vmem->commit(address, m_page_size, 1);
        }
        else
        {
            // All pages are used
            return nullptr;
        }

        // Page is committed, so it is physical, mark it
        ppage->set_is_physical();

        // Initialize page with 'size' (alloc size)
        init_page(ppage, m_page_size, allocsize);

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

}; // namespace xcore
