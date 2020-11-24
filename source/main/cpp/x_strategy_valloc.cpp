
#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"
#include "xbase/x_hibitset.h"

#include "xvmem/private/x_doubly_linked_list.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    // Tiny Size allocations
    // Can NOT manage GPU memory
    // Book-Keeping per page
    // <= 32: 8/16/24/32/../../1024/1280/1536/1792/2048
    struct Tiny
    {
        static const s32 BIN_MIN       = 0;
        static const s32 BIN_MAX       = 28;
        static const u64 ADDRESS_SPACE = (u64)16 * 1024 * 1024 * 1024;
        static const u32 CHUNK_SIZE    = 2 * 1024 * 1024;
        static const u64 CHUNKS        = ADDRESS_SPACE / CHUNK_SIZE; // 8192 (13 bits)
        static const u32 CHUNKS_CACHE  = 4;
        static const u32 PAGE_SIZE     = 64 * 1024;

        struct Chunk
        {
            static const u32 PAGES = CHUNK_SIZE / PAGE_SIZE;
            u16              m_elem_free_list[PAGES];
            u16              m_elem_free_index[PAGES];
            u16              m_elem_used[PAGES];
            u32              m_usable_pages_bitmap; // 32 pages
            u16              m_elem_max;
            u16              m_elem_size;
        };

        void*            m_memory_base;
        Chunk            m_chunks[CHUNKS];
        xalist_t::node_t m_chunk_listnodes[CHUNKS];
        xalist_t         m_free_chunk_list;
        xalist_t         m_free_cached_chunk_list; // Some free chunks are cached
        xalist_t::index  m_used_chunk_list_per_size[29];
    };

    void Initialize(Tiny::Chunk& chunk)
    {
        for (s32 i = 0; i < Tiny::Chunk::PAGES; ++i)
        {
            chunk.m_elem_free_list[i]  = xalist_t::NIL;
            chunk.m_elem_free_index[i] = 0;
            chunk.m_elem_used[i]       = 0;
        }
        chunk.m_elem_max            = 0;
        chunk.m_elem_size           = 0;
        chunk.m_usable_pages_bitmap = 0;
    }

    void Initialize(Tiny& region, xvmem* vmem)
    {
        u32 page_size;
        vmem->reserve(Tiny::ADDRESS_SPACE, page_size, 0, region.m_memory_base);
        ASSERT(Tiny::PAGE_SIZE == page_size);

        for (s32 i = 0; i < Tiny::CHUNKS; ++i)
        {
            Initialize(region.m_chunks[i]);
        }
        region.m_free_chunk_list.initialize(&region.m_chunk_listnodes[0], Tiny::CHUNKS, Tiny::CHUNKS);
        region.m_free_cached_chunk_list = xalist_t(0, Tiny::CHUNKS_CACHE);
        for (s32 i = Tiny::BIN_MIN; i <= Tiny::BIN_MAX; i++)
        {
            region.m_used_chunk_list_per_size[i - Tiny::BIN_MIN] = xalist_t::NIL;
        }
    }

    void* Allocate(Tiny& region, u32 size, u32 bin)
    {
        ASSERT(bin >= Tiny::BIN_MIN && bin <= Tiny::BIN_MAX);

        // Get the chunk from 'm_used_chunk_list_per_size[bin]'
        //   If it is NILL then take one from 'm_free_cached_chunk_list'
        //   If it is NIL then take one from 'm_free_chunk_list'
        //   Add it to 'm_used_chunk_list_per_size[bin]'

        // From 'usable_pages_bitmap' find the first-bit-set, convert to page index
        // Pop an element from the page, if page is empty reset bit in 'usable_pages_bitmap'.
        // If 'usable_pages_bitmap' is '0' set the m_used_chunk_list_per_size[bin] to NIL

        return nullptr;
    }

    u32 Deallocate(Tiny& region, void* ptr)
    {
        // Convert 'ptr' to chunk-index, page-index and elem-index
        // Convert m_chunks[chunk-index].m_elem_size to bin-index
        // Copy m_elem_size from chunk to return it from this function
        // Push element to the page, if page was empty then set bit in 'usable_pages_bitmap'.
        // If whole chunk is empty push it in the cache list, if the cache list is full
        //  then decommit the chunk and add it to the free list.

        return 0;
    }

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

        void*            m_memory_base;
        xalist_t::node_t m_chunk_nodes[CHUNKS];
        Chunk            m_chunk_stat[CHUNKS];
        ChunkOccupancy   m_chunk_occupancy[CHUNKS];
        xalist_t::head   m_free_chunk_list;
        xalist_t::head   m_used_chunk_list_per_size[14];
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

        void*            m_memory_base;
        xalist_t::node_t m_chunk_nodes[CHUNKS];
        Chunk            m_chunk_stat[CHUNKS];
        ChunkOccupancy   m_chunk_occupancy[CHUNKS];
        xalist_t::head   m_free_chunk_list;
        xalist_t::head   m_used_chunk_list_per_size[16];
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

        void*            m_memory_base;
        xalist_t::head   m_chunk_free_list;               // List of all free chunks
        xalist_t::head   m_chunk_used_list_per_size[8];   // Per-Size list of used chunks
        xalist_t::node_t m_chunk_listnodes[CHUNKS];       //  8 KB
        u32              m_chunk_bitmap[CHUNKS];          //  8 KB
        u16              m_chunk_alloc_pages[32][CHUNKS]; // 64 KB
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

        void*            m_memory_base;
        xalist_t::head   m_chunk_free_list;               // List of all free chunks
        xalist_t::head   m_chunk_used_list_per_size[8];   // Per-Size list of used chunks
        xalist_t::node_t m_chunk_list[CHUNKS];            //  4 KB
        u32              m_chunk_bitmap[CHUNKS];          //  4 KB
        u16              m_chunk_alloc_pages[32][CHUNKS]; // 64 KB
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
