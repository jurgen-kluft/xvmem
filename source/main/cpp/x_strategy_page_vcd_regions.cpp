#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_page_vcd_regions.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    // Virtual Commit and Decommit of Regions:
    //    This allocator is a proxy and keeps track of regions of memory to
    //    be able to decommit pages back to the system.

    class xalloc_page_vcd_regions : public alloc_t
    {
    public:
        virtual void* v_allocate(u32 size, u32 alignment) X_FINAL;
        virtual u32   v_deallocate(void* ptr) X_FINAL;
        virtual void  v_release();

        void commit_region(void* reg_base, u32 num_regions) { m_vmem->commit(reg_base, m_page_size, (u32)((num_regions * m_reg_range) / m_page_size)); }
        void decommit_region(void* reg_base, u32 num_regions) { m_vmem->decommit(reg_base, m_page_size, (u32)((num_regions * m_reg_range) / m_page_size)); }

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        struct region_t
        {
            u16 m_counter;
        };

        alloc_t*   m_main_heap;   // Internal allocator to allocate ourselves and bookkeeping data from
        alloc_t*   m_allocator;   // The allocator that does the allocations/deallocations
        xvmem*    m_vmem;        // Virtual memory interface
        u32       m_page_size;   //
        void*     m_mem_base;    // Memory base pointer
        u64       m_mem_range;   // Memory range
        u64       m_reg_range;   // Memory range of a region
        u32       m_num_regions; //
        region_t* m_regions;     // The array of regions
    };

    void* xalloc_page_vcd_regions::v_allocate(u32 size, u32 alignment)
    {
        void*     ptr        = m_allocator->allocate(size, alignment);
        u32 const alloc_size = size; // size alignment ?

        u32 const region_index_L = (u32)(((u64)ptr - (u64)m_mem_base) / m_reg_range);
        u16 const region_ref_L   = m_regions[region_index_L].m_counter;
        m_regions[region_index_L].m_counter += 1;
        u32 const region_index_R = (u32)((((u64)ptr + alloc_size) - (u64)m_mem_base) / m_reg_range);
        u16 const region_ref_R   = m_regions[region_index_R].m_counter;

        ASSERT(region_index_L < m_num_regions);
        ASSERT(region_index_R < m_num_regions);

        if (region_index_L == region_index_R)
        {
            if (region_ref_L == 0)
            {
                void* const region_mem_base = x_advance_ptr(m_mem_base, region_index_L * m_reg_range);
                commit_region(region_mem_base, 1);
            }
        }
        else
        {
            m_regions[region_index_R].m_counter += 1;

            ASSERT((region_index_R - region_index_L) == 1);
            if (region_ref_L == 0 && region_ref_R != 0)
            {
                void* const region_mem_base = x_advance_ptr(m_mem_base, region_index_L * m_reg_range);
                commit_region(region_mem_base, 1);
            }
            else if (region_ref_L == 0 && region_ref_R == 0)
            {
                void* const region_mem_base = x_advance_ptr(m_mem_base, region_index_L * m_reg_range);
                commit_region(region_mem_base, 2);
            }
            else if (region_ref_L != 0 && region_ref_R == 0)
            {
                void* const region_mem_base = x_advance_ptr(m_mem_base, region_index_R * m_reg_range);
                u32 const   num_regions     = 1;
                commit_region(region_mem_base, 1);
            }
        }
        return ptr;
    }

    u32 xalloc_page_vcd_regions::v_deallocate(void* ptr)
    {
        u32 const alloc_size = m_allocator->deallocate(ptr);

        u32 const region_index_L = (u32)(((u64)ptr - (u64)m_mem_base) / m_reg_range);
        u16 const region_ref_L   = m_regions[region_index_L].m_counter;
        m_regions[region_index_L].m_counter -= 1;
        u32 const region_index_R = (u32)((((u64)ptr + alloc_size) - (u64)m_mem_base) / m_reg_range);
        u16 const region_ref_R   = m_regions[region_index_R].m_counter;

        ASSERT(region_index_L < m_num_regions);
        ASSERT(region_index_R < m_num_regions);

        if (region_index_L == region_index_R)
        {
            if (region_ref_L == 1)
            {
                void* const region_mem_base = x_advance_ptr(m_mem_base, region_index_L * m_reg_range);
                decommit_region(region_mem_base, 1);
            }
        }
        else
        {
            m_regions[region_index_R].m_counter -= 1;

            ASSERT((region_index_R - region_index_L) == 1);
            if (region_ref_L == 1 && region_ref_R > 1)
            {
                void* const region_mem_base = x_advance_ptr(m_mem_base, region_index_L * m_reg_range);
                decommit_region(region_mem_base, 1);
            }
            else if (region_ref_L == 1 && region_ref_R == 1)
            {
                void* const region_mem_base = x_advance_ptr(m_mem_base, region_index_L * m_reg_range);
                decommit_region(region_mem_base, 2);
            }
            else if (region_ref_L > 1 && region_ref_R == 1)
            {
                void* const region_mem_base = x_advance_ptr(m_mem_base, region_index_R * m_reg_range);
                decommit_region(region_mem_base, 1);
            }
        }

        return alloc_size;
    }

    void xalloc_page_vcd_regions::v_release()
    {
        m_main_heap->deallocate(m_regions);
        m_main_heap->destruct(this);
    }

    alloc_t* create_page_vcd_regions(alloc_t* main_heap, alloc_t* allocator, xvmem* vmem, void* address_base, u64 address_range, u32 page_size, u32 region_size)
    {
        xalloc_page_vcd_regions* proxy = main_heap->construct<xalloc_page_vcd_regions>();

        proxy->m_main_heap   = main_heap;
        proxy->m_allocator   = allocator;
        proxy->m_vmem        = vmem;
        proxy->m_page_size   = page_size;
        proxy->m_mem_base    = address_base;
        proxy->m_mem_range   = address_range;
        proxy->m_reg_range   = region_size;
        proxy->m_num_regions = (u32)(address_range / region_size);
        proxy->m_regions     = (xalloc_page_vcd_regions::region_t*)main_heap->allocate(sizeof(xalloc_page_vcd_regions::region_t) * proxy->m_num_regions);
		x_memclr(proxy->m_regions, sizeof(xalloc_page_vcd_regions::region_t) * proxy->m_num_regions);

        return proxy;
    }

} // namespace xcore
