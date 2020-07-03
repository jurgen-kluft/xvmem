#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_page_vcd_direct.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    // Virtual Commit and Decommit Directly:
    //    This allocator is a proxy and commits the pages of an allocation
    //    and decommits the pages of a deallocation.

    class xalloc_page_vcd_direct : public xalloc
    {
    public:
        virtual void* v_allocate(u32 size, u32 alignment) X_FINAL;
        virtual u32   v_deallocate(void* ptr) X_FINAL;
        virtual void  v_release() X_FINAL;

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        xalloc* m_main_heap; // Internal allocator to allocate ourselves and bookkeeping data from
        xalloc* m_allocator; // The allocator that does the allocations/deallocations
        xvmem*  m_vmem;      // Virtual memory interface
        u32     m_page_size; //
    };

    static inline void* advance_ptr(void* ptr, u64 size) { return (void*)((uptr)ptr + size); }

    void* xalloc_page_vcd_direct::v_allocate(u32 size, u32 alignment)
    {
        void*     ptr        = m_allocator->allocate(size, alignment);
        u32 const alloc_size = size; // size alignment ?

        u32 const num_pages = (alloc_size + (m_page_size - 1)) / m_page_size;
        m_vmem->commit(ptr, m_page_size, num_pages);

        return ptr;
    }

    u32 xalloc_page_vcd_direct::v_deallocate(void* ptr)
    {
        u32 const alloc_size = m_allocator->deallocate(ptr);

        u32 const num_pages = (alloc_size + (m_page_size - 1)) / m_page_size;
        m_vmem->decommit(ptr, m_page_size, num_pages);

        return alloc_size;
    }

    void xalloc_page_vcd_direct::v_release() { m_main_heap->destruct(this); }

    xalloc* create_page_vcd_direct(xalloc* main_heap, xalloc* allocator, xvmem* vmem, u32 page_size)
    {
        xalloc_page_vcd_direct* proxy = main_heap->construct<xalloc_page_vcd_direct>();

        proxy->m_main_heap = main_heap;
        proxy->m_allocator = allocator;
        proxy->m_vmem      = vmem;
        proxy->m_page_size = page_size;

        return proxy;
    }

} // namespace xcore