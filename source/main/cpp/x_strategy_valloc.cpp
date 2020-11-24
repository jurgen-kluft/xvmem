
#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"
#include "xbase/x_hibitset.h"

#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    struct List
    {
        typedef u16 Index;
        typedef u16 Head;

        struct Node
        {
            List::Index m_next; // used/free list
            List::Index m_prev; // ..
        };
    };

    // Tiny Size allocations
    // Can NOT manage GPU memory
    // Book-Keeping per page
    // <= 32: 8/16/24/32/../../1024/1280/1536/1792/2048
    struct Tiny
    {
        static const s32 BIN_MIN       = 1;
        static const s32 BIN_MAX       = 28;
        static const u64 ADDRESS_SPACE = (u64)16 * 1024 * 1024 * 1024;
        static const u32 CHUNK_SIZE    = 2 * 1024 * 1024;
        static const u64 CHUNKS        = ADDRESS_SPACE / CHUNK_SIZE; // 8192 (13 bits)

        struct Chunk
        {
            static const u32 PAGE  = 64 * 1024;
            static const u32 PAGES = CHUNK_SIZE / PAGE;
            u16              m_elem_free_list[PAGES];
            u16              m_elem_free_index[PAGES];
            u16              m_elem_used[PAGES];
            u32              m_elem_max;
            u32              m_free_page_bitmap; // 32 pages
        };

        void*      m_memory_base;
        Chunk      m_chunks[CHUNKS];
        List::Node m_chunk_listnodes[CHUNKS];
        List::Head m_free_chunk_list;
        List::Head m_used_chunk_list_per_size[28];
    };

    // Small sized allocations
    // Can manage GPU memory
    // Book-keeping is for chunk
    // At this size an allocation can overlap 2 pages
    // The amount of allocations is less/equal to 768 (requires a 2-level bitmap)
    // <= 16: 2560/3072/3584/4096/5120/6144/7168/8192/10240/12288/14336/16384/20480/24576
    struct Small
    {
        static const s32 BIN_MIN       = 29;
        static const s32 BIN_MAX       = 42;
        static const u64 ADDRESS_SPACE = ((u64)32 * 1024 * 1024 * 1024);
        static const u64 CHUNK_SIZE    = 2 * 1024 * 1024;
        static const u64 CHUNKS        = ADDRESS_SPACE / CHUNK_SIZE; // ~ 16K
        struct Chunk
        {
            static const u64 PAGE  = 64 * 1024;
            static const u64 PAGES = CHUNK_SIZE / PAGE;
            u16              m_max_allocs;
            u16              m_num_allocs;
        };
        struct ChunkOccupancy
        {
            u32 m_elem_bitmap0;
            u32 m_elem_bitmap1[24];
        };

        void*          m_memory_base;
        List::Node     m_chunk_nodes[CHUNKS];
        Chunk          m_chunk_stat[CHUNKS];
        ChunkOccupancy m_chunk_occupancy[CHUNKS];
        List::Head     m_free_chunk_list;
        List::Head     m_used_chunk_list_per_size[14];
    };

    // Medium sized allocations
    // Book-keeping is for chunk
    // At this size an allocation can overlap 2 pages
    // The amount of allocations is less/equal to 64 (single level bitmap)
    // <= 16: 28672\32768\36864\40960\49152\57344\65536\81920\98304\114688\131072\163840\196608\229376\262144
    struct Medium
    {
        static const u64 CHUNK_SIZE    = 2 * 1024 * 1024;
        static const s32 BIN_MIN       = 43;
        static const s32 BIN_MAX       = 56;
        static const u64 ADDRESS_SPACE = ((u64)64 * 1024 * 1024 * 1024);
        static const u64 CHUNKS        = ADDRESS_SPACE / CHUNK_SIZE;

        struct Chunk
        {
            static const u64 PAGE  = 64 * 1024;
            static const u64 PAGES = CHUNK_SIZE / PAGE;
            u8               m_max_allocs;
            u8               m_num_allocs;
        };
        struct ChunkOccupancy
        {
            u64 m_elem_bitmap;
        };

        void*          m_memory_base;
        List::Node     m_chunk_nodes[CHUNKS];
        Chunk          m_chunk_stat[CHUNKS];
        ChunkOccupancy m_chunk_occupancy[CHUNKS];
        List::Head     m_free_chunk_list;
        List::Head     m_used_chunk_list_per_size[16];
    };

    // =======================================================================================
    // Large size allocations
    // At this size, size alignment is page-size
    // The following size-bins are managed: 512KB/1MB/2MB/4MB/8MB
    struct Large
    {
        static const s32 BIN_MIN       = 57;
        static const s32 BIN_MAX       = 76;
        static const u64 CHUNK_SIZE    = 16 * 1024 * 1024;
        static const u64 ADDRESS_SPACE = (u64)16 * 1024 * 1024 * 1024;
        static const u64 CHUNKS        = ADDRESS_SPACE / CHUNK_SIZE; // 1024

        void*      m_memory_base;
        List::Head m_chunk_free_list;               // List of all free chunks
        List::Head m_chunk_used_list_per_size[8];   // Per-Size list of used chunks
        List::Node m_chunk_listnodes[CHUNKS];       //  8 KB
        u32        m_chunk_bitmap[CHUNKS];          //  8 KB
        u16        m_chunk_alloc_pages[32][CHUNKS]; // 64 KB
    };

    // =======================================================================================
    // Huge size allocations
    // At this size, size alignment is page-size
    // The following size-bins are managed: 16MB/32MB/64MB/128MB/256MB
    // So the largest allocation supported is 256MB
    struct Huge
    {
        static const s32 BIN_MIN       = 77;
        static const s32 BIN_MAX       = 96;
        static const u64 CHUNK_SIZE    = 256 * 1024 * 1024;
        static const u64 ADDRESS_SPACE = (u64)256 * 1024 * 1024 * 1024;
        static const u64 CHUNKS        = ADDRESS_SPACE / CHUNK_SIZE; // 1024

        void*      m_memory_base;
        List::Head m_chunk_free_list;               // List of all free chunks
        List::Head m_chunk_used_list_per_size[8];   // Per-Size list of used chunks
        List::Node m_chunk_list[CHUNKS];            //  4 KB
        u32        m_chunk_bitmap[CHUNKS];          //  4 KB
        u16        m_chunk_alloc_pages[32][CHUNKS]; // 64 KB
    };

    struct Arena
    {
        Tiny   m_region_tiny;
        Small  m_region_small;
        Medium m_region_medium;
        Large  m_region_large;
        Huge   m_region_huge;
    };

} // namespace xcore
