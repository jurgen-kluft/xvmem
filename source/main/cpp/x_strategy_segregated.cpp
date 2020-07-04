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
    class xalloc_segregated : public xalloc
    {
    public:
        virtual void* v_allocate(u32 size, u32 alignment) X_FINAL;
        virtual u32   v_deallocate(void* ptr) X_FINAL;
        virtual void  v_release() X_FINAL;

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        xalloc*  m_main_heap;
        void*    m_mem_base;
        u64      m_mem_range;
        u32      m_alloc_size_min;
        u32      m_alloc_size_max;
        u32      m_alloc_size_align;
        u32      m_alloc_count;
        xalloc** m_allocators;
    };

    static inline void* advance_ptr(void* ptr, u64 size) { return (void*)((uptr)ptr + size); }

    xalloc* create_alloc_segregated(xalloc* main_heap, xfsa* node_heap, void* mem_address, u64 mem_range, u32 allocsize_min, u32 allocsize_max, u32 allocsize_align)
    {
        ASSERT(xispo2(allocsize_min) && xispo2(allocsize_max) && xispo2(allocsize_align));

        xalloc_segregated* self = main_heap->construct<xalloc_segregated>();
        self->m_main_heap       = main_heap;
        self->m_mem_base        = mem_address;
        self->m_mem_range       = mem_range;

        // Allocators do not include the minimum value (except for the first allocator (FSA))
        // We only include the most maximum allocation size. So here we align allocsize_min
        // to a power-of-2 value and take the next po2 value.
        self->m_alloc_size_min   = xfloorpo2(allocsize_min) << 1;
        self->m_alloc_size_max   = allocsize_max;
        self->m_alloc_size_align = allocsize_align;

        self->m_alloc_count = xilog2(allocsize_max) - xilog2(allocsize_min);
        self->m_allocators  = (xalloc**)main_heap->allocate(sizeof(xalloc*) * self->m_alloc_count);

        void*     fsa_mem_base  = mem_address;
        u64 const fsa_mem_range = mem_range / self->m_alloc_count;
        u32       fsa_size      = allocsize_min << 1;
        for (u32 i = 0; i < self->m_alloc_count; ++i)
        {
            self->m_allocators[i] = create_alloc_fsa_large(main_heap, node_heap, fsa_mem_base, fsa_mem_range, allocsize_align, fsa_size);
            fsa_mem_base          = advance_ptr(fsa_mem_base, fsa_mem_range);
            fsa_size              = fsa_size << 1;
        }

        return self;
    }

    void xalloc_segregated::v_release()
    {
        for (u32 i = 0; i < m_alloc_count; ++i)
        {
            m_allocators[i]->release();
            m_allocators[i] = nullptr;
        }
        m_main_heap->deallocate(m_allocators);
        m_main_heap->deallocate(this);
    }

    void* xalloc_segregated::v_allocate(u32 size, u32 alignment)
    {
        ASSERT(size <= m_alloc_size_max);
        if (size < m_alloc_size_min)
            size = m_alloc_size_min;

        u32 const index = xilog2(size) - xilog2(m_alloc_size_min);
        void*     ptr   = m_allocators[index]->allocate(size, alignment);

        return ptr;
    }

    u32 xalloc_segregated::v_deallocate(void* ptr)
    {
        const u32 index = (u32)(((u64)ptr - (u64)m_mem_base) / (m_mem_range / m_alloc_count));
        const u32 size  = m_allocators[index]->deallocate(ptr);

        return size;
    }

} // namespace xcore