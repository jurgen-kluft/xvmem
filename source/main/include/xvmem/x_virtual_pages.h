#ifndef __X_VMEM_VIRTUAL_PAGES_H__
#define __X_VMEM_VIRTUAL_PAGES_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xbase/x_allocator.h"

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

        bool is_full() const { return m_elem_used == m_elem_total; }
        bool is_empty() const { return m_elem_used == 0; }
        bool is_physical() const { return (m_flags & PAGE_PHYSICAL) == PAGE_PHYSICAL; }
        bool is_virtual() const { return (m_flags & PAGE_VIRTUAL) == PAGE_VIRTUAL; }
        bool is_linked() const { return !(m_next == INDEX_NIL && m_prev == INDEX_NIL); }

        void set_is_virtual() { m_flags = (m_flags & ~(PAGE_PHYSICAL | PAGE_VIRTUAL)) | PAGE_VIRTUAL; }
        void set_is_physical() { m_flags = (m_flags & ~(PAGE_PHYSICAL | PAGE_VIRTUAL)) | PAGE_PHYSICAL; }

        XCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    class xvpages_t
    {
    public:
        xvpages_t(xvpage_t* page_array, u32 pagecount, void* memory_base, u64 memory_range, u32 pagesize);

        u64 memory_range() const;

        void* allocate(u32& freelist, u32 const allocsize);
        void  deallocate(u32& freelist, void* const ptr);

        xvpage_t* alloc_page(u32 const allocsize);
        void      free_page(xvpage_t* const ppage);
	    xvpage_t* find_page(void* const address) const;

        u32       address_to_allocsize(void* const address) const;
        xvpage_t* address_to_page(void* const address) const;
        void*     get_base_address(xvpage_t* const page) const;

        void* idx2ptr(u32 const index, u32 const page_elem_cnt, u32 const alloc_size) const;
        u32   ptr2idx(void* const ptr, u32 const page_elem_cnt, u32 const alloc_size) const;

        xvpage_t* next_page(xvpage_t* const page);
        xvpage_t* prev_page(xvpage_t* const page);
        xvpage_t* indexto_page(u32 const page) const;
        u32       indexof_page(xvpage_t* const ppage) const;

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        void* const     m_memory_base;
        u64 const       m_memory_range;
        u32 const       m_page_size;
        u32 const       m_page_total_cnt;
        u32             m_free_pages_physical_head;
        u32             m_free_pages_physical_count;
        u32             m_free_pages_virtual_head;
        u32             m_free_pages_virtual_count;
        xvpage_t* const m_page_array;
    };
} // namespace xcore

#endif // __X_VMEM_VIRTUAL_PAGES_H__