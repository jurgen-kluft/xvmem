#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_btree.h"

#include "xvmem/x_virtual_main_allocator.h"
#include "xvmem/private/x_strategy_fsa_small.h"
#include "xvmem/private/x_strategy_fsa_large.h"
#include "xvmem/private/x_strategy_coalesce_direct.h"
#include "xvmem/private/x_strategy_segregated.h"
#include "xvmem/private/x_strategy_large.h"
#include "xvmem/private/x_strategy_page_vcd_direct.h"
#include "xvmem/private/x_strategy_page_vcd_regions.h"
#include "xvmem/private/x_strategy_page_vcd_regions_cached.h"
#include "xvmem/x_virtual_allocator_small.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    class xvmem_allocator : public xalloc
    {
    public:
        void init(xalloc* heap_allocator, xvmem* vmem, xvmem_config const* const cfg);

        virtual void* v_allocate(u32 size, u32 align);
        virtual u32   v_deallocate(void* ptr);
        virtual void  v_release();

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        xalloc*    m_main_heap;            // Internal heap
        xfsadexed* m_node16_heap;          // 16 B node heap
        xfsadexed* m_node32_heap;          // 16 B node heap
        void*      m_fsa_mem_base;         //
        u64        m_fsa_mem_range;        // A memory base pointer
        u32        m_fsa_min_size;         // 8 B
        u32        m_fsa_max_size;         // 4096 B
        xalloc*    m_fsa_allocator;        //
        s32        m_med_count;            //
        void*      m_med_mem_base[2];      // A memory base pointer
        u32        m_med_mem_range[2];     // 768 MB
        u32        m_med_min_size[2];      // 4 KB
        u32        m_med_step_size[2];     // 256 B (size alignment)
        u32        m_med_max_size[2];      // 64 KB
        xalloc*    m_med_allocator[2];     //
        xalloc*    m_med_allocator_vcd[2]; // Virtual Commit/Decommit
        u32        m_seg_min_size;         // 640 KB
        u32        m_seg_max_size;         // 32 MB
        u32        m_seg_step_size;        // 1 MB
        void*      m_seg_mem_base;         // A memory base pointer
        u64        m_seg_mem_range;        // 128 GB
        xalloc*    m_seg_allocator;        //
        xalloc*    m_seg_allocator_vcd;    //
        u32        m_large_min_size;       // 32MB
        void*      m_large_mem_base;       // A memory base pointer
        u64        m_large_mem_range;      //
        xalloc*    m_large_allocator;      //
        xalloc*    m_large_allocator_vcd;  //
    };

    void* xvmem_allocator::v_allocate(u32 size, u32 align)
    {
        // If the size request is less than the alignment request we will adjust
        // size to at least be the same size as the alignment request.
        if (align > size)
            size = align;

        // TODO: Handle large alignments

        if (size <= m_fsa_max_size)
        {
            if (size < m_fsa_min_size)
                size = m_fsa_min_size;
            return m_fsa_allocator->allocate(size, align);
        }

		// Medium size; Direct Coalesce Allocators
		for (s32 i=0; i<m_med_count; ++i)
		{
			if (size <= m_med_max_size[i])
			{
				if (size < m_med_min_size[i])
					size = m_med_min_size[i];
				return m_med_allocator_vcd[i]->allocate(size, align);
			}
		}

		// Segregated
        if (size <= m_seg_max_size)
        {
            if (size < m_seg_min_size)
                size = m_seg_min_size;
			return m_seg_allocator_vcd->allocate(size, align);
		}

        return m_large_allocator_vcd->allocate(size, align);
    }

    // Helper function to determine if a pointer is inside a certain memory range
    static inline bool helper_is_in_memrange(void* const base, u64 const range, void* const ptr)
    {
        xbyte const* const begin = (xbyte const*)base;
        xbyte const* const end   = begin + range;
        xbyte const* const p     = (xbyte const*)ptr;
        return p >= begin && p < end;
    }

    u32 xvmem_allocator::v_deallocate(void* ptr)
    {
        u32 alloc_size = 0;
        if (helper_is_in_memrange(m_fsa_mem_base, m_fsa_mem_range, ptr))
        {
            alloc_size = m_fsa_allocator->deallocate(ptr);
            ASSERT(alloc_size >= m_fsa_min_size && alloc_size <= m_fsa_max_size);
        }
        else if (helper_is_in_memrange(m_med_mem_base[0], m_med_mem_range[0], ptr))
        {
            alloc_size = m_med_allocator_vcd[0]->deallocate(ptr);
        }
        else if (helper_is_in_memrange(m_med_mem_base[1], m_med_mem_range[1], ptr))
        {
            alloc_size = m_med_allocator_vcd[1]->deallocate(ptr);
        }
        else if (helper_is_in_memrange(m_seg_mem_base, m_seg_mem_range, ptr))
        {
            alloc_size = m_seg_allocator_vcd->deallocate(ptr);
        }
        else if (helper_is_in_memrange(m_large_mem_base, m_large_mem_range, ptr))
        {
            alloc_size = m_large_allocator_vcd->deallocate(ptr);
        }
        else
        {
            ASSERTS(false, "error: deallocating an address that is not owned by this allocator!");
        }
        return alloc_size;
    }

    void xvmem_allocator::v_release()
    {
        m_large_allocator_vcd->release();
        m_seg_allocator_vcd->release();
		m_med_allocator_vcd[1]->release();
        m_med_allocator_vcd[0]->release();

        m_large_allocator->release();
        m_seg_allocator->release();
        m_med_allocator[1]->release();
        m_med_allocator[0]->release();
        m_fsa_allocator->release();

        // TODO: release all reserved virtual memory

        m_node16_heap->release();
        m_node32_heap->release();

		m_main_heap->destruct(this);
    }

    void xvmem_allocator::init(xalloc* main_heap, xvmem* vmem, xvmem_config const* const cfg)
    {
        m_main_heap = main_heap;

        m_fsa_mem_range = cfg->m_fsa_mem_range;
        m_fsa_allocator = create_alloc_fsa(main_heap, vmem, m_fsa_mem_range, m_fsa_mem_base);

        // We prefer to create a separate virtual memory based FSA allocator.
        m_node16_heap = gCreateVMemBasedDexedFsa(main_heap, vmem, cfg->m_node16_heap_size, 16);
        m_node32_heap = gCreateVMemBasedDexedFsa(main_heap, vmem, cfg->m_node32_heap_size, 32);

		m_med_count = cfg->m_med_count;
		for (s32 i=0; i<m_med_count; ++i)
		{
			m_med_mem_range[i] = cfg->m_med_mem_range[i];
			m_med_mem_base[i]  = nullptr;
			m_med_min_size[i]  = cfg->m_med_min_size[i];
			m_med_step_size[i] = cfg->m_med_step_size[i];
			m_med_max_size[i]  = cfg->m_med_max_size[i];
		}

        // Reserve physical memory for the medium size allocator(s)
		for (s32 i=0; i<m_med_count; ++i)
		{
			u32       med_page_size = 0;
			u32 const med_mem_attrs = 0; // Page/Memory attributes
			vmem->reserve(m_med_mem_range[i], med_page_size, med_mem_attrs, m_med_mem_base[i]);
			m_med_allocator[i]     = create_alloc_coalesce_direct(main_heap, m_node16_heap, m_med_mem_base[i], m_med_mem_range[i], m_med_min_size[i], m_med_max_size[i], m_med_step_size[i], cfg->m_med_addr_node_cnt[i]);
			m_med_allocator_vcd[i] = create_page_vcd_regions_cached(main_heap, m_med_allocator[i], vmem, m_med_mem_base[i], m_med_mem_range[i], med_page_size, cfg->m_med_region_size[i], cfg->m_med_region_cached[i]);
		}

        // Segregated allocator
        m_seg_min_size  = cfg->m_seg_min_size;
        m_seg_max_size  = cfg->m_seg_max_size;
        m_seg_mem_base  = nullptr;
        m_seg_mem_range = cfg->m_seg_mem_range;

        // Reserve virtual memory for the segregated allocator
        u32       seg_page_size = 0;
        u32 const seg_mem_attrs = 0; // Page/Memory attributes
        vmem->reserve(m_seg_mem_range, seg_page_size, seg_mem_attrs, m_seg_mem_base);
        m_seg_allocator     = create_alloc_segregated(main_heap, m_node32_heap, m_seg_mem_base, m_seg_mem_range, m_seg_min_size, m_seg_max_size, seg_page_size);
        m_seg_allocator_vcd = create_page_vcd_direct(main_heap, m_seg_allocator, vmem, seg_page_size);

        // Large allocator
        m_large_min_size          = cfg->m_large_min_size;
        m_large_mem_base          = nullptr; // A memory base pointer
        m_large_mem_range         = cfg->m_large_mem_range;
        u32       large_page_size = 0;
        u32 const large_mem_attrs = 0; // Page/Memory attributes
        vmem->reserve(m_large_mem_range, large_page_size, large_mem_attrs, m_large_mem_base);

        m_large_allocator     = create_alloc_large(main_heap, m_large_mem_base, m_large_mem_range, cfg->m_large_max_allocs);
        m_large_allocator_vcd = create_page_vcd_direct(main_heap, m_large_allocator, vmem, large_page_size);
    }

    xalloc* gCreateVmAllocator(xalloc* internal_allocator, xvmem* vmem, xvmem_config const* cfg)
    {
        xvmem_allocator* main_allocator = internal_allocator->construct<xvmem_allocator>();

        if (cfg == nullptr)
        {
            xvmem_config default_config;
            main_allocator->init(internal_allocator, vmem, &default_config);
        }
        else
        {
            main_allocator->init(internal_allocator, vmem, cfg);
        }
        return main_allocator;
    }
}; // namespace xcore
