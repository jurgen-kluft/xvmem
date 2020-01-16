#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/x_virtual_allocator_small.h"
#include "xvmem/x_virtual_memory.h"
#include "xvmem/x_virtual_pages.h"

namespace xcore
{
    // ----------------------------------------------------------------------------------------------
    // ----------------------------------------------------------------------------------------------
    // ----------------------------------------------------------------------------------------------
    class xvfsa : public xfsa
    {
    public:
        inline xvfsa(xalloc* main_heap, xvpages_t* pages, u32 allocsize)
            : m_main_heap(main_heap)
            , m_pages(pages)
            , m_pages_list(xvpage_t::INDEX_NIL)
            , m_alloc_size(allocsize)
        {
        }

        virtual u32 size() const X_FINAL { return m_alloc_size; }

        virtual void* allocate() X_FINAL { return m_pages->allocate(m_pages_list, m_alloc_size); }
        virtual void  deallocate(void* ptr) X_FINAL { return m_pages->deallocate(m_pages_list, ptr); }

        virtual void release()
        {
            m_pages->free_pages(m_pages_list);
            m_main_heap->deallocate(this);
        }

        XCORE_CLASS_PLACEMENT_NEW_DELETE

    protected:
        xalloc*          m_main_heap;
        xvpages_t* const m_pages;
        u32              m_pages_list;
        u32 const        m_alloc_size;
    };

    xfsa* gCreateVMemBasedFsa(xalloc* main_allocator, xvpages_t* vpages, u32 allocsize)
    {
        xvfsa* fsa = main_allocator->construct<xvfsa>(main_allocator, vpages, allocsize);
        return fsa;
    }

    // ----------------------------------------------------------------------------------------------
    // ----------------------------------------------------------------------------------------------
    // ----------------------------------------------------------------------------------------------
    class xvfsa_dexed : public xfsadexed
    {
    public:
        inline xvfsa_dexed(xalloc* main_heap, xvpages_t* pages, u32 allocsize)
            : m_main_heap(main_heap)
            , m_pages(pages)
            , m_pages_notfull_list(xvpage_t::INDEX_NIL)
            , m_alloc_size(allocsize)
            , m_page_elem_cnt(pages->m_page_size / allocsize)
        {
        }

        virtual u32 size() const X_FINAL { return m_alloc_size; }

        virtual void* allocate() X_FINAL { return m_pages->allocate(m_pages_notfull_list, m_alloc_size); }
        virtual void  deallocate(void* ptr) X_FINAL { return m_pages->deallocate(m_pages_notfull_list, ptr); }

        virtual void* idx2ptr(u32 index) const X_FINAL { return m_pages->idx2ptr(index, m_page_elem_cnt, m_alloc_size); }
        virtual u32   ptr2idx(void* ptr) const X_FINAL { return m_pages->ptr2idx(ptr, m_page_elem_cnt, m_alloc_size); }

        virtual void release() { m_main_heap->deallocate(this); }

        XCORE_CLASS_PLACEMENT_NEW_DELETE

    protected:
        xalloc*          m_main_heap;
        xvpages_t* const m_pages;
        u32              m_pages_notfull_list;
        u32 const        m_alloc_size;
        u32 const        m_page_elem_cnt;
    };

    // Constraints:
    // - xvpages is not allowed to manage a memory range more than (4G * allocsize) (32-bit indices)
    xfsadexed* gCreateVMemBasedDexedFsa(xalloc* main_allocator, xvpages_t* vpages, u32 allocsize)
    {
        ASSERT(vpages->memory_range() < ((u64)4 * 1024 * 1024 * 1024 * allocsize));
        xvfsa_dexed* fsa = main_allocator->construct<xvfsa_dexed>(main_allocator, vpages, allocsize);
        return fsa;
    }
}; // namespace xcore
