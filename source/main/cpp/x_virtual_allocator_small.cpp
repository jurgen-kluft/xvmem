#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/x_virtual_allocator_small.h"
#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_strategy_fsa_small.h"

namespace xcore
{
    // ----------------------------------------------------------------------------------------------
    // ----------------------------------------------------------------------------------------------
    // ----------------------------------------------------------------------------------------------
    class xvfsa : public xfsa
    {
    public:
        inline xvfsa(xalloc* main_heap, xfsastrat::xpages_t* pages, u32 allocsize)
            : m_main_heap(main_heap)
            , m_pages(pages)
            , m_pages_list()
            , m_alloc_size(allocsize)
        {
        }

        virtual u32 v_size() const X_FINAL { return m_alloc_size; }

        virtual void* v_allocate() X_FINAL { return alloc_elem(m_pages, m_pages_list, m_alloc_size); }
        virtual u32   v_deallocate(void* ptr) X_FINAL
        {
            free_elem(m_pages, m_pages_list, ptr, m_empty_pages_list);
            return m_alloc_size;
        }

        virtual void v_release()
        {
            free_all_pages(m_pages, m_pages_list);
            free_all_pages(m_pages, m_empty_pages_list);
            m_main_heap->deallocate(this);
        }

        XCORE_CLASS_PLACEMENT_NEW_DELETE

    protected:
        xalloc*                    m_main_heap;
        xfsastrat::xpages_t* const m_pages;
        xfsastrat::xlist_t         m_pages_list;
        xfsastrat::xlist_t         m_empty_pages_list;
        u32 const                  m_alloc_size;
    };

    xfsa* gCreateVMemBasedFsa(xalloc* main_allocator, xfsastrat::xpages_t* vpages, u32 allocsize)
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
        inline xvfsa_dexed(xalloc* main_heap, xfsastrat::xpages_t* pages, u32 allocsize)
            : m_main_heap(main_heap)
            , m_pages(pages)
            , m_pages_notfull_list()
            , m_alloc_size(allocsize)
        {
        }

        virtual u32 v_size() const X_FINAL { return m_alloc_size; }

        virtual void* v_allocate() X_FINAL { return alloc_elem(m_pages, m_pages_notfull_list, m_alloc_size); }
        virtual u32   v_deallocate(void* ptr) X_FINAL
        {
            free_elem(m_pages, m_pages_notfull_list, ptr, m_pages_empty_list);

            // Here we constrain the amount of empty pages to a fixed number
            // TODO: Currently hard-coded to '1'
            if (m_pages_empty_list.m_count > 1)
            {
                free_one_page(m_pages, m_pages_empty_list);
            }

            return m_alloc_size;
        }

        virtual void* v_idx2ptr(u32 index) const X_FINAL { return ptr_of_elem(m_pages, index); }
        virtual u32   v_ptr2idx(void* ptr) const X_FINAL { return idx_of_elem(m_pages, ptr); }

        virtual void v_release() { m_main_heap->deallocate(this); }

        XCORE_CLASS_PLACEMENT_NEW_DELETE

    protected:
        xalloc*                    m_main_heap;
        xfsastrat::xpages_t* const m_pages;
        xfsastrat::xlist_t         m_pages_notfull_list;
        xfsastrat::xlist_t         m_pages_empty_list;
        u32 const                  m_alloc_size;
    };

    // Constraints:
    // - xvpages is not allowed to manage a memory range more than (4G * allocsize) (32-bit indices)
    xfsadexed* gCreateVMemBasedDexedFsa(xalloc* main_allocator, xfsastrat::xpages_t* vpages, u32 allocsize)
    {
        xvfsa_dexed* fsa = main_allocator->construct<xvfsa_dexed>(main_allocator, vpages, allocsize);
        return fsa;
    }
}; // namespace xcore
