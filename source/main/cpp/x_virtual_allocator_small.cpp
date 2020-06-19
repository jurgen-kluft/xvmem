#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/x_virtual_allocator_small.h"
#include "xvmem/x_virtual_memory.h"

#include "xvmem/private/x_strategy_fsa_small.h"
#include "xvmem/private/x_strategy_fsa_pages.h"
#include "xvmem/private/x_doubly_linked_list.h"

namespace xcore
{
    // ----------------------------------------------------------------------------------------------
    // ----------------------------------------------------------------------------------------------
    // ----------------------------------------------------------------------------------------------
    class xvfsa_dexed : public xfsadexed
    {
    public:
        inline xvfsa_dexed(xalloc* main_heap, xpages_t* pages, u32 allocsize)
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
        xalloc* const   m_main_heap;
        xpages_t* const m_pages;
        xalist_t        m_pages_notfull_list;
        xalist_t        m_pages_empty_list;
        u32 const       m_alloc_size;
    };


    class xvfsa_dexed_own_pages : public xvfsa_dexed
    {
    public:
        inline xvfsa_dexed_own_pages(xalloc* main_heap, xpages_t* pages, xvmem* vmem, void* mem_base, u64 mem_range, u32 allocsize)
            : xvfsa_dexed(main_heap, pages, allocsize)
            , m_pages_owned(pages)
			, m_vmem(vmem)
			, m_mem_base(mem_base)
			, m_mem_range(mem_range)
        {
        }

        virtual void v_release() 
		{
			// release pages and vmem
			destroy(m_pages_owned);
			m_vmem->release(m_mem_base, m_mem_range);
			m_main_heap->deallocate(this);
		}

        XCORE_CLASS_PLACEMENT_NEW_DELETE

    protected:
        xpages_t*		m_pages_owned;
		xvmem*			m_vmem;
		u64             m_mem_range;
		void*           m_mem_base;
    };

    // Constraints:
    // - xvpages is not allowed to manage a memory range more than (4G * allocsize) (32-bit indices)
    xfsadexed* gCreateVMemBasedDexedFsa(xalloc* main_heap, xpages_t* vpages, u32 allocsize)
    {
        xvfsa_dexed* fsa = main_heap->construct<xvfsa_dexed>(main_heap, vpages, allocsize);
        return fsa;
    }

	xfsadexed* gCreateVMemBasedDexedFsa(xalloc* main_heap, xvmem* vmem, u64 mem_range, u32 allocsize)
	{
		// create pages
        void* mem_base      = nullptr;
        u32   page_size     = 0;
        u32 const mem_attrs = 0;
        vmem->reserve(mem_range, page_size, mem_attrs, mem_base);
        xpages_t* vpages = create_fsa_pages(main_heap, mem_base, mem_range, page_size);

        xvfsa_dexed* fsa = main_heap->construct<xvfsa_dexed_own_pages>(main_heap, vpages, vmem, mem_base, mem_range, allocsize);
        return fsa;
	}

}; // namespace xcore
