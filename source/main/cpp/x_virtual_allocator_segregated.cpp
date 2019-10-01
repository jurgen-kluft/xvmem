#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_btree.h"

#include "xvmem/x_virtual_allocator.h"
#include "xvmem/x_virtual_pages.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    //   64 KB - 16384 allocations
    //  128 KB - 8192 allocations
    //  192 KB - 5461 allocations
    //  256 KB - 4096 allocations
    //  320 KB - 3276 allocations
    //  384 KB - 2730 allocations
    //  448 KB - 2340 allocations
    //  512 KB - 2048 allocations
    //  576 KB - 1820 allocations
    //  640 KB - 1638 allocations
    //  704 KB - 1489 allocations
    //  768 KB - 1365 allocations
    //  832 KB - 1260 allocations
    //  894 KB - 1170 allocations
    //  958 KB - 1092 allocations
    // 1024 KB - 1024 allocations
    // Total 16 * 1 GB = 16 GB

    // 1280 KB
    // 1536 KB
    // 1792 KB
    // 2048 KB
    // 2304 KB
    // 2560 KB
    // 2816 KB
    // 3072 KB
    // ..
    // 32 MB
    // Total 128 * 1

    class xvmem_allocator_segregated : public xalloc
    {
    public:
        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);
        virtual void  release();

        u32       m_allocsize_min;
        u32       m_allocsize_max;
        u32       m_allocsize_step;
        xfsalloc* m_alloc;

        struct nsize_t
        {
            u32 m_page;
            u32 m_size;
            u32 m_next;
            u32 m_prev;
        };

        struct naddr_t
        {
            u32 m_page;
            u32 m_flags;
            u32 m_next;
            u32 m_prev;
        };

        struct level_t
        {
            void* m_base_address;
            u64   m_memory_range;
            u32   m_size_btree;
            u32   m_addr_btree;
        };

        level_t* m_levels;
    };

}; // namespace xcore
