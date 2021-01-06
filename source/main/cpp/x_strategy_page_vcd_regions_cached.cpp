#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_page_vcd_regions.h"
#include "xvmem/private/x_doubly_linked_list.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    // Virtual Commit and Decommit of Regions:
    //    This allocator is a proxy and keeps track of regions of memory to
    //    be able to decommit pages back to the system.

    class xalloc_page_vcd_regions_cached : public alloc_t
    {
    public:
        xalloc_page_vcd_regions_cached();

        virtual void* v_allocate(u32 size, u32 alignment) X_FINAL;
        virtual u32   v_deallocate(void* ptr) X_FINAL;
        virtual void  v_release() X_FINAL;

        void commit_region(void* reg_base, u32 region_index, u32 num_regions)
        {
            bool const region_1st_is_cached = m_regions_list[region_index].is_linked();
            if (num_regions == 1)
            {
                if (!region_1st_is_cached)
                {
                    m_vmem->commit(reg_base, m_page_size, (u32)((num_regions * m_reg_range) / m_page_size));
                }
                else
                {
                    m_regions_cache.remove_item(sizeof(llnode_t), m_regions_list, region_index);
                }
            }
            else
            {
                ASSERT(num_regions == 2);
                bool const region_2nd_is_cached = m_regions_list[region_index + 1].is_linked();
                if (region_1st_is_cached && region_2nd_is_cached)
                {
                    m_regions_cache.remove_item(sizeof(llnode_t), m_regions_list, region_index);
                    m_regions_cache.remove_item(sizeof(llnode_t), m_regions_list, region_index + 1);
                }
                else if (region_1st_is_cached && !region_2nd_is_cached)
                {
                    m_regions_cache.remove_item(sizeof(llnode_t), m_regions_list, region_index);
                    m_vmem->commit(x_advance_ptr(reg_base, m_reg_range), m_page_size, (u32)(m_reg_range / m_page_size));
                }
                else if (!region_1st_is_cached && region_2nd_is_cached)
                {
                    m_vmem->commit(reg_base, m_page_size, (u32)(m_reg_range / m_page_size));
                    m_regions_cache.remove_item(sizeof(llnode_t), m_regions_list, region_index + 1);
                }
            }
        }

        void decommit_region(void* reg_base, u32 region_index, u32 num_regions)
        {
            // Check to see if we can add it to the cache
            // If the cache is holding too many regions then decommit the oldest
            // Add this region to the cache
            for (u32 i = 0; i < num_regions; ++i)
            {
				u32 const region = region_index + i;
                m_regions_cache.insert_tail(sizeof(llnode_t), m_regions_list, region);
            }

            // Is cache too large?  ->  decommit the oldest
            while (m_regions_cache.size() > m_max_regions_cached)
            {
                llnode_t*       pregion  = m_regions_cache.remove_head(sizeof(llnode_t), m_regions_list);
                llindex_t const iregion  = m_regions_cache.node2idx(sizeof(llnode_t), m_regions_list, pregion);
                void*           reg_base = x_advance_ptr(m_mem_base, iregion * m_reg_range);
                m_vmem->decommit(reg_base, m_page_size, (u32)(m_reg_range / m_page_size));
            }
        }

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        struct region_t
        {
            u16 m_counter;
        };

        alloc_t*           m_main_heap;          // Internal allocator to allocate ourselves and bookkeeping data from
        alloc_t*           m_allocator;          // The allocator that does the allocations/deallocations
        xvmem*            m_vmem;               // Virtual memory interface
        u32               m_page_size;          //
        void*             m_mem_base;           // Memory base pointer
        u64               m_mem_range;          // Memory range
        u64               m_reg_range;          // Memory range of a region
        u32               m_num_regions;        // Number of regions
        u32               m_max_regions_cached; // Number of regions to cache (maximum)
        region_t*         m_regions;            // The array of regions
        llist_t           m_regions_cache;      // We do not immediatly decommit a region, we add it to this list
        llnode_t*         m_regions_list;       // Every region has a list node
    };

    xalloc_page_vcd_regions_cached::xalloc_page_vcd_regions_cached()
        : m_main_heap(nullptr)
        , m_allocator(nullptr)
        , m_vmem(nullptr)
        , m_page_size(0)
        , m_mem_base(nullptr)
        , m_mem_range(0)
        , m_reg_range(0)
        , m_num_regions(0)
        , m_regions(nullptr)
        , m_regions_cache()
        , m_regions_list(nullptr)
    {
    }

    void* xalloc_page_vcd_regions_cached::v_allocate(u32 size, u32 alignment)
    {
        void*     ptr        = m_allocator->allocate(size, alignment);
        u32 const alloc_size = size; // size alignment ?

        u32 const region_index_L = (u32)(((u64)ptr - (u64)m_mem_base) / m_reg_range);
        u16 const region_ref_L   = m_regions[region_index_L].m_counter;
        u32 const region_index_R = (u32)((((u64)ptr + alloc_size) - (u64)m_mem_base) / m_reg_range);
        u16 const region_ref_R   = m_regions[region_index_R].m_counter;

        ASSERT(region_index_L < m_num_regions);
        ASSERT(region_index_R < m_num_regions);

        if (region_index_L == region_index_R)
        {
            m_regions[region_index_L].m_counter += 1;
            if (region_ref_L == 0)
            {
                void* const region_mem_base = x_advance_ptr(m_mem_base, region_index_L * m_reg_range);
                commit_region(region_mem_base, region_index_L, 1);
            }
        }
        else
        {
            m_regions[region_index_L].m_counter += 1;
            m_regions[region_index_R].m_counter += 1;

            ASSERT((region_index_R - region_index_L) == 1);
            if (region_ref_L == 0 && region_ref_R != 0)
            {
                void* const region_mem_base = x_advance_ptr(m_mem_base, region_index_L * m_reg_range);
                commit_region(region_mem_base, region_index_L, 1);
            }
            else if (region_ref_L == 0 && region_ref_R == 0)
            {
                void* const region_mem_base = x_advance_ptr(m_mem_base, region_index_L * m_reg_range);
                commit_region(region_mem_base, region_index_L, 2);
            }
            else if (region_ref_L != 0 && region_ref_R == 0)
            {
                void* const region_mem_base = x_advance_ptr(m_mem_base, region_index_R * m_reg_range);
                u32 const   num_regions     = 1;
                commit_region(region_mem_base, region_index_R, 1);
            }
        }
        return ptr;
    }

    u32 xalloc_page_vcd_regions_cached::v_deallocate(void* ptr)
    {
        u32 const alloc_size = m_allocator->deallocate(ptr);

        u32 const region_index_L = (u32)(((u64)ptr - (u64)m_mem_base) / m_reg_range);
        u16 const region_ref_L   = m_regions[region_index_L].m_counter;
        u32 const region_index_R = (u32)((((u64)ptr + alloc_size) - (u64)m_mem_base) / m_reg_range);
        u16 const region_ref_R   = m_regions[region_index_R].m_counter;

        ASSERT(region_index_L < m_num_regions);
        ASSERT(region_index_R < m_num_regions);

        if (region_index_L == region_index_R)
        {
            m_regions[region_index_L].m_counter -= 1;
            if (region_ref_L == 1)
            {
                void* const region_mem_base = x_advance_ptr(m_mem_base, region_index_L * m_reg_range);
                decommit_region(region_mem_base, region_index_L, 1);
            }
        }
        else
        {
            m_regions[region_index_L].m_counter -= 1;
            m_regions[region_index_R].m_counter -= 1;

            ASSERT((region_index_R - region_index_L) == 1);
            if (region_ref_L == 1 && region_ref_R > 1)
            {
                void* const region_mem_base = x_advance_ptr(m_mem_base, region_index_L * m_reg_range);
                decommit_region(region_mem_base, region_index_L, 1);
            }
            else if (region_ref_L == 1 && region_ref_R == 1)
            {
                void* const region_mem_base = x_advance_ptr(m_mem_base, region_index_L * m_reg_range);
                decommit_region(region_mem_base, region_index_L, 2);
            }
            else if (region_ref_L > 1 && region_ref_R == 1)
            {
                void* const region_mem_base = x_advance_ptr(m_mem_base, region_index_R * m_reg_range);
                decommit_region(region_mem_base, region_index_R, 1);
            }
        }

        return alloc_size;
    }

    void xalloc_page_vcd_regions_cached::v_release()
    {
		while (m_regions_cache.is_empty() == false)
		{
            llnode_t*       pregion  = m_regions_cache.remove_head(sizeof(llnode_t), m_regions_list);
            llindex_t const iregion  = m_regions_cache.node2idx(sizeof(llnode_t), m_regions_list, pregion);
            void*           reg_base = x_advance_ptr(m_mem_base, iregion * m_reg_range);
            m_vmem->decommit(reg_base, m_page_size, (u32)(m_reg_range / m_page_size));
		}

        m_main_heap->deallocate(m_regions_list);
        m_main_heap->deallocate(m_regions);
        m_main_heap->destruct(this);
    }

    alloc_t* create_page_vcd_regions_cached(alloc_t* main_heap, alloc_t* allocator, xvmem* vmem, void* address_base, u64 address_range, u32 page_size, u32 region_size, u32 num_regions_to_cache)
    {
        xalloc_page_vcd_regions_cached* proxy = main_heap->construct<xalloc_page_vcd_regions_cached>();

        proxy->m_main_heap          = main_heap;
        proxy->m_allocator          = allocator;
        proxy->m_vmem               = vmem;
        proxy->m_page_size          = page_size;
        proxy->m_mem_base           = address_base;
        proxy->m_mem_range          = address_range;
        proxy->m_reg_range          = region_size;
        proxy->m_num_regions        = (u32)(address_range / region_size);
        proxy->m_max_regions_cached = num_regions_to_cache;
        proxy->m_regions            = (xalloc_page_vcd_regions_cached::region_t*)main_heap->allocate(sizeof(xalloc_page_vcd_regions_cached::region_t) * proxy->m_num_regions);
        proxy->m_regions_cache      = llist_t(0, proxy->m_num_regions);
        proxy->m_regions_list       = (llnode_t*)main_heap->allocate(sizeof(llnode_t) * proxy->m_num_regions);

        x_memclr(proxy->m_regions, sizeof(xalloc_page_vcd_regions_cached::region_t) * proxy->m_num_regions);
		x_memset(proxy->m_regions_list, 0xFFFFFFFF, sizeof(llnode_t) * proxy->m_num_regions);

        return proxy;
    }

} // namespace xcore
