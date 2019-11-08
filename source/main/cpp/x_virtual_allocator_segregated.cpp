#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_btree.h"

#include "xvmem/x_virtual_allocator.h"
#include "xvmem/x_virtual_allocator_small.h"
#include "xvmem/x_virtual_pages.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
	// ---------------------------------------
	// Example 1:

    //   64 KB <= __ < 128 KB
    //  128 KB <= __ < 192 KB
    //  192 KB <= __ < 256 KB
    //  256 KB <= __ < 320 KB
    //  320 KB <= __ < 384 KB
    //  384 KB <= __ < 448 KB
    //  448 KB <= __ < 512 KB
    //  512 KB <= __ < 576 KB
    //  576 KB <= __ < 640 KB
    //  640 KB <= __ < 704 KB
    //  704 KB <= __ < 768 KB
    //  768 KB <= __ < 832 KB
    //  832 KB <= __ < 894 KB
    //  894 KB <= __ < 958 KB
    //  958 KB <= __ < 1024 KB
    // Total 16 * 1 GB = 16 GB

	// ---------------------------------------
	// Example 2:

    // 1 MB >= __ <= 2 MB (2 MB blocks)
    // 2 MB >= __ <= 3 MB (2 MB blocks)
    // ..
    // 30 MB >= __ <= 31 MB (2 MB blocks)
    // 31 MB >= __ <= 32 MB (2 MB blocks)

    // Total 32 * 10 GB

    class xvmem_allocator_segregated : public xalloc
    {
    public:
        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);
        virtual void  release();

        struct region_t
        {
            u64   m_alloc_occupancy;
            u8    m_alloc_pagecnt[64];  // Number of pages that are NOT committed at the tail of the allocation (always < 1MB)
        };

        // Every level is cut up into regions.
        // For example with a 10 GB address space:
        // - [1 MB,2 MB] allocation range it will allocate 10 GB / (2MB * 64) = 80 regions
        // - [8 MB,10 MB] allocation range it will allocate 10 GB / (10MB * 64) = 16 regions
        struct level_t
        {
            void*     m_base_address;
            u32       m_alloc_size_min;
            u32       m_alloc_size_max;
            u32       m_region_size;   // e.g.1 MB <= X <= 2 MB
            u32       m_region_cnt;
            region_t* m_regions;
        };


        u32       m_allocsize_min;
        u32       m_allocsize_max;
        u32       m_allocsize_step;
        xvmem*    m_vmem;
        xfsa*     m_alloc;
        level_t* m_levels;
    };

}; // namespace xcore
