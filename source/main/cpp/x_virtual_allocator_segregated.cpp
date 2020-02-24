#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_strategy_segregated.h"
#include "xvmem/private/x_binarysearch_tree.h"

namespace xcore
{
    class xvmem_allocator_segregated : public xalloc
    {
    public:
        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);
        virtual void  release();

        void initialize(xalloc* main_alloc, xfsadexed* node_alloc, void* vmem_address, u64 vmem_space, u64 level_range, u32 allocsize_min, u32 allocsize_max, u32 allocsize_step, u32 pagesize);

        xsegregatedstrat::xinstance_t* m_segregated;
    };

    void xvmem_allocator_segregated::initialize(xalloc* main_alloc, xfsadexed* node_alloc, void* vmem_address, u64 vmem_space, u64 level_range, u32 allocsize_min, u32 allocsize_max, u32 allocsize_step, u32 pagesize)
    {
        m_segregated = xsegregatedstrat::create(main_alloc, node_alloc, vmem_address, vmem_space, level_range, allocsize_min, allocsize_max, allocsize_step, pagesize);
    }

    void* xvmem_allocator_segregated::allocate(u32 size, u32 alignment)
    {
        void* ptr = xsegregatedstrat::allocate(m_segregated, size, alignment);
        return ptr;
    }

    void xvmem_allocator_segregated::deallocate(void* ptr) { xsegregatedstrat::deallocate(m_segregated, ptr); }
    void xvmem_allocator_segregated::release() { xsegregatedstrat::destroy(m_segregated); }

}; // namespace xcore
