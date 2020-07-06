#ifndef __X_ALLOCATOR_VIRTUAL_ALLOCATOR_H__
#define __X_ALLOCATOR_VIRTUAL_ALLOCATOR_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    // Forward declares
    class xalloc;
    class xvmem;

    struct xvmem_config
    {
        static inline u32 KB(u32 value) { return value * (u32)1024; }
        static inline u32 MB(u32 value) { return value * (u32)1024 * (u32)1024; }

        static inline u64 MBx(u64 value) { return value * (u64)1024 * (u64)1024; }
        static inline u64 GBx(u64 value) { return value * (u64)1024 * (u64)1024 * (u64)1024; }

        xvmem_config()
        {
            m_node16_heap_size     = MB(16);   // Size of node heap for sizeof(node)==16
            m_node32_heap_size     = MB(32);   // Size of node heap for sizeof(node)==32
            m_fsa_mem_range        = MBx(512); // The virtual memory range for the FSA allocator
            m_med_count            = 2;        // For now, 2 direct coalesce allocators
            m_med_mem_range[0]     = MB(768);  //
            m_med_min_size[0]      = KB(4);    // Medium-Size-Allocator; Minimum allocation size
            m_med_step_size[0]     = 256;      // 4 KB % 256 % 64 KB
            m_med_max_size[0]      = KB(64);   // Medium-Size-Allocator; Maximum allocation size
            m_med_addr_node_cnt[0] = 4096;     // Medium-Size-Allocator; Number of address nodes to split the memory range
            m_med_region_size[0]   = MB(1);    // Medium-Size-Allocator; Size of commit/decommit regions
            m_med_region_cached[0] = 50;       // Keep 50 medium size regions cached
            m_med_mem_range[1]     = MB(512);  // MedLarge-Size-Allocator; The virtual memory range
            m_med_min_size[1]      = KB(64);   // MedLarge-Size-Allocator; Minimum allocation size
            m_med_step_size[1]     = KB(2);    // 64 KB % 2 KB % 512 KB
            m_med_max_size[1]      = KB(512);  // MedLarge-Size-Allocator; Maximum allocation size
            m_med_addr_node_cnt[1] = 512;      // MedLarge-Size-Allocator; Number of address nodes to split the memory range
            m_med_region_size[1]   = MB(8);    // MedLarge-Size-Allocator; Size of commit/decommit regions
            m_med_region_cached[1] = 8;        // Keep 8 medium-2-large regions cached
            m_seg_min_size         = KB(512);  //
            m_seg_max_size         = MB(32);   //
            m_seg_mem_range        = GBx(128); //
            m_large_min_size       = MB(32);   //
            m_large_mem_range      = GBx(128); //
            m_large_max_allocs     = 64;       // Maximum number of large allocations
        }

        bool validate(const char*& reason)
        {
            for (s32 i = 0; i < m_med_count; ++i)
            {
                if ((m_med_mem_range[i] / m_med_addr_node_cnt[i]) <= (m_med_max_size[i] + 1024))
                {
                    reason = "Memory range division should be larger than the maximum allocation size.";
                    return false;
                }
                if (((m_med_max_size[i] - m_med_min_size[i]) / m_med_step_size[i]) >= 255)
                {
                    reason = "Direct Coalesce Allocator can only handle a limitted amount (<255) of unique sizes.";
                    return false;
                }
            }
            reason = "success";
            return true;
        }

        u32 m_node16_heap_size;
        u32 m_node32_heap_size;
        u64 m_fsa_mem_range;
        s32 m_med_count;
        u32 m_med_mem_range[2];
        u32 m_med_min_size[2];
        u32 m_med_step_size[2];
        u32 m_med_max_size[2];
        u32 m_med_addr_node_cnt[2];
        u32 m_med_region_size[2];
        u32 m_med_region_cached[2];
        u32 m_seg_min_size;
        u32 m_seg_max_size;
        u64 m_seg_mem_range;
        u32 m_large_min_size;
        u64 m_large_mem_range;
        u32 m_large_max_allocs;
    };

    // A virtual memory allocator, suitable for CPU as well as GPU memory
    // + Small Size Allocator
    // + Coalesce Allocator
    // + Segregated Allocator
    // + Large Allocator
    extern xalloc* gCreateVmAllocator(xalloc* main_heap, xvmem* vmem, xvmem_config const* const cfg);

}; // namespace xcore

#endif // __X_ALLOCATOR_VIRTUAL_ALLOCATOR_H__