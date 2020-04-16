#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"
#include "xbase/x_hibitset.h"

#include "xvmem/private/x_strategy_segregated.h"
#include "xvmem/private/x_strategy_fsa_large.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    namespace xsegregatedstrat
    {
        static inline void* advance_ptr(void* ptr, u64 size) { return (void*)((uptr)ptr + size); }

        struct xinstance_t
        {
            xalloc*                   m_main_heap;
            void*                     m_mem_base;
            u64                       m_mem_range;
            u32                       m_alloc_size_min;
            u32                       m_alloc_size_max;
            u32                       m_alloc_size_align;
            u32                       m_alloc_count;
            xfsa_large::xinstance_t** m_allocators;
        };

        xinstance_t* create(xalloc* main_heap, void* mem_address, u64 mem_range, u32 allocsize_min, u32 allocsize_max, u32 allocsize_align)
        {
            ASSERT(xispo2(allocsize_min) && xispo2(allocsize_max) && xispo2(allocsize_align));

            u32 const pagesize = 0;

            xinstance_t* self = (xinstance_t*)main_heap->allocate(sizeof(xinstance_t));
            self->m_main_heap = main_heap;
            self->m_mem_base  = mem_address;
            self->m_mem_range = mem_range;

            self->m_alloc_size_min   = allocsize_min;
            self->m_alloc_size_max   = allocsize_max;
            self->m_alloc_size_align = allocsize_align;

            self->m_alloc_count = 1 + xilog2(allocsize_max) - xilog2(allocsize_min);
            self->m_allocators  = (xfsa_large::xinstance_t**)main_heap->allocate(sizeof(xfsa_large::xinstance_t*) * self->m_alloc_count);

            void*     fsa_mem_base  = mem_address;
            u64 const fsa_mem_range = mem_range / self->m_alloc_count;
            u32       fsa_size      = allocsize_min;
            for (s32 i = 0; i < self->m_alloc_count; ++i)
            {
                self->m_allocators[i] = xfsa_large::create(main_heap, fsa_mem_base, fsa_mem_range, pagesize, fsa_size);
                fsa_mem_base          = advance_ptr(fsa_mem_base, fsa_mem_range);
                fsa_size              = fsa_size << 1;
            }

            return self;
        }

        void destroy(xinstance_t* self) {}

        void* allocate(xinstance_t* self, u32 size, u32 alignment)
        {
            ASSERT(size <= self->m_alloc_size_max);
            if (size < self->m_alloc_size_min)
                size = self->m_alloc_size_min;

            u32 const index = xilog2(size) - xilog2(self->m_alloc_size_min);
            void*     ptr   = xfsa_large::allocate(self->m_allocators[index], size, alignment);

            // TODO: Commit virtual memory to become physical memory

            return ptr;
        }

        u32 deallocate(xinstance_t* self, void* ptr)
        {
            const u32 index = ((u64)ptr - (u64)self->m_mem_base) / (self->m_mem_range / self->m_alloc_count);
            const u32 size  = xfsa_large::deallocate(self->m_allocators[index], ptr);

            // TODO: Decommit physical memory

            return size;
        }

    } // namespace xsegregatedstrat
} // namespace xcore