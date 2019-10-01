#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_btree.h"

#include "xvmem/x_virtual_allocator.h"
#include "xvmem/x_virtual_pages.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    // Allocations are aligned to page-size
    // Allocations are commiting pages
    // Deallocations decommit pages
    //

    class xvmem_allocator_large : public xalloc
    {
    public:
        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);
        virtual void  release();

        struct allocation_t
        {
            void* m_address;
            u64   m_size;
        };

        xalloc*       m_main_allocator;
        void*         m_memory_base;
        u64           m_memory_range;
        u64           m_allocsize_min;
        allocation_t* m_allocations;
        u32           m_alloc_read;
        u32           m_alloc_write;
    };

}; // namespace xcore
