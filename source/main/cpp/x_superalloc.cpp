#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_doubly_linked_list.h"
#include "xvmem/private/x_binmap.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    static inline void* toaddress(void* base, u64 offset) { return (void*)((u64)base + offset); }
    static inline u64   todistance(void* base, void* ptr)
    {
        ASSERT(ptr >= base);
        return (u64)((u64)ptr - (u64)base);
    }

    // Can only allocate, used internally to allocate initially required memory
    class superheap_t
    {
    public:
        void  initialize(xvmem* vmem, u64 memory_range, u64 size_to_pre_allocate);
        void  deinitialize();
        void* allocate(u32 size);

        void*  m_address;
        u64    m_address_range;
        xvmem* m_vmem;
        u32    m_size_alignment;
        u32    m_page_size;
        u32    m_page_count_current;
        u32    m_page_count_maximum;
        u64    m_ptr;
    };

    void superheap_t::initialize(xvmem* vmem, u64 memory_range, u64 size_to_pre_allocate)
    {
        u32 attributes  = 0;
        m_vmem          = vmem;
        m_address_range = memory_range;
        m_vmem->reserve(memory_range, m_page_size, attributes, m_address);
        m_size_alignment     = 32;
        m_page_count_maximum = (u32)(memory_range / m_page_size);
        m_page_count_current = 0;
        m_ptr                = 0;

        if (size_to_pre_allocate > 0)
        {
            u32 const pages_to_commit = (u32)(xalignUp(size_to_pre_allocate, (u64)m_page_size) / m_page_size);
            m_vmem->commit(m_address, m_page_size, pages_to_commit);
            m_page_count_current = pages_to_commit;
        }
    }

    void superheap_t::deinitialize()
    {
        m_vmem->release(m_address, m_address_range);
        m_address            = nullptr;
        m_address_range      = 0;
        m_vmem               = nullptr;
        m_size_alignment     = 0;
        m_page_size          = 0;
        m_page_count_current = 0;
        m_page_count_maximum = 0;
        m_ptr                = 0;
    }

    void* superheap_t::allocate(u32 size)
    {
        size        = xalignUp(size, m_size_alignment);
        u64 ptr_max = ((u64)m_page_count_current * m_page_size);
        if ((m_ptr + size) > ptr_max)
        {
            // add more pages
            u32 const page_count           = (u32)(xalignUp(m_ptr + size, (u64)m_page_size) / (u64)m_page_size);
            u32 const page_count_to_commit = page_count - m_page_count_current;
            u64       commit_base          = ((u64)m_page_count_current * m_page_size);
            m_vmem->commit(toaddress(m_address, commit_base), m_page_size, page_count_to_commit);
            m_page_count_current += page_count_to_commit;
        }
        u64 const offset = m_ptr;
        m_ptr += size;
        return toaddress(m_address, offset);
    }

    struct superpage_t
    {
        static const u16 NIL = 0xffff;

        u16 m_item_size;
        u16 m_item_count;
        u16 m_item_max;
        u16 m_item_freelist;

        void initialize(u32 size, u32 pagesize)
        {
            m_item_size     = size;
            m_item_count    = 0;
            m_item_max      = pagesize / size;
            m_item_freelist = NIL;
        }

        inline bool is_full() const { return m_item_count == m_item_max; }
        inline bool is_empty() const { return m_item_count == 0; }
        inline u32  ptr2idx(void* const ptr, void* const elem) const { return (u32)(((u64)elem - (u64)ptr) / m_item_size); }
        inline u32* idx2ptr(void* const ptr, u32 const index) const { return (u32*)((xbyte*)ptr + ((u64)index * m_item_size)); }

        u16 iallocate(void* page_address)
        {
            if (m_item_freelist != NIL)
            {
                u16 const  iitem = m_item_freelist;
                u16* const pitem = (u16*)idx2ptr(page_address, iitem);
                m_item_freelist  = pitem[0];
                m_item_count++;
                return iitem;
            }
            else if (m_item_count < m_item_max)
            {
                u16 const ielem = m_item_count++;
                return ielem;
            }
            // panic
            return NIL;
        }

        void deallocate(void* page_address, u16 item_index)
        {
            ASSERT(m_item_count > 0);
            ASSERT(item_index < m_item_max);
            u16* const pelem = (u16*)idx2ptr(page_address, item_index);
#ifdef SUPERALLOC_DEBUG
            x_memset(pelem, 0xFEFEFEFE, m_item_size);
#endif
            pelem[0]        = m_item_freelist;
            m_item_freelist = item_index;
            m_item_count -= 1;
        }
    };

    struct superpages_t
    {
        void  initialize(superheap_t& heap, xvmem* vmem, u64 address_range, u32 size_to_pre_allocate);
        void  deinitialize(superheap_t& heap);
        u32   checkout_page(u32 const alloc_size);
        void  release_page(u32 index);
        void* address_of_page(u32 ipage) const { return toaddress(m_address, (u64)ipage * m_page_size); }

        inline void* idx2ptr(u32 i) const
        {
            if (i == 0xffffffff)
                return nullptr;
            u16 const                pageindex = i >> 16;
            u16 const                itemindex = i & 0xFFFF;
            superpage_t const* const ppage     = &m_page_array[pageindex];
            void* const              paddr     = address_of_page(pageindex);
            return ppage->idx2ptr(paddr, itemindex);
        }

        inline u32 ptr2idx(void* ptr) const
        {
            if (ptr == nullptr)
                return 0xffffffff;
            u32 const                pageindex = (u32)(todistance(m_address, ptr) / m_page_size);
            superpage_t const* const ppage     = &m_page_array[pageindex];
            void* const              paddr     = address_of_page(pageindex);
            u32 const                itemindex = ppage->ptr2idx(paddr, ptr);
            return (pageindex << 16) | (itemindex & 0xFFFF);
        }

        xvmem*       m_vmem;
        void*        m_address;
        u64          m_address_range;
        u32          m_page_count;
        u32          m_page_size;
        superpage_t* m_page_array;
        lldata_t     m_page_list_data;
        llist_t      m_free_page_list;
        llist_t      m_cached_page_list;
    };

    void superpages_t::initialize(superheap_t& heap, xvmem* vmem, u64 address_range, u32 size_to_pre_allocate)
    {
        m_vmem         = vmem;
        u32 attributes = 0;
        m_vmem->reserve(address_range, m_page_size, attributes, m_address);
        m_address_range = address_range;

        m_page_count = (u32)(address_range / (u64)m_page_size);
        m_page_array = (superpage_t*)heap.allocate(m_page_count * sizeof(superpage_t));
#ifdef SUPERALLOC_DEBUG
        x_memset(m_page_array, 0xCDCDCDCD, m_page_count * sizeof(superpage_t));
#endif

        m_page_list_data.m_data     = heap.allocate(m_page_count * sizeof(llnode_t));
        m_page_list_data.m_itemsize = sizeof(llnode_t);
        m_page_list_data.m_pagesize = m_page_count * sizeof(llnode_t);

        u32 const num_pages_to_cache = xalignUp(size_to_pre_allocate, m_page_size) / m_page_size;
        ASSERT(num_pages_to_cache <= m_page_count);
        m_free_page_list.initialize(m_page_list_data, num_pages_to_cache, m_page_count - num_pages_to_cache, m_page_count);
        if (num_pages_to_cache > 0)
        {
            m_cached_page_list.initialize(m_page_list_data, 0, num_pages_to_cache, num_pages_to_cache);
            m_vmem->commit(m_address, m_page_size, num_pages_to_cache);
        }
    }

    void superpages_t::deinitialize(superheap_t& heap)
    {
        // NOTE: Do we need to decommit physical pages, or is 'release' enough?
        m_vmem->release(m_address, m_address_range);
    }

    u32 superpages_t::checkout_page(u32 const alloc_size)
    {
        // Get a page and initialize that page for this size
        u32 ipage = llnode_t::NIL;
        if (!m_cached_page_list.is_empty())
        {
            ipage = m_cached_page_list.remove_headi(m_page_list_data);
        }
        else if (!m_free_page_list.is_empty())
        {
            ipage       = m_free_page_list.remove_headi(m_page_list_data);
            void* apage = address_of_page(ipage);
            m_vmem->commit(apage, m_page_size, 1);
        }
#ifdef SUPERALLOC_DEBUG
        u64* apage = (u64*)address_of_page(ipage);
        x_memset(apage, 0xCDCDCDCD, m_page_size);
#endif
        superpage_t* ppage = &m_page_array[ipage];
        ppage->initialize(alloc_size, m_page_size);
        return ipage;
    }

    void superpages_t::release_page(u32 pageindex)
    {
        superpage_t* const ppage = &m_page_array[pageindex];
#ifdef SUPERALLOC_DEBUG
        x_memset(ppage, 0xFEFEFEFE, sizeof(superpage_t));
#endif
        if (!m_cached_page_list.is_full())
        {
            m_cached_page_list.insert(m_page_list_data, pageindex);
        }
        else
        {
            void* const paddr = address_of_page(pageindex);
            m_vmem->decommit(paddr, m_page_size, 1);
            m_free_page_list.insert(m_page_list_data, pageindex);
        }
    }

    // Power-of-2 sizes, minimum size = 8, maximum_size = 32768
    // @note: returned index to the user is u32[u16(page-index):u16(item-index)]
    class superfsa_t
    {
    public:
        static const u32 NIL = 0xffffffff;

        void initialize(superheap_t& heap, xvmem* vmem, u64 address_range, u32 size_to_pre_allocate);
        void deinitialize(superheap_t& heap);

        u32  alloc(u32 size);
        u32  allocsizeof(u32 size) const;
        void dealloc(u32 index);

        inline void* idx2ptr(u32 i) const { return m_pages.idx2ptr(i); }
        inline u32   ptr2idx(void* ptr) const { return m_pages.ptr2idx(ptr); }

        void* baseptr() const { return m_pages.m_address; }
        u32   pagesize() const { return m_pages.m_page_size; }

    private:
        superpages_t     m_pages;
        static const s32 c_max_num_sizes = 32;
        llhead_t         m_used_page_list_per_size[c_max_num_sizes];
    };

    void superfsa_t::initialize(superheap_t& heap, xvmem* vmem, u64 address_range, u32 size_to_pre_allocate)
    {
        m_pages.initialize(heap, vmem, address_range, size_to_pre_allocate);
        for (u32 i = 0; i < c_max_num_sizes; i++)
            m_used_page_list_per_size[i].reset();
    }

    void superfsa_t::deinitialize(superheap_t& heap) { m_pages.deinitialize(heap); }

    u32 superfsa_t::alloc(u32 alloc_size)
    {
        alloc_size      = xceilpo2(alloc_size);
        s32 const c     = xcountTrailingZeros(alloc_size);
        u32       ipage = 0xffffffff;
        ASSERT(c >= 0 && c < c_max_num_sizes);
        if (m_used_page_list_per_size[c].is_nil())
        {
            // Get a page and initialize that page for this size
            ipage = m_pages.checkout_page(alloc_size);
            m_used_page_list_per_size[c].insert(m_pages.m_page_list_data, ipage);
        }
        else
        {
            ipage = m_used_page_list_per_size[c].m_index;
        }

        if (ipage != llnode_t::NIL)
        {
            superpage_t* ppage    = &m_pages.m_page_array[ipage];
            void*        paddress = m_pages.address_of_page(ipage);
            u16 const    itemidx  = ppage->iallocate(paddress);
            if (ppage->is_full())
            {
                m_used_page_list_per_size[c].remove_item(m_pages.m_page_list_data, ipage);
            }
            return (ipage << 16) + itemidx;
        }
        else
        {
            return NIL;
        }
    }

    u32 superfsa_t::allocsizeof(u32 alloc_size) const { return xceilpo2(alloc_size); }

    void superfsa_t::dealloc(u32 i)
    {
        u16 const          pageindex = i >> 16;
        u16 const          itemindex = i & 0xFFFF;
        superpage_t* const ppage     = &m_pages.m_page_array[pageindex];
        void* const        paddr     = m_pages.address_of_page(pageindex);
        ppage->deallocate(paddr, itemindex);
        if (ppage->is_empty())
        {
            s32 const c = xcountTrailingZeros(ppage->m_item_size);
            ASSERT(c >= 0 && c < c_max_num_sizes);
            m_used_page_list_per_size[c].remove_item(m_pages.m_page_list_data, pageindex);
            m_pages.release_page(pageindex);
        }
    }

    struct superbin_t
    {
        inline superbin_t(u32 allocsize_mb, u32 allocsize_kb, u32 allocsize_b, u8 binidx, u8 allocindex, u8 use_binmap, u16 count, u16 l1len, u16 l2len)
            : m_alloc_size((xMB * allocsize_mb) + (xKB * allocsize_kb) + (allocsize_b))
            , m_alloc_bin_index(binidx)
            , m_alloc_index(allocindex)
            , m_use_binmap(use_binmap)
            , m_alloc_count(count)
            , m_binmap_l1len(l1len)
            , m_binmap_l2len(l2len)
        {
        }

        u32 m_alloc_size;
        u32 m_alloc_bin_index : 8; // Only one indirection is allowed
        u32 m_alloc_index : 8;     // The index into the allocator that manages us
        u32 m_use_binmap : 1;      // How do we manage a chunk (binmap or page-count)
        u32 m_alloc_count;
        u16 m_binmap_l1len;
        u16 m_binmap_l2len;
    };

    // Managing requests of different chunk-sizes but managed through first a division into blocks, where blocks are
    // divided into segments. Segments contain chunks.
    //
    // Chunk-Sizes are all power-of-2 sizes
    //
    // Functionality:
    //   Allocate
    //    - Handling the request of a new chunk, either creating one or taking one from the cache
    //   Deallocate
    //    - Quickly finding the block_t*, chunk_t* and superalloc_t* that belong to a 'void* ptr'
    //    - Collecting a now empty-chunk and either release or cache it
    //
    //   Get chunk by index
    //   Get address of chunk
    //
    // There are 2 ways to add tracking of allocations:
    // 1) External data structure (xdtrie2 ?)
    // 2) Add an u32* array in block_t where every entry can hold another array of u32 to enable
    //    a u32 per allocation. (This is the most optimum).
    //
    struct superchunks_t
    {
        struct chunk_t : llnode_t
        {
            u32 m_bin_map;
            u32 m_page_index;
            u16 m_elem_used;
            u16 m_bin_index;
        };

        struct block_t : llnode_t
        {
            u16* m_chunks_physical_pages;
            u32* m_chunks_array;
            u32  m_binmap_chunks_free;
            u32  m_binmap_chunks_cached;
            u32  m_count_chunks_cached;
            u32  m_count_chunks_free;
            u16  m_config_index;
            u16  m_chunks_used;
        };

        struct index_t
        {
            u16 m_block_index;
            u16 m_block_chunk_index;
            u32 m_chunk_index;
        };

        struct config_t
        {
            config_t(u16 chunks_max, u8 chunks_shift, u16 l1_len, u16 l2_len)
                : m_chunks_shift(chunks_shift)
                , m_chunks_max(chunks_max)
                , m_binmap_l1(l1_len)
                , m_binmap_l2(l2_len)
            {
            }
            u16 m_chunks_shift;
            u16 m_chunks_max;
            u16 m_binmap_l1;
            u16 m_binmap_l2;
        };

        static const u64 c_address_block_range = xGB * 1;
        static const u64 c_fsa_address_range   = xMB * 128;
        static const u64 c_fsa_initial_range   = xMB * 1;

        static const s32 c_num_configs            = 32;
        const config_t   c_configs[c_num_configs] = {
            config_t(0, 0, 0, 0),       config_t(0, 0, 0, 0),       config_t(0, 0, 0, 0),       config_t(0, 0, 0, 0),       config_t(0, 0, 0, 0),      config_t(0, 0, 0, 0),     config_t(0, 0, 0, 0),     config_t(0, 0, 0, 0),
            config_t(0, 0, 0, 0),       config_t(0, 0, 0, 0),       config_t(0, 0, 0, 0),       config_t(0, 0, 0, 0),       config_t(0, 0, 0, 0),      config_t(0, 0, 0, 0),     config_t(0, 0, 0, 0),     config_t(0, 0, 0, 0),
            config_t(2048, 16, 8, 128), config_t(2048, 17, 8, 128), config_t(2048, 18, 8, 128), config_t(2048, 19, 8, 128), config_t(1024, 20, 4, 64), config_t(512, 21, 2, 32), config_t(256, 22, 2, 16), config_t(128, 23, 2, 8),
            config_t(64, 24, 2, 4),     config_t(32, 25, 0, 0),     config_t(16, 26, 0, 0),     config_t(8, 27, 0, 0),      config_t(4, 28, 0, 0),     config_t(2, 29, 0, 0),    config_t(0, 0, 0, 0),     config_t(0, 0, 0, 0),
        };

        void initialize(xvmem* vmem, u64 address_range, superheap_t& heap)
        {
            m_vmem          = vmem;
            m_address_range = address_range;
            u32 const attrs = 0;
            m_vmem->reserve(address_range, m_page_size, attrs, m_address_base);
            m_page_shift = xcountTrailingZeros(m_page_size);
            m_page_count = 0;

            m_fsa.initialize(heap, vmem, c_fsa_address_range, c_fsa_initial_range);
            m_chunk_list_data.m_data     = m_fsa.baseptr();
            m_chunk_list_data.m_itemsize = m_fsa.allocsizeof(sizeof(chunk_t));
            m_chunk_list_data.m_pagesize = m_fsa.pagesize();

            m_blocks_shift                = xcountTrailingZeros(c_address_block_range);
            u32 const num_blocks          = (u32)(m_address_range >> m_blocks_shift);
            m_blocks_array                = (block_t*)heap.allocate(num_blocks * sizeof(block_t));
            m_blocks_list_data.m_data     = m_blocks_array;
            m_blocks_list_data.m_itemsize = sizeof(block_t);
            m_blocks_list_free.initialize(m_blocks_list_data, 0, num_blocks, num_blocks);

            for (s32 i = 0; i < 32; i++)
            {
                m_block_per_group_list_active[i].reset();
            }
        }

        void deinitialize(superheap_t& heap) { m_fsa.deinitialize(heap); }

        static inline u32 chunk_size_to_config_index(u32 const chunk_size)
        {
            ASSERT(xispo2(chunk_size));
            s32 const config_index = xcountTrailingZeros(chunk_size);
            return config_index;
        }

        void initialize_binmap(u32 const binmap_index, config_t const& config, bool set)
        {
            binmap_t* bm    = (binmap_t*)m_fsa.idx2ptr(binmap_index);
            bm->m_l1_offset = 0xffffffff;
            bm->m_l2_offset = 0xffffffff;
            if (config.m_binmap_l2 > 0)
                bm->m_l2_offset = m_fsa.alloc(sizeof(u16) * config.m_binmap_l2);
            if (config.m_binmap_l1 > 0)
                bm->m_l1_offset = m_fsa.alloc(sizeof(u16) * config.m_binmap_l1);
            u16* l1 = (u16*)m_fsa.idx2ptr(bm->m_l1_offset);
            u16* l2 = (u16*)m_fsa.idx2ptr(bm->m_l2_offset);
            if (set)
                bm->init1(config.m_chunks_max, l1, config.m_binmap_l1, l2, config.m_binmap_l2);
            else
                bm->init(config.m_chunks_max, l1, config.m_binmap_l1, l2, config.m_binmap_l2);
        }

        binmap_t* get_binmap_by_index(u32 const binmap_index, u16*& l1, u16*& l2)
        {
            binmap_t* bm = (binmap_t*)m_fsa.idx2ptr(binmap_index);
            l1           = (u16*)m_fsa.idx2ptr(bm->m_l1_offset);
            l2           = (u16*)m_fsa.idx2ptr(bm->m_l2_offset);
            return bm;
        }

        u16 checkout_block(u32 const config_index)
        {
            config_t const& config     = c_configs[config_index];
            u16 const       num_chunks = config.m_chunks_max;

            u32 const block_index         = m_blocks_list_free.remove_headi(m_blocks_list_data);
            block_t*  block               = &m_blocks_array[block_index];
            u32 const ichunks_index_array = m_fsa.alloc(sizeof(u32) * num_chunks);
            u32 const ichunks_pages_array = m_fsa.alloc(sizeof(u32) * num_chunks);

            block->m_prev                  = llnode_t::NIL;
            block->m_next                  = llnode_t::NIL;
            block->m_chunks_physical_pages = (u16*)m_fsa.idx2ptr(ichunks_pages_array);
            block->m_chunks_array          = (u32*)m_fsa.idx2ptr(ichunks_index_array);
            block->m_binmap_chunks_cached  = m_fsa.alloc(sizeof(binmap_t));
            block->m_binmap_chunks_free    = m_fsa.alloc(sizeof(binmap_t));
            if (config.m_binmap_l2 > 0)
            {
                initialize_binmap(block->m_binmap_chunks_cached, config, true);
                initialize_binmap(block->m_binmap_chunks_free, config, false);
            }

            block->m_config_index = config_index;
            block->m_chunks_used  = 0;

            block->m_count_chunks_cached = 0;
            block->m_count_chunks_free   = num_chunks;

            return block_index;
        }

        u32 chunk_physical_pages(superbin_t const& bin, u32 alloc_size) const
        {
            u32 size;
            if (bin.m_use_binmap == 1)
                size = (bin.m_alloc_size * bin.m_alloc_count);
            else
                size = alloc_size;
            return (size + (m_page_size - 1)) >> m_page_shift;
        }

        u32 checkout_chunk(u32 main_chunk_size, u32 alloc_size, superbin_t const& bin)
        {
            u32 const config_index = chunk_size_to_config_index(main_chunk_size);
            u32       block_index  = 0xffffffff;
            if (m_block_per_group_list_active[config_index].is_nil())
            {
                block_index = checkout_block(config_index);
                m_block_per_group_list_active[config_index].insert(m_blocks_list_data, block_index);
            }
            else
            {
                block_index = m_block_per_group_list_active[config_index].m_index;
            }

            u32 const required_physical_pages = chunk_physical_pages(bin, alloc_size);
            m_page_count += required_physical_pages;

            // Here we have a block where we can get a chunk from
            config_t const& config                  = c_configs[config_index];
            block_t*        block                   = &m_blocks_array[block_index];
            u32             block_chunk_index       = 0xffffffff;
            u32             already_committed_pages = 0;
            if (block->m_count_chunks_cached > 0)
            {
                u16 *     l1, *l2;
                binmap_t* bm      = get_binmap_by_index(block->m_binmap_chunks_cached, l1, l2);
                block_chunk_index = bm->findandset(config.m_chunks_max, l1, l2);
                block->m_count_chunks_cached -= 1;
                already_committed_pages = block->m_chunks_physical_pages[block_chunk_index];
            }
            else if (block->m_count_chunks_free > 0)
            {
                u16 *     l1, *l2;
                binmap_t* bm      = get_binmap_by_index(block->m_binmap_chunks_free, l1, l2);
                block_chunk_index = bm->findandset(config.m_chunks_max, l1, l2);
                block->m_count_chunks_free -= 1;
            }
            else
            {
                // Error, this segment should have been removed from 'm_segment_per_chunk_size_active'
                ASSERT(false);
            }

            const u32 chunk_index = m_fsa.alloc(sizeof(chunk_t));
            chunk_t*  chunk       = (chunk_t*)m_fsa.idx2ptr(chunk_index);
            chunk->m_prev         = llnode_t::NIL;
            chunk->m_next         = llnode_t::NIL;
            chunk->m_bin_map      = superfsa_t::NIL;
            chunk->m_page_index   = (block_index << (m_blocks_shift - m_page_shift)) + (block_chunk_index << (config.m_chunks_shift - m_page_shift));
            ASSERT(chunk->m_page_index < (m_address_range >> m_page_shift));
            chunk->m_elem_used                                = 0;
            block->m_chunks_array[block_chunk_index]          = chunk_index;
            block->m_chunks_physical_pages[block_chunk_index] = required_physical_pages;

            // Commit the virtual pages for this chunk
            if (required_physical_pages < already_committed_pages)
            {
                // Overcommitted, uncommit pages ?
            }
            else if (required_physical_pages > already_committed_pages)
            {
                // Undercommitted, commit necessary pages
            }

            // Check if block is now empty
            block->m_chunks_used += 1;
            if (block->m_chunks_used == config.m_chunks_max)
            {
                m_block_per_group_list_active[config_index].remove_item(m_blocks_list_data, block_index);
            }

            // Return the chunk index
            return chunk_index;
        }

        void release_chunk(u32 const chunk_index, u32 alloc_size)
        {
            chunk_t* chunk = get_chunk_by_index(chunk_index);
            index_t  info  = page_index_to_info(chunk->m_page_index);

            block_t*        block        = &m_blocks_array[info.m_block_index];
            u32 const       config_index = block->m_config_index;
            config_t const& config       = c_configs[config_index];

            if (block->m_chunks_used == config.m_chunks_max)
            {
                m_block_per_group_list_active[config_index].insert(m_blocks_list_data, info.m_block_index);
            }

            m_page_count -= block->m_chunks_physical_pages[info.m_block_chunk_index];

            // Release the chunk structure back to the fsa
            m_fsa.dealloc(chunk_index);
            chunk                                           = nullptr;
            block->m_chunks_array[info.m_block_chunk_index] = 0xffffffff;

            // We need to limit the number of cached chunks, once that happens we need to add the
            // block_chunk_index to the m_binmap_chunks_free.
            u16 *     l1, *l2;
            binmap_t* bm = get_binmap_by_index(block->m_binmap_chunks_cached, l1, l2);
            bm->clr(config.m_chunks_max, l1, l2, info.m_block_chunk_index);
            block->m_count_chunks_cached += 1;

            block->m_chunks_used -= 1;
            if (block->m_chunks_used == 0)
            {
                m_block_per_group_list_active[config_index].remove_item(m_blocks_list_data, info.m_block_index);

                // Maybe every size should cache at least one block otherwise single alloc/dealloc calls will
                // checkout and release a block every time?
                // TODO: Deal with jittering between block checkout/release

                // Release back all physical pages of the cached chunks
                binmap_t* bm = get_binmap_by_index(block->m_binmap_chunks_cached, l1, l2);
                while (block->m_count_chunks_cached > 0)
                {
                    u32 const ci = bm->findandset(config.m_chunks_max, l1, l2);
                    // NOTE: We need to decommit memory here.
                    block->m_count_chunks_cached -= 1;
                }

                u32 const chunks_array_index = m_fsa.ptr2idx(block->m_chunks_array);
                m_fsa.dealloc(chunks_array_index);
                u32 const chunks_pages_index = m_fsa.ptr2idx(block->m_chunks_physical_pages);
                m_fsa.dealloc(chunks_pages_index);
                m_fsa.dealloc(block->m_binmap_chunks_cached);
                m_fsa.dealloc(block->m_binmap_chunks_free);

                block->m_prev                = llnode_t::NIL;
                block->m_next                = llnode_t::NIL;
                block->m_chunks_array        = nullptr;
                block->m_count_chunks_cached = 0;
                block->m_count_chunks_free   = 0;
                block->m_config_index        = 0xffff;

                m_blocks_list_free.insert(m_blocks_list_data, info.m_block_index);
            }
        }

        // When deallocating, call this to get the page-index which you can than use
        // to get the 'chunk_t*'.
        u32     address_to_page_index(void* ptr) const { return (u32)(todistance(m_address_base, ptr) >> m_page_shift); }
        void*   page_index_to_address(u32 const page_index) const { return toaddress(m_address_base, (u64)page_index * m_page_size); }
        index_t page_index_to_info(u32 const page_index) const
        {
            index_t   info;
            u32 const page_index_to_block_index_shift       = (m_blocks_shift - m_page_shift);
            info.m_block_index                              = page_index >> page_index_to_block_index_shift;
            block_t*  block                                 = &m_blocks_array[info.m_block_index];
            u32 const block_page_index                      = page_index & ((1 << page_index_to_block_index_shift) - 1);
            u32 const block_page_index_to_chunk_index_shift = (c_configs[block->m_config_index].m_chunks_shift - m_page_shift);
            info.m_block_chunk_index                        = block_page_index >> block_page_index_to_chunk_index_shift;
            ASSERT(info.m_block_chunk_index < c_configs[block->m_config_index].m_chunks_max);
            info.m_chunk_index = block->m_chunks_array[info.m_block_chunk_index];
            ASSERT(info.m_chunk_index != 0xffffffff);
            return info;
        }

        chunk_t*  get_chunk_by_index(u32 const chunk_index) const { return (chunk_t*)m_fsa.idx2ptr(chunk_index); }
        u32       get_index_of_chunk(chunk_t* chunk) const { return m_fsa.ptr2idx(chunk); }
        lldata_t& get_chunk_list_data() { return m_chunk_list_data; }
        block_t*  get_block_from_index(u32 const block_index) const { return &m_blocks_array[block_index]; }

        superfsa_t m_fsa;
        lldata_t   m_chunk_list_data;
        llhead_t   m_block_per_group_list_active[32];
        xvmem*     m_vmem;
        void*      m_address_base;
        u64        m_address_range;
        u32        m_page_count;
        u32        m_page_size;
        u32        m_page_shift;   // e.g. 16 (1<<16 = 64 KB)
        s16        m_blocks_shift; // e.g. 25 (1<<30 =  1 GB)
        block_t*   m_blocks_array;
        lldata_t   m_blocks_list_data;
        llist_t    m_blocks_list_free;
    };

    // @superalloc manages an address range, a list of chunks and a range of allocation sizes.
    struct superalloc_t
    {
        superalloc_t(const superalloc_t& s, llhead_t* used_chunk_list_per_size)
            : m_chunk_size(s.m_chunk_size)
            , m_chunks(nullptr)
            , m_used_chunk_list_per_size(used_chunk_list_per_size)
        {
        }

        superalloc_t(u32 chunk_size)
            : m_chunk_size(chunk_size)
            , m_chunks(nullptr)
            , m_used_chunk_list_per_size(nullptr)
        {
        }

        void  initialize(superchunks_t* chunks, superheap_t& heap, superfsa_t& fsa);
        void* allocate(superfsa_t& sfsa, u32 size, superbin_t const& bin);
        u32   deallocate(superfsa_t& sfsa, void* ptr, u32 chunkindex, superbin_t const& bin);

        void  initialize_chunk(superfsa_t& fsa, u32 chunkindex, u32 size, superbin_t const& bin);
        void  deinitialize_chunk(superfsa_t& fsa, u32 chunkindex, superbin_t const& bin);
        void* allocate_from_chunk(superfsa_t& fsa, u32 chunkindex, u32 size, superbin_t const& bin, bool& chunk_is_now_full);
        u32   deallocate_from_chunk(superfsa_t& fsa, u32 chunkindex, void* ptr, superbin_t const& bin, bool& chunk_is_now_empty, bool& chunk_was_full);

        u32            m_chunk_size;
        superchunks_t* m_chunks;
        llhead_t*      m_used_chunk_list_per_size;
    };

    void superalloc_t::initialize(superchunks_t* chunks, superheap_t& heap, superfsa_t& fsa) { m_chunks = chunks; }

    void* superalloc_t::allocate(superfsa_t& sfsa, u32 alloc_size, superbin_t const& bin)
    {
        u32 const c          = bin.m_alloc_bin_index;
        llindex_t chunkindex = m_used_chunk_list_per_size[c].m_index;
        if (chunkindex == llnode_t::NIL)
        {
            chunkindex = m_chunks->checkout_chunk(m_chunk_size, alloc_size, bin);
            initialize_chunk(sfsa, chunkindex, alloc_size, bin);
            m_used_chunk_list_per_size[c].insert(m_chunks->get_chunk_list_data(), chunkindex);
        }

        bool        chunk_is_now_full = false;
        void* const ptr               = allocate_from_chunk(sfsa, chunkindex, alloc_size, bin, chunk_is_now_full);
        if (chunk_is_now_full) // Chunk is full, no more allocations possible
        {
            m_used_chunk_list_per_size[c].remove_item(m_chunks->get_chunk_list_data(), chunkindex);
        }
        return ptr;
    }

    u32 superalloc_t::deallocate(superfsa_t& fsa, void* ptr, u32 chunkindex, superbin_t const& bin)
    {
        u32 const c                  = bin.m_alloc_bin_index;
        bool      chunk_is_now_empty = false;
        bool      chunk_was_full     = false;
        u32 const alloc_size         = deallocate_from_chunk(fsa, chunkindex, ptr, bin, chunk_is_now_empty, chunk_was_full);
        if (chunk_is_now_empty)
        {
            if (!chunk_was_full)
            {
                m_used_chunk_list_per_size[c].remove_item(m_chunks->get_chunk_list_data(), chunkindex);
            }
            deinitialize_chunk(fsa, chunkindex, bin);
            m_chunks->release_chunk(chunkindex, alloc_size);
        }
        else if (chunk_was_full)
        {
            m_used_chunk_list_per_size[c].insert(m_chunks->get_chunk_list_data(), chunkindex);
        }
        return alloc_size;
    }

    void superalloc_t::initialize_chunk(superfsa_t& fsa, u32 chunkindex, u32 alloc_size, superbin_t const& bin)
    {
        superchunks_t::chunk_t* chunk = m_chunks->get_chunk_by_index(chunkindex);
        if (bin.m_use_binmap == 1)
        {
            u32 const ibinmap = fsa.alloc(sizeof(binmap_t));
            binmap_t* binmap  = (binmap_t*)fsa.idx2ptr(ibinmap);
            chunk->m_bin_map  = ibinmap;
            if (bin.m_alloc_count > 32)
            {
                binmap->m_l1_offset = fsa.alloc(sizeof(u16) * bin.m_binmap_l1len);
                binmap->m_l2_offset = fsa.alloc(sizeof(u16) * bin.m_binmap_l2len);
                u16* l1             = (u16*)fsa.idx2ptr(binmap->m_l1_offset);
                u16* l2             = (u16*)fsa.idx2ptr(binmap->m_l2_offset);
                binmap->init(bin.m_alloc_count, l1, bin.m_binmap_l1len, l2, bin.m_binmap_l2len);
            }
            else
            {
                binmap->m_l1_offset = superfsa_t::NIL;
                binmap->m_l2_offset = superfsa_t::NIL;
                binmap->init(bin.m_alloc_count, nullptr, 0, nullptr, 0);
            }
        }
        else
        {
            chunk->m_bin_map = 0xffffffff;
        }
        chunk->m_bin_index = bin.m_alloc_bin_index;
        chunk->m_elem_used = 0;
    }

    void superalloc_t::deinitialize_chunk(superfsa_t& fsa, u32 chunkindex, superbin_t const& bin)
    {
        superchunks_t::chunk_t* chunk = m_chunks->get_chunk_by_index(chunkindex);
        if (bin.m_use_binmap == 1)
        {
            binmap_t* binmap = (binmap_t*)fsa.idx2ptr(chunk->m_bin_map);
            if (binmap->m_l1_offset != superfsa_t::NIL)
            {
                fsa.dealloc(binmap->m_l1_offset);
                fsa.dealloc(binmap->m_l2_offset);
            }
            fsa.dealloc(chunk->m_bin_map);
            chunk->m_bin_map = superfsa_t::NIL;
        }
    }

    void* superalloc_t::allocate_from_chunk(superfsa_t& fsa, u32 chunkindex, u32 size, superbin_t const& bin, bool& chunk_is_now_full)
    {
        superchunks_t::chunk_t* chunk = m_chunks->get_chunk_by_index(chunkindex);
        ASSERT(chunk->m_bin_index == bin.m_alloc_bin_index);

        void* ptr;
        if (bin.m_use_binmap == 1)
        {
            binmap_t* binmap = (binmap_t*)fsa.idx2ptr(chunk->m_bin_map);
            u16*      l1     = nullptr;
            u16*      l2     = nullptr;
            if (bin.m_alloc_count > 32)
            {
                l1 = (u16*)fsa.idx2ptr(binmap->m_l1_offset);
                l2 = (u16*)fsa.idx2ptr(binmap->m_l2_offset);
            }
            u32 const i = binmap->findandset(bin.m_alloc_count, l1, l2);
            ASSERT(i < bin.m_alloc_count);
            ptr = (xbyte*)m_chunks->page_index_to_address(chunk->m_page_index) + ((u64)i * bin.m_alloc_size);
        }
        else
        {
            chunk->m_bin_map = (size + (m_chunks->m_page_size - 1)) >> m_chunks->m_page_shift;
            ptr              = m_chunks->page_index_to_address(chunk->m_page_index);
        }

        chunk->m_elem_used += 1;
        chunk_is_now_full = (bin.m_alloc_count == chunk->m_elem_used);

        return ptr;
    }

    u32 superalloc_t::deallocate_from_chunk(superfsa_t& fsa, u32 chunkindex, void* ptr, superbin_t const& bin, bool& chunk_is_now_empty, bool& chunk_was_full)
    {
        superchunks_t::chunk_t* chunk = m_chunks->get_chunk_by_index(chunkindex);
        ASSERT(chunk->m_bin_index == bin.m_alloc_bin_index);

        u32 size;
        if (bin.m_use_binmap == 1)
        {
            void* const chunkaddress = m_chunks->page_index_to_address(chunk->m_page_index);
            u32 const   i            = (u32)(todistance(chunkaddress, ptr) / bin.m_alloc_size);
            ASSERT(i < bin.m_alloc_count);
            binmap_t* binmap = (binmap_t*)fsa.idx2ptr(chunk->m_bin_map);
            u16*      l1     = (u16*)fsa.idx2ptr(binmap->m_l1_offset);
            u16*      l2     = (u16*)fsa.idx2ptr(binmap->m_l2_offset);
            binmap->clr(bin.m_alloc_count, l1, l2, i);
            size = bin.m_alloc_size;
        }
        else
        {
            u32 const num_physical_pages = chunk->m_bin_map;
            size                         = num_physical_pages * m_chunks->m_page_size;
        }

        chunk_was_full = (bin.m_alloc_count == chunk->m_elem_used);
        chunk->m_elem_used -= 1;
        chunk_is_now_empty = (0 == chunk->m_elem_used);

        return size;
    }

    /// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    /// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    /// The following is a strict data-drive initialization of the bins and allocators, please know what you are doing when modifying any of this.

    struct superallocator_config_t
    {
        superallocator_config_t()
            : m_num_bins(0)
            , m_asbins(nullptr)
            , m_num_allocators(0)
            , m_allocators(nullptr)
            , m_address_range(xGB * 128)
            , m_internal_heap_address_range(0)
            , m_internal_heap_pre_size(0)
            , m_internal_fsa_address_range(0)
            , m_internal_fsa_pre_size(0)
        {
        }

        superallocator_config_t(const superallocator_config_t& other)
            : m_num_bins(other.m_num_bins)
            , m_asbins(other.m_asbins)
            , m_num_allocators(other.m_num_allocators)
            , m_allocators(other.m_allocators)
            , m_address_range(other.m_address_range)
            , m_internal_heap_address_range(other.m_internal_heap_address_range)
            , m_internal_heap_pre_size(other.m_internal_heap_pre_size)
            , m_internal_fsa_address_range(other.m_internal_fsa_address_range)
            , m_internal_fsa_pre_size(other.m_internal_fsa_pre_size)
        {
        }

        superallocator_config_t(s32 const num_bins, superbin_t const* asbins, const s32 num_allocators, superalloc_t const* allocators, u64 const address_range, u32 const internal_heap_address_range, u32 const internal_heap_pre_size,
                                u32 const internal_fsa_address_range, u32 const internal_fsa_pre_size)
            : m_num_bins(num_bins)
            , m_asbins(asbins)
            , m_num_allocators(num_allocators)
            , m_allocators(allocators)
            , m_address_range(address_range)
            , m_internal_heap_address_range(internal_heap_address_range)
            , m_internal_heap_pre_size(internal_heap_pre_size)
            , m_internal_fsa_address_range(internal_fsa_address_range)
            , m_internal_fsa_pre_size(internal_fsa_pre_size)
        {
        }

        s32                 m_num_bins;
        superbin_t const*   m_asbins;
        s32                 m_num_allocators;
        superalloc_t const* m_allocators;
        u64                 m_address_range;
        u32                 m_internal_heap_address_range;
        u32                 m_internal_heap_pre_size;
        u32                 m_internal_fsa_address_range;
        u32                 m_internal_fsa_pre_size;
    };

    namespace superallocator_config_desktop_app_25p_t
    {
        // 25% allocation waste
        // @TODO: for level 1 we can skip allocating u16[] if the count for level 1 <= 32
        // superbin_t(allocation size MB, KB, B, bin redir index, allocator index, use binmap?, maximum allocation count, binmap level 1 length (u16), binmap level 2 length (u16))
        static const s32        c_num_bins           = 112;
        static const superbin_t c_asbins[c_num_bins] = {
            superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),   superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),   superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),   superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),   superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),
            superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),   superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),   superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),   superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),   superbin_t(0, 0, 10, 10, 0, 1, 6553, 32, 512),
            superbin_t(0, 0, 12, 10, 0, 1, 5461, 32, 512), superbin_t(0, 0, 14, 12, 0, 1, 4681, 32, 512), superbin_t(0, 0, 16, 12, 0, 1, 4096, 16, 256), superbin_t(0, 0, 20, 13, 0, 1, 3276, 16, 256), superbin_t(0, 0, 24, 14, 0, 1, 2730, 16, 256),
            superbin_t(0, 0, 28, 15, 0, 1, 2340, 16, 256), superbin_t(0, 0, 32, 16, 0, 1, 2048, 8, 128),  superbin_t(0, 0, 40, 17, 0, 1, 1638, 8, 128),  superbin_t(0, 0, 48, 18, 0, 1, 1365, 8, 128),  superbin_t(0, 0, 56, 19, 0, 1, 1170, 8, 128),
            superbin_t(0, 0, 64, 20, 0, 1, 1024, 4, 64),   superbin_t(0, 0, 80, 21, 0, 1, 819, 4, 64),    superbin_t(0, 0, 96, 22, 0, 1, 682, 4, 64),    superbin_t(0, 0, 112, 23, 0, 1, 585, 4, 64),   superbin_t(0, 0, 128, 24, 0, 1, 512, 2, 32),
            superbin_t(0, 0, 160, 25, 0, 1, 409, 2, 32),   superbin_t(0, 0, 192, 26, 0, 1, 341, 2, 32),   superbin_t(0, 0, 224, 27, 0, 1, 292, 2, 32),   superbin_t(0, 0, 256, 28, 0, 1, 256, 2, 16),   superbin_t(0, 0, 320, 29, 0, 1, 204, 2, 16),
            superbin_t(0, 0, 384, 30, 0, 1, 170, 2, 16),   superbin_t(0, 0, 448, 31, 0, 1, 146, 2, 16),   superbin_t(0, 0, 512, 32, 0, 1, 128, 2, 8),    superbin_t(0, 0, 640, 33, 0, 1, 102, 2, 8),    superbin_t(0, 0, 768, 34, 0, 1, 85, 2, 8),
            superbin_t(0, 0, 896, 35, 0, 1, 73, 2, 8),     superbin_t(0, 1, 0, 36, 0, 1, 64, 2, 4),       superbin_t(0, 1, 256, 37, 0, 1, 51, 2, 4),     superbin_t(0, 1, 512, 38, 1, 1, 85, 2, 8),     superbin_t(0, 1, 768, 39, 1, 1, 73, 2, 8),
            superbin_t(0, 2, 0, 40, 0, 1, 32, 0, 0),       superbin_t(0, 2, 512, 41, 1, 1, 51, 2, 4),     superbin_t(0, 3, 0, 42, 2, 1, 85, 2, 8),       superbin_t(0, 3, 512, 43, 2, 1, 73, 2, 8),     superbin_t(0, 4, 0, 44, 0, 1, 16, 0, 0),
            superbin_t(0, 5, 0, 45, 2, 1, 51, 2, 4),       superbin_t(0, 6, 0, 46, 2, 1, 32, 2, 4),       superbin_t(0, 7, 0, 47, 3, 1, 73, 2, 8),       superbin_t(0, 8, 0, 48, 0, 1, 8, 0, 0),        superbin_t(0, 10, 0, 49, 3, 1, 51, 2, 4),
            superbin_t(0, 12, 0, 50, 2, 1, 16, 0, 0),      superbin_t(0, 14, 0, 51, 3, 1, 32, 2, 4),      superbin_t(0, 16, 0, 52, 0, 1, 4, 0, 0),       superbin_t(0, 20, 0, 53, 3, 1, 16, 0, 0),      superbin_t(0, 24, 0, 54, 2, 1, 8, 0, 0),
            superbin_t(0, 28, 0, 55, 3, 1, 16, 0, 0),      superbin_t(0, 32, 0, 56, 0, 1, 2, 0, 0),       superbin_t(0, 40, 0, 57, 3, 1, 8, 0, 0),       superbin_t(0, 48, 0, 58, 2, 1, 4, 0, 0),       superbin_t(0, 56, 0, 59, 3, 1, 8, 0, 0),
            superbin_t(0, 64, 0, 60, 0, 0, 1, 0, 0),       superbin_t(0, 80, 0, 61, 3, 1, 4, 0, 0),       superbin_t(0, 96, 0, 62, 2, 1, 2, 0, 0),       superbin_t(0, 112, 0, 63, 3, 1, 4, 0, 0),      superbin_t(0, 128, 0, 64, 1, 0, 1, 0, 0),
            superbin_t(0, 160, 0, 65, 3, 1, 2, 0, 0),      superbin_t(0, 192, 0, 66, 2, 0, 1, 0, 0),      superbin_t(0, 224, 0, 67, 3, 1, 2, 0, 0),      superbin_t(0, 256, 0, 68, 2, 0, 1, 0, 0),      superbin_t(0, 320, 0, 69, 3, 0, 1, 0, 0),
            superbin_t(0, 384, 0, 70, 3, 0, 1, 0, 0),      superbin_t(0, 448, 0, 71, 3, 0, 1, 0, 0),      superbin_t(0, 512, 0, 72, 3, 0, 1, 0, 0),      superbin_t(0, 640, 0, 73, 4, 0, 1, 0, 0),      superbin_t(0, 768, 0, 74, 4, 0, 1, 0, 0),
            superbin_t(0, 896, 0, 75, 4, 0, 1, 0, 0),      superbin_t(1, 0, 0, 76, 4, 0, 1, 0, 0),        superbin_t(1, 256, 0, 77, 5, 0, 1, 0, 0),      superbin_t(1, 512, 0, 78, 5, 0, 1, 0, 0),      superbin_t(1, 768, 0, 79, 5, 0, 1, 0, 0),
            superbin_t(2, 0, 0, 80, 5, 0, 1, 0, 0),        superbin_t(2, 512, 0, 81, 6, 0, 1, 0, 0),      superbin_t(3, 0, 0, 82, 6, 0, 1, 0, 0),        superbin_t(3, 512, 0, 83, 6, 0, 1, 0, 0),      superbin_t(4, 0, 0, 84, 6, 0, 1, 0, 0),
            superbin_t(5, 0, 0, 85, 7, 0, 1, 0, 0),        superbin_t(6, 0, 0, 86, 7, 0, 1, 0, 0),        superbin_t(7, 0, 0, 87, 7, 0, 1, 0, 0),        superbin_t(8, 0, 0, 88, 7, 0, 1, 0, 0),        superbin_t(10, 0, 0, 89, 8, 0, 1, 0, 0),
            superbin_t(12, 0, 0, 90, 8, 0, 1, 0, 0),       superbin_t(14, 0, 0, 91, 8, 0, 1, 0, 0),       superbin_t(16, 0, 0, 92, 8, 0, 1, 0, 0),       superbin_t(20, 0, 0, 93, 9, 0, 1, 0, 0),       superbin_t(24, 0, 0, 94, 9, 0, 1, 0, 0),
            superbin_t(28, 0, 0, 95, 9, 0, 1, 0, 0),       superbin_t(32, 0, 0, 96, 9, 0, 1, 0, 0),       superbin_t(40, 0, 0, 97, 10, 0, 1, 0, 0),      superbin_t(48, 0, 0, 98, 10, 0, 1, 0, 0),      superbin_t(56, 0, 0, 99, 10, 0, 1, 0, 0),
            superbin_t(64, 0, 0, 100, 10, 0, 1, 0, 0),     superbin_t(80, 0, 0, 101, 11, 0, 1, 0, 0),     superbin_t(96, 0, 0, 102, 11, 0, 1, 0, 0),     superbin_t(112, 0, 0, 103, 11, 0, 1, 0, 0),    superbin_t(128, 0, 0, 104, 11, 0, 1, 0, 0),
            superbin_t(160, 0, 0, 105, 12, 0, 1, 0, 0),    superbin_t(192, 0, 0, 106, 12, 0, 1, 0, 0),    superbin_t(224, 0, 0, 107, 12, 0, 1, 0, 0),    superbin_t(256, 0, 0, 108, 12, 0, 1, 0, 0),    superbin_t(320, 0, 0, 109, 13, 0, 1, 0, 0),
            superbin_t(384, 0, 0, 110, 13, 0, 1, 0, 0),    superbin_t(448, 0, 0, 111, 13, 0, 1, 0, 0),
        };

        static const s32    c_num_allocators               = 14;
        static superalloc_t c_allocators[c_num_allocators] = {
            superalloc_t(65536),   superalloc_t(131072),   superalloc_t(262144),   superalloc_t(524288),   superalloc_t(1048576),   superalloc_t(2097152),   superalloc_t(4194304),
            superalloc_t(8388608), superalloc_t(16777216), superalloc_t(33554432), superalloc_t(67108864), superalloc_t(134217728), superalloc_t(268435456), superalloc_t(536870912),
        };

        static const u64 c_address_range = 128 * xGB;

        static const u32 c_internal_heap_address_range = 32 * xMB;
        static const u32 c_internal_heap_pre_size      = 2 * xMB;
        static const u32 c_internal_fsa_address_range  = 32 * xMB;
        static const u32 c_internal_fsa_pre_size       = 2 * xMB;

        static superallocator_config_t get_config()
        {
            return superallocator_config_t(c_num_bins, c_asbins, c_num_allocators, c_allocators, c_address_range, c_internal_heap_address_range, c_internal_heap_pre_size, c_internal_fsa_address_range, c_internal_fsa_pre_size);
        }

        static inline s32 size2bin(u32 size)
        {
            s32 w = xcountLeadingZeros(size);
            u32 f = (u32)0x80000000 >> w;
            u32 r = 0xFFFFFFFF << (29 - w);
            u32 t = ((f - 1) >> 2);
            size  = (size + t) & ~t;
            int i = (int)((size & r) >> (29 - w)) + ((29 - w) * 4);
            return i;
        }

    }; // namespace superallocator_config_desktop_app_25p_t

    namespace superallocator_config_desktop_app_10p_t
    {
        // 10% allocation waste
        static const s32        c_num_bins           = 216;
        static const superbin_t c_asbins[c_num_bins] = {
            superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),   superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),   superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),   superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),   superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),
            superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),   superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),   superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),   superbin_t(0, 0, 8, 8, 0, 1, 8192, 32, 512),   superbin_t(0, 0, 9, 12, 0, 1, 7281, 32, 512),
            superbin_t(0, 0, 10, 12, 0, 1, 6553, 32, 512), superbin_t(0, 0, 11, 12, 0, 1, 5957, 32, 512), superbin_t(0, 0, 12, 12, 0, 1, 5461, 32, 512), superbin_t(0, 0, 13, 16, 0, 1, 5041, 32, 512), superbin_t(0, 0, 14, 16, 0, 1, 4681, 32, 512),
            superbin_t(0, 0, 15, 16, 0, 1, 4369, 32, 512), superbin_t(0, 0, 16, 16, 0, 1, 4096, 16, 256), superbin_t(0, 0, 18, 18, 0, 1, 3640, 16, 256), superbin_t(0, 0, 20, 18, 0, 1, 3276, 16, 256), superbin_t(0, 0, 22, 20, 0, 1, 2978, 16, 256),
            superbin_t(0, 0, 24, 20, 0, 1, 2730, 16, 256), superbin_t(0, 0, 26, 22, 0, 1, 2520, 16, 256), superbin_t(0, 0, 28, 22, 0, 1, 2340, 16, 256), superbin_t(0, 0, 30, 24, 0, 1, 2184, 16, 256), superbin_t(0, 0, 32, 24, 0, 1, 2048, 8, 128),
            superbin_t(0, 0, 36, 25, 0, 1, 1820, 8, 128),  superbin_t(0, 0, 40, 26, 0, 1, 1638, 8, 128),  superbin_t(0, 0, 44, 27, 0, 1, 1489, 8, 128),  superbin_t(0, 0, 48, 28, 0, 1, 1365, 8, 128),  superbin_t(0, 0, 52, 29, 0, 1, 1260, 8, 128),
            superbin_t(0, 0, 56, 30, 0, 1, 1170, 8, 128),  superbin_t(0, 0, 60, 31, 0, 1, 1092, 8, 128),  superbin_t(0, 0, 64, 32, 0, 1, 1024, 4, 64),   superbin_t(0, 0, 72, 33, 0, 1, 910, 4, 64),    superbin_t(0, 0, 80, 34, 0, 1, 819, 4, 64),
            superbin_t(0, 0, 88, 35, 0, 1, 744, 4, 64),    superbin_t(0, 0, 96, 36, 0, 1, 682, 4, 64),    superbin_t(0, 0, 104, 37, 0, 1, 630, 4, 64),   superbin_t(0, 0, 112, 38, 0, 1, 585, 4, 64),   superbin_t(0, 0, 120, 39, 0, 1, 546, 4, 64),
            superbin_t(0, 0, 128, 40, 0, 1, 512, 2, 32),   superbin_t(0, 0, 144, 41, 0, 1, 455, 2, 32),   superbin_t(0, 0, 160, 42, 0, 1, 409, 2, 32),   superbin_t(0, 0, 176, 43, 0, 1, 372, 2, 32),   superbin_t(0, 0, 192, 44, 0, 1, 341, 2, 32),
            superbin_t(0, 0, 208, 45, 0, 1, 315, 2, 32),   superbin_t(0, 0, 224, 46, 0, 1, 292, 2, 32),   superbin_t(0, 0, 240, 47, 0, 1, 273, 2, 32),   superbin_t(0, 0, 256, 48, 0, 1, 256, 2, 16),   superbin_t(0, 0, 288, 49, 0, 1, 227, 2, 16),
            superbin_t(0, 0, 320, 50, 0, 1, 204, 2, 16),   superbin_t(0, 0, 352, 51, 0, 1, 186, 2, 16),   superbin_t(0, 0, 384, 52, 0, 1, 170, 2, 16),   superbin_t(0, 0, 416, 53, 0, 1, 157, 2, 16),   superbin_t(0, 0, 448, 54, 0, 1, 146, 2, 16),
            superbin_t(0, 0, 480, 55, 0, 1, 136, 2, 16),   superbin_t(0, 0, 512, 56, 0, 1, 128, 2, 8),    superbin_t(0, 0, 576, 57, 0, 1, 113, 2, 8),    superbin_t(0, 0, 640, 58, 0, 1, 102, 2, 8),    superbin_t(0, 0, 704, 59, 0, 1, 93, 2, 8),
            superbin_t(0, 0, 768, 60, 0, 1, 85, 2, 8),     superbin_t(0, 0, 832, 61, 0, 1, 78, 2, 8),     superbin_t(0, 0, 896, 62, 0, 1, 73, 2, 8),     superbin_t(0, 0, 960, 63, 0, 1, 68, 2, 8),     superbin_t(0, 1, 0, 64, 0, 1, 64, 2, 4),
            superbin_t(0, 1, 128, 65, 1, 1, 113, 2, 8),    superbin_t(0, 1, 256, 66, 0, 1, 51, 2, 4),     superbin_t(0, 1, 384, 67, 1, 1, 93, 2, 8),     superbin_t(0, 1, 512, 68, 1, 1, 85, 2, 8),     superbin_t(0, 1, 640, 69, 0, 1, 39, 2, 4),
            superbin_t(0, 1, 768, 70, 1, 1, 73, 2, 8),     superbin_t(0, 1, 896, 71, 0, 1, 34, 2, 4),     superbin_t(0, 2, 0, 72, 0, 1, 32, 0, 0),       superbin_t(0, 2, 256, 73, 2, 1, 113, 2, 8),    superbin_t(0, 2, 512, 74, 1, 1, 51, 2, 4),
            superbin_t(0, 2, 768, 75, 2, 1, 93, 2, 8),     superbin_t(0, 3, 0, 76, 2, 1, 85, 2, 8),       superbin_t(0, 3, 256, 77, 1, 1, 39, 2, 4),     superbin_t(0, 3, 512, 78, 2, 1, 73, 2, 8),     superbin_t(0, 3, 768, 79, 0, 1, 17, 0, 0),
            superbin_t(0, 4, 0, 80, 0, 1, 16, 0, 0),       superbin_t(0, 4, 512, 81, 3, 1, 113, 2, 8),    superbin_t(0, 5, 0, 82, 2, 1, 51, 2, 4),       superbin_t(0, 5, 512, 83, 3, 1, 93, 2, 8),     superbin_t(0, 6, 0, 84, 2, 1, 32, 2, 4),
            superbin_t(0, 6, 512, 85, 2, 1, 39, 2, 4),     superbin_t(0, 7, 0, 86, 3, 1, 73, 2, 8),       superbin_t(0, 7, 512, 87, 1, 1, 17, 0, 0),     superbin_t(0, 8, 0, 88, 0, 1, 8, 0, 0),        superbin_t(0, 9, 0, 89, 4, 1, 113, 2, 8),
            superbin_t(0, 10, 0, 90, 3, 1, 51, 2, 4),      superbin_t(0, 11, 0, 91, 3, 1, 29, 2, 4),      superbin_t(0, 12, 0, 92, 2, 1, 16, 0, 0),      superbin_t(0, 13, 0, 93, 3, 1, 39, 2, 4),      superbin_t(0, 14, 0, 94, 3, 1, 32, 2, 4),
            superbin_t(0, 15, 0, 95, 2, 1, 17, 0, 0),      superbin_t(0, 16, 0, 96, 0, 1, 4, 0, 0),       superbin_t(0, 18, 0, 97, 4, 1, 53, 2, 4),      superbin_t(0, 20, 0, 98, 3, 1, 16, 0, 0),      superbin_t(0, 22, 0, 99, 4, 1, 32, 2, 4),
            superbin_t(0, 24, 0, 100, 2, 1, 8, 0, 0),      superbin_t(0, 26, 0, 101, 4, 1, 39, 2, 4),     superbin_t(0, 28, 0, 102, 3, 1, 16, 0, 0),     superbin_t(0, 30, 0, 103, 3, 1, 17, 0, 0),     superbin_t(0, 32, 0, 104, 0, 1, 2, 0, 0),
            superbin_t(0, 36, 0, 105, 4, 1, 23, 0, 0),     superbin_t(0, 40, 0, 106, 3, 1, 8, 0, 0),      superbin_t(0, 44, 0, 107, 4, 1, 16, 0, 0),     superbin_t(0, 48, 0, 108, 2, 1, 4, 0, 0),      superbin_t(0, 52, 0, 109, 4, 1, 16, 0, 0),
            superbin_t(0, 56, 0, 110, 3, 1, 8, 0, 0),      superbin_t(0, 60, 0, 111, 4, 1, 17, 0, 0),     superbin_t(0, 64, 0, 112, 0, 0, 1, 0, 0),      superbin_t(0, 72, 0, 113, 4, 1, 8, 0, 0),      superbin_t(0, 80, 0, 114, 3, 1, 4, 0, 0),
            superbin_t(0, 88, 0, 115, 4, 1, 8, 0, 0),      superbin_t(0, 96, 0, 116, 2, 1, 2, 0, 0),      superbin_t(0, 104, 0, 117, 4, 1, 8, 0, 0),     superbin_t(0, 112, 0, 118, 3, 1, 4, 0, 0),     superbin_t(0, 120, 0, 119, 4, 1, 8, 0, 0),
            superbin_t(0, 128, 0, 120, 1, 0, 1, 0, 0),     superbin_t(0, 144, 0, 121, 4, 1, 4, 0, 0),     superbin_t(0, 160, 0, 122, 3, 1, 2, 0, 0),     superbin_t(0, 176, 0, 123, 4, 1, 4, 0, 0),     superbin_t(0, 192, 0, 124, 2, 0, 1, 0, 0),
            superbin_t(0, 208, 0, 125, 4, 1, 4, 0, 0),     superbin_t(0, 224, 0, 126, 3, 1, 2, 0, 0),     superbin_t(0, 240, 0, 127, 4, 1, 4, 0, 0),     superbin_t(0, 256, 0, 128, 2, 0, 1, 0, 0),     superbin_t(0, 288, 0, 129, 4, 1, 2, 0, 0),
            superbin_t(0, 320, 0, 130, 3, 0, 1, 0, 0),     superbin_t(0, 352, 0, 131, 4, 1, 2, 0, 0),     superbin_t(0, 384, 0, 132, 3, 0, 1, 0, 0),     superbin_t(0, 416, 0, 133, 4, 1, 2, 0, 0),     superbin_t(0, 448, 0, 134, 3, 0, 1, 0, 0),
            superbin_t(0, 480, 0, 135, 4, 1, 2, 0, 0),     superbin_t(0, 512, 0, 136, 3, 0, 1, 0, 0),     superbin_t(0, 576, 0, 137, 4, 0, 1, 0, 0),     superbin_t(0, 640, 0, 138, 4, 0, 1, 0, 0),     superbin_t(0, 704, 0, 139, 4, 0, 1, 0, 0),
            superbin_t(0, 768, 0, 140, 4, 0, 1, 0, 0),     superbin_t(0, 832, 0, 141, 4, 0, 1, 0, 0),     superbin_t(0, 896, 0, 142, 4, 0, 1, 0, 0),     superbin_t(0, 960, 0, 143, 4, 0, 1, 0, 0),     superbin_t(1, 0, 0, 144, 4, 0, 1, 0, 0),
            superbin_t(1, 128, 0, 145, 5, 0, 1, 0, 0),     superbin_t(1, 256, 0, 146, 5, 0, 1, 0, 0),     superbin_t(1, 384, 0, 147, 5, 0, 1, 0, 0),     superbin_t(1, 512, 0, 148, 5, 0, 1, 0, 0),     superbin_t(1, 640, 0, 149, 5, 0, 1, 0, 0),
            superbin_t(1, 768, 0, 150, 5, 0, 1, 0, 0),     superbin_t(1, 896, 0, 151, 5, 0, 1, 0, 0),     superbin_t(2, 0, 0, 152, 5, 0, 1, 0, 0),       superbin_t(2, 256, 0, 153, 6, 0, 1, 0, 0),     superbin_t(2, 512, 0, 154, 6, 0, 1, 0, 0),
            superbin_t(2, 768, 0, 155, 6, 0, 1, 0, 0),     superbin_t(3, 0, 0, 156, 6, 0, 1, 0, 0),       superbin_t(3, 256, 0, 157, 6, 0, 1, 0, 0),     superbin_t(3, 512, 0, 158, 6, 0, 1, 0, 0),     superbin_t(3, 768, 0, 159, 6, 0, 1, 0, 0),
            superbin_t(4, 0, 0, 160, 6, 0, 1, 0, 0),       superbin_t(4, 512, 0, 161, 7, 0, 1, 0, 0),     superbin_t(5, 0, 0, 162, 7, 0, 1, 0, 0),       superbin_t(5, 512, 0, 163, 7, 0, 1, 0, 0),     superbin_t(6, 0, 0, 164, 7, 0, 1, 0, 0),
            superbin_t(6, 512, 0, 165, 7, 0, 1, 0, 0),     superbin_t(7, 0, 0, 166, 7, 0, 1, 0, 0),       superbin_t(7, 512, 0, 167, 7, 0, 1, 0, 0),     superbin_t(8, 0, 0, 168, 7, 0, 1, 0, 0),       superbin_t(9, 0, 0, 169, 8, 0, 1, 0, 0),
            superbin_t(10, 0, 0, 170, 8, 0, 1, 0, 0),      superbin_t(11, 0, 0, 171, 8, 0, 1, 0, 0),      superbin_t(12, 0, 0, 172, 8, 0, 1, 0, 0),      superbin_t(13, 0, 0, 173, 8, 0, 1, 0, 0),      superbin_t(14, 0, 0, 174, 8, 0, 1, 0, 0),
            superbin_t(15, 0, 0, 175, 8, 0, 1, 0, 0),      superbin_t(16, 0, 0, 176, 8, 0, 1, 0, 0),      superbin_t(18, 0, 0, 177, 9, 0, 1, 0, 0),      superbin_t(20, 0, 0, 178, 9, 0, 1, 0, 0),      superbin_t(22, 0, 0, 179, 9, 0, 1, 0, 0),
            superbin_t(24, 0, 0, 180, 9, 0, 1, 0, 0),      superbin_t(26, 0, 0, 181, 9, 0, 1, 0, 0),      superbin_t(28, 0, 0, 182, 9, 0, 1, 0, 0),      superbin_t(30, 0, 0, 183, 9, 0, 1, 0, 0),      superbin_t(32, 0, 0, 184, 9, 0, 1, 0, 0),
            superbin_t(36, 0, 0, 185, 10, 0, 1, 0, 0),     superbin_t(40, 0, 0, 186, 10, 0, 1, 0, 0),     superbin_t(44, 0, 0, 187, 10, 0, 1, 0, 0),     superbin_t(48, 0, 0, 188, 10, 0, 1, 0, 0),     superbin_t(52, 0, 0, 189, 10, 0, 1, 0, 0),
            superbin_t(56, 0, 0, 190, 10, 0, 1, 0, 0),     superbin_t(60, 0, 0, 191, 10, 0, 1, 0, 0),     superbin_t(64, 0, 0, 192, 10, 0, 1, 0, 0),     superbin_t(72, 0, 0, 193, 11, 0, 1, 0, 0),     superbin_t(80, 0, 0, 194, 11, 0, 1, 0, 0),
            superbin_t(88, 0, 0, 195, 11, 0, 1, 0, 0),     superbin_t(96, 0, 0, 196, 11, 0, 1, 0, 0),     superbin_t(104, 0, 0, 197, 11, 0, 1, 0, 0),    superbin_t(112, 0, 0, 198, 11, 0, 1, 0, 0),    superbin_t(120, 0, 0, 199, 11, 0, 1, 0, 0),
            superbin_t(128, 0, 0, 200, 11, 0, 1, 0, 0),    superbin_t(144, 0, 0, 201, 12, 0, 1, 0, 0),    superbin_t(160, 0, 0, 202, 12, 0, 1, 0, 0),    superbin_t(176, 0, 0, 203, 12, 0, 1, 0, 0),    superbin_t(192, 0, 0, 204, 12, 0, 1, 0, 0),
            superbin_t(208, 0, 0, 205, 12, 0, 1, 0, 0),    superbin_t(224, 0, 0, 206, 12, 0, 1, 0, 0),    superbin_t(240, 0, 0, 207, 12, 0, 1, 0, 0),    superbin_t(256, 0, 0, 208, 12, 0, 1, 0, 0),    superbin_t(288, 0, 0, 209, 13, 0, 1, 0, 0),
            superbin_t(320, 0, 0, 210, 13, 0, 1, 0, 0),    superbin_t(352, 0, 0, 211, 13, 0, 1, 0, 0),    superbin_t(384, 0, 0, 212, 13, 0, 1, 0, 0),    superbin_t(416, 0, 0, 213, 13, 0, 1, 0, 0),    superbin_t(448, 0, 0, 214, 13, 0, 1, 0, 0),
            superbin_t(480, 0, 0, 215, 13, 0, 1, 0, 0),
        };
        static const s32    c_num_allocators               = 14;
        static superalloc_t c_allocators[c_num_allocators] = {
            superalloc_t(65536),   superalloc_t(131072),   superalloc_t(262144),   superalloc_t(524288),   superalloc_t(1048576),   superalloc_t(2097152),   superalloc_t(4194304),
            superalloc_t(8388608), superalloc_t(16777216), superalloc_t(33554432), superalloc_t(67108864), superalloc_t(134217728), superalloc_t(268435456), superalloc_t(536870912),
        };

        static const u64 c_address_range = 128 * xGB;

        static const u32 c_internal_heap_address_range = 32 * xMB;
        static const u32 c_internal_heap_pre_size      = 2 * xMB;
        static const u32 c_internal_fsa_address_range  = 32 * xMB;
        static const u32 c_internal_fsa_pre_size       = 2 * xMB;

        static superallocator_config_t get_config()
        {
            return superallocator_config_t(c_num_bins, c_asbins, c_num_allocators, c_allocators, c_address_range, c_internal_heap_address_range, c_internal_heap_pre_size, c_internal_fsa_address_range, c_internal_fsa_pre_size);
        }

        static inline s32 size2bin(u32 size)
        {
            s32 w = xcountLeadingZeros(size);
            u32 f = (u32)0x80000000 >> w;
            u32 r = 0xFFFFFFFF << (28 - w);
            u32 t = ((f - 1) >> 3);
            size  = (size + t) & ~t;
            int i = (int)((size & r) >> (28 - w)) + ((28 - w) * 8);
            return i;
        }

    }; // namespace superallocator_config_desktop_app_10p_t

    namespace superallocator_config = superallocator_config_desktop_app_10p_t;
    //namespace superallocator_config = superallocator_config_desktop_app_25p_t;

    class superallocator_t
    {
    public:
        superallocator_t()
            : m_config()
            , m_allocators(nullptr)
            , m_vmem(nullptr)
            , m_internal_heap()
            , m_internal_fsa()
        {
        }

        void  initialize(xvmem* vmem, superallocator_config_t const& config);
        void  deinitialize();
        void* allocate(u32 size, u32 alignment);
        u32   deallocate(void* ptr);
        u32   get_size(void* ptr) const;

        superallocator_config_t m_config;
        superchunks_t           m_chunks;
        superalloc_t*           m_allocators;
        xvmem*                  m_vmem;
        superheap_t             m_internal_heap;
        superfsa_t              m_internal_fsa;
    };

    void superallocator_t::initialize(xvmem* vmem, superallocator_config_t const& config)
    {
        m_config = config;
        m_vmem   = vmem;
        m_internal_heap.initialize(m_vmem, m_config.m_internal_heap_address_range, m_config.m_internal_heap_pre_size);
        m_internal_fsa.initialize(m_internal_heap, m_vmem, m_config.m_internal_fsa_address_range, m_config.m_internal_fsa_pre_size);
        m_chunks.initialize(vmem, config.m_address_range, m_internal_heap);

        llhead_t* used_chunk_list_per_size = (llhead_t*)m_internal_heap.allocate(sizeof(llhead_t) * m_config.m_num_bins);
        for (s32 i = 0; i < m_config.m_num_bins; ++i)
        {
            used_chunk_list_per_size[i].reset();
        }

        m_allocators = (superalloc_t*)m_internal_heap.allocate(sizeof(superalloc_t) * config.m_num_allocators);
        for (s32 i = 0; i < m_config.m_num_allocators; ++i)
        {
            m_allocators[i] = superalloc_t(config.m_allocators[i], used_chunk_list_per_size);
        }

        for (s32 i = 0; i < m_config.m_num_allocators; ++i)
        {
            m_allocators[i].initialize(&m_chunks, m_internal_heap, m_internal_fsa);
        }

        u32 const bin_index2 = superallocator_config::size2bin(0xF0);

        // sanity check on the superbin_t config
        for (s32 s = 0; s < m_config.m_num_bins; s++)
        {
            u32 const rs            = m_config.m_asbins[s].m_alloc_bin_index;
            u32 const size          = m_config.m_asbins[rs].m_alloc_size;
            u32 const bin_index     = superallocator_config::size2bin(size);
            u32 const bin_reindex   = m_config.m_asbins[bin_index].m_alloc_bin_index;
            u32 const bin_allocsize = m_config.m_asbins[bin_reindex].m_alloc_size;
            ASSERT(size <= bin_allocsize);
        }
    }

    void superallocator_t::deinitialize()
    {
        m_internal_fsa.deinitialize(m_internal_heap);
        m_internal_heap.deinitialize();
        m_chunks.deinitialize(m_internal_heap);
        m_vmem = nullptr;
    }

    void* superallocator_t::allocate(u32 size, u32 alignment)
    {
        size                 = xalignUp(size, alignment);
        u32 const binindex   = m_config.m_asbins[superallocator_config::size2bin(size)].m_alloc_bin_index;
        s32 const allocindex = m_config.m_asbins[binindex].m_alloc_index;
        ASSERT(size <= m_config.m_asbins[binindex].m_alloc_size);
        ASSERT(m_config.m_asbins[binindex].m_alloc_bin_index == binindex);
        void* ptr = m_allocators[allocindex].allocate(m_internal_fsa, size, m_config.m_asbins[binindex]);
        ASSERT(ptr >= m_chunks.m_address_base && ptr < ((xbyte*)m_chunks.m_address_base + m_chunks.m_address_range));
        return ptr;
    }

    u32 superallocator_t::deallocate(void* ptr)
    {
        if (ptr == nullptr)
            return 0;
        ASSERT(ptr >= m_chunks.m_address_base && ptr < ((xbyte*)m_chunks.m_address_base + m_chunks.m_address_range));
        u32 const               page_index = m_chunks.address_to_page_index(ptr);
        superchunks_t::index_t  index      = m_chunks.page_index_to_info(page_index);
        superchunks_t::chunk_t* chunk      = m_chunks.get_chunk_by_index(index.m_chunk_index);
        u32 const               binindex   = chunk->m_bin_index;
        u32 const               allocindex = m_config.m_asbins[binindex].m_alloc_index;
        u32 const               size       = m_allocators[allocindex].deallocate(m_internal_fsa, ptr, index.m_chunk_index, m_config.m_asbins[binindex]);
        ASSERT(size <= m_config.m_asbins[binindex].m_alloc_size);
        return size;
    }

    u32 superallocator_t::get_size(void* ptr) const
    {
        if (ptr == nullptr)
            return 0;
        u32 const               page_index = m_chunks.address_to_page_index(ptr);
        superchunks_t::index_t  index      = m_chunks.page_index_to_info(page_index);
        superchunks_t::chunk_t* chunk      = m_chunks.get_chunk_by_index(index.m_chunk_index);
        u32 const               binindex   = chunk->m_bin_index;
        if (m_config.m_asbins[binindex].m_use_binmap == 1)
        {
            return m_config.m_asbins[binindex].m_alloc_size;
        }
        else
        {
            superchunks_t::block_t* block = m_chunks.get_block_from_index(index.m_block_index);
            return block->m_chunks_physical_pages[index.m_block_chunk_index] * m_chunks.m_page_size;
        }
    }
} // namespace xcore
