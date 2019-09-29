#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/x_vfsa_allocator.h"
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
        inline xvfsa(xvpages_t* pages, u32 allocsize)
            : m_pages(pages)
            , m_pages_freelist(xvpage_t::INDEX_NIL)
            , m_alloc_size(allocsize)
        {
        }

        virtual u32 size() const { return m_alloc_size; }

        virtual void* allocate() { return m_pages->allocate(m_pages_freelist, m_alloc_size); }
        virtual void  deallocate(void* ptr) { return m_pages->deallocate(m_pages_freelist, ptr); }

        virtual void release() {}

        XCORE_CLASS_PLACEMENT_NEW_DELETE
    protected:
        xvpages_t* const m_pages;
        u32              m_pages_freelist;
        u32 const        m_alloc_size;
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
        inline xvfsa_dexed(xvpages_t* pages, u32 allocsize)
            : m_pages(pages)
            , m_pages_freelist(xvpage_t::INDEX_NIL)
            , m_alloc_size(allocsize)
            , m_page_elem_cnt(pages->m_page_size / allocsize)
        {
        }

        virtual u32 size() const { return m_alloc_size; }

        virtual void* allocate() { return m_pages->allocate(m_pages_freelist, m_alloc_size); }

        virtual void deallocate(void* ptr) { return m_pages->deallocate(m_pages_freelist, ptr); }

        virtual void* idx2ptr(u32 index) const { return m_pages->idx2ptr(index, m_page_elem_cnt, m_alloc_size); }

        virtual u32 ptr2idx(void* ptr) const
        {
            u64 index = m_pages->ptr2idx(ptr, m_page_elem_cnt, m_alloc_size);
            return (u32)index;
        }

        virtual void release() {}

        XCORE_CLASS_PLACEMENT_NEW_DELETE
    protected:
        xvpages_t* const m_pages;
        u32              m_pages_freelist;
        u32 const        m_alloc_size;
        u32 const        m_page_elem_cnt;
    };

    // Constraints:
    // - xvpages is not allowed to manage a memory range more than (4G * allocsize) (32-bit indices)
    xfsadexed* gCreateVMemBasedDexedFsa(xalloc* main_allocator, xvpages_t* vpages, u32 allocsize)
    {
        ASSERT(vpages->memory_range() <= ((u64)4 * 1024 * 1024 * 1024 * allocsize));
        xvfsa_dexed* fsa = main_allocator->construct<xvfsa_dexed>(vpages, allocsize);
        return fsa;
    }

}; // namespace xcore
