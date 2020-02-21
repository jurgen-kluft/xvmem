#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/x_virtual_allocator_small.h"
#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_strategy_fsablock.h"

namespace xcore
{
    // ----------------------------------------------------------------------------------------------
    // ----------------------------------------------------------------------------------------------
    // ----------------------------------------------------------------------------------------------
    class xvfsa : public xfsa
    {
    public:
        inline xvfsa(xalloc* main_heap, xfsapages_t* pages, u32 allocsize)
            : m_main_heap(main_heap)
            , m_pages(pages)
            , m_pages_list()
            , m_alloc_size(allocsize)
        {
        }

        virtual u32 size() const X_FINAL { return m_alloc_size; }

        virtual void* allocate() X_FINAL { return alloc_elem(m_pages, m_pages_list, m_alloc_size); }
        virtual void  deallocate(void* ptr) X_FINAL { return free_elem(m_pages, m_pages_list, ptr, m_empty_pages_list); }

        virtual void release()
        {
			free_all_pages(m_pages, m_pages_list);
            free_all_pages(m_pages, m_empty_pages_list);
            m_main_heap->deallocate(this);
        }

        XCORE_CLASS_PLACEMENT_NEW_DELETE

    protected:
        xalloc*            m_main_heap;
        xfsapages_t* const m_pages;
        xfsapage_list_t    m_pages_list;
        xfsapage_list_t    m_empty_pages_list;
        u32 const          m_alloc_size;
    };

    xfsa* gCreateVMemBasedFsa(xalloc* main_allocator, xfsapages_t* vpages, u32 allocsize)
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
        inline xvfsa_dexed(xalloc* main_heap, xfsapages_t* pages, u32 allocsize)
            : m_main_heap(main_heap)
            , m_pages(pages)
            , m_pages_notfull_list()
            , m_alloc_size(allocsize)
        {
        }

        virtual u32 size() const X_FINAL { return m_alloc_size; }

        virtual void* allocate() X_FINAL { return alloc_elem(m_pages, m_pages_notfull_list, m_alloc_size); }
        virtual void  deallocate(void* ptr) X_FINAL { return free_elem(m_pages, m_pages_notfull_list, ptr, m_pages_empty_list); }

        virtual void* idx2ptr(u32 index) const X_FINAL { return ptr_of_elem(m_pages, index); }
        virtual u32   ptr2idx(void* ptr) const X_FINAL { return idx_of_elem(m_pages, ptr); }

        virtual void release() { m_main_heap->deallocate(this); }

        XCORE_CLASS_PLACEMENT_NEW_DELETE

    protected:
        xalloc*            m_main_heap;
        xfsapages_t* const m_pages;
        xfsapage_list_t    m_pages_notfull_list;
		xfsapage_list_t    m_pages_empty_list;
        u32 const          m_alloc_size;
    };

    // Constraints:
    // - xvpages is not allowed to manage a memory range more than (4G * allocsize) (32-bit indices)
    xfsadexed* gCreateVMemBasedDexedFsa(xalloc* main_allocator, xfsapages_t* vpages, u32 allocsize)
    {
        xvfsa_dexed* fsa = main_allocator->construct<xvfsa_dexed>(main_allocator, vpages, allocsize);
        return fsa;
    }
}; // namespace xcore
