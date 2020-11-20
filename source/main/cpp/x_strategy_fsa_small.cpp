#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_fsa_small.h"
#include "xvmem/private/x_strategy_fsa_pages.h"
#include "xvmem/private/x_doubly_linked_list.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    class xfsa_allocator : public xalloc
    {
    public:
        virtual void* v_allocate(u32 size, u32 alignment) X_FINAL
        {
            if (size < m_fvsa_min_size)
                size = m_fvsa_min_size;

            u32 const size_index  = (size - m_fvsa_max_size) / m_fvsa_step_size;
            u32 const alloc_index = m_fvsa_size_to_index[size_index];
            u32 const alloc_size  = m_fvsa_index_to_size[alloc_index];

            void* ptr = alloc_elem(m_fsa_pages, m_fvsa_pages_list[alloc_index], m_fsa_freepages_list, alloc_size);
			return ptr;
        }

        virtual u32 v_deallocate(void* ptr) X_FINAL
        {
            u32 const alloc_size  = sizeof_elem(m_fsa_pages, ptr);
            u32 const size_index  = (alloc_size - m_fvsa_max_size) / m_fvsa_step_size;
            u32 const alloc_index = m_fvsa_size_to_index[size_index];

            xalist_t& page_list = m_fvsa_pages_list[alloc_index];
            free_elem(m_fsa_pages, page_list, ptr, m_fsa_freepages_list);

			// Check the size of the 'freepages list' to see if we need to free any pages

            return alloc_size;
        }

        virtual void v_release()
        {
			free_all_pages(m_fsa_pages, m_fsa_freepages_list);
            for (u32 i = 0; i < m_fvsa_pages_list_size; i++)
            {
                free_all_pages(m_fsa_pages, m_fvsa_pages_list[i]);
            }
            destroy(m_fsa_pages);

            m_main_heap->deallocate(m_fvsa_size_to_index);
            m_main_heap->deallocate(m_fvsa_index_to_size);
            m_main_heap->deallocate(m_fvsa_pages_list);
            m_main_heap->deallocate(this);
        }

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        xalloc* m_main_heap;

        void*     m_fvsa_mem_base;  // A memory base pointer
        u64       m_fvsa_mem_range; // 1 GB
        u32       m_fvsa_min_size;
        u32       m_fvsa_step_size;
        u32       m_fvsa_max_size;
        u8*       m_fvsa_size_to_index;
        u16*      m_fvsa_index_to_size;
        u32       m_fvsa_pages_list_size;
        xalist_t* m_fvsa_pages_list; // N allocators
        u32       m_fsa_page_size;   // 64 KB
        xalist_t  m_fsa_freepages_list;
        xpages_t* m_fsa_pages;
    };

    static void populate_size2index(u8* array, s32 array_index, u8& allocator_index, u32 size, s32 count)
    {
        for (s32 te = 0; te < count; ++te)
        {
            array[array_index++] = allocator_index;
        }
    }

    struct size_range_t
    {
        u32 m_size_min;
        u32 m_size_max;
        u32 m_size_step;
    };

    xalloc* create_alloc_fsa(xalloc* main_heap, xvmem* vmem, u64 mem_range, void*& mem_base)
    {
        xfsa_allocator* fsa = main_heap->construct<xfsa_allocator>();
        fsa->m_main_heap    = main_heap;

        size_range_t const size_ranges[] = {
            {0, 64, 8}, {64, 512, 16}, {512, 1024, 64}, {1024, 2048, 128}, {2048, 4096, 256},
        };
        u32 const size_range_count = sizeof(size_ranges) / sizeof(size_range_t);
        u32 const min_step         = 8;
        u32 const min_size         = size_ranges[0].m_size_min + size_ranges[0].m_size_step;
        u32 const max_size         = size_ranges[size_range_count - 1].m_size_max;
        u32 const num_sizes        = max_size / min_step;

        fsa->m_fvsa_size_to_index = (u8*)main_heap->allocate(sizeof(u8) * num_sizes);
        for (u32 c = 0; c < num_sizes; ++c)
        {
            fsa->m_fvsa_size_to_index[c] = 0xff;
        }

        u32 num_allocators = 0;
        for (u32 c = 0; c < size_range_count; ++c)
        {
            size_range_t const& r = size_ranges[c];
            num_allocators += (r.m_size_max - r.m_size_min) / r.m_size_step;
        }

        ASSERT(num_allocators < 255); // 0xff means empty

        fsa->m_fvsa_index_to_size = (u16*)main_heap->allocate(sizeof(u16) * num_allocators);
        for (u32 c = 0; c < num_allocators; ++c)
        {
            fsa->m_fvsa_index_to_size[c] = 0xffff;
        }

        u8 allocator_index = 0;
        for (u32 c = 0; c < size_range_count; ++c)
        {
            size_range_t const& range = size_ranges[c];
            for (u32 size = range.m_size_min + range.m_size_step; size <= range.m_size_max;)
            {
                s32 const array_index                      = (size - min_size) / min_step;
                s32 const count                            = range.m_size_step / min_step;
                fsa->m_fvsa_size_to_index[array_index]     = allocator_index;
                fsa->m_fvsa_index_to_size[allocator_index] = size;
                ASSERT(allocator_index < num_allocators);
                allocator_index += 1;
                size += count * min_step;
            }
        }
        ASSERT(allocator_index == num_allocators);

        // TODO: Fixup the 'size_to_index' array
        u8 ia = 0xff;
        s32 ic = (s32)num_sizes - 1;
        for (; ic >= 0; ic--)
        {
            if (fsa->m_fvsa_size_to_index[ic] == 0xff)
            {
                ASSERT(ia != 0xff);
                fsa->m_fvsa_size_to_index[ic] = ia;
            }
            else
            {
                ia = fsa->m_fvsa_size_to_index[ic];
            }
        }

        fsa->m_fvsa_mem_range    = mem_range;
        fsa->m_fvsa_mem_base     = nullptr;
        u32       fvsa_page_size = 0;
        u32 const fvsa_mem_attrs = 0;
        vmem->reserve(fsa->m_fvsa_mem_range, fvsa_page_size, fvsa_mem_attrs, fsa->m_fvsa_mem_base);
        fsa->m_fsa_pages = create_fsa_pages(fsa->m_main_heap, fsa->m_fvsa_mem_base, fsa->m_fvsa_mem_range, fvsa_page_size);

        // every size has it's own 'used pages' list
        fsa->m_fvsa_min_size        = min_size;
        fsa->m_fvsa_step_size       = min_step;
        fsa->m_fvsa_max_size        = max_size;
        fsa->m_fvsa_pages_list_size = num_allocators;
        fsa->m_fvsa_pages_list      = (xalist_t*)fsa->m_main_heap->allocate(sizeof(xalist_t) * fsa->m_fvsa_pages_list_size, sizeof(void*));

        xalist_t const default_list = init_list(fsa->m_fsa_pages);
        for (u32 c = 0; c < fsa->m_fvsa_pages_list_size; ++c)
        {
            fsa->m_fvsa_pages_list[c] = default_list;
        }

        return fsa;
    }

}; // namespace xcore
