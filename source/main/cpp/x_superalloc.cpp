#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_doubly_linked_list.h"
#include "xvmem/private/x_binmap.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    static inline u64   alignto(u64 value, u64 alignment) { return (value + (alignment - 1)) & ~(alignment - 1); }
    static inline u32   alignto(u32 value, u32 alignment) { return (value + (alignment - 1)) & ~(alignment - 1); }
    static inline void* toaddress(void* base, u64 offset) { return (void*)((u64)base + offset); }
    static inline u64   todistance(void* base, void* ptr) { return (u64)((u64)ptr + (u64)base); }

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
            u32 const pages_to_commit = (u32)(alignto(size_to_pre_allocate, (u64)m_page_size) / m_page_size);
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
        size        = alignto(size, m_size_alignment);
        u64 ptr_max = ((u64)m_page_count_current * m_page_size);
        if ((m_ptr + size) > ptr_max)
        {
            // add more pages
            u32 const page_count           = (u32)(alignto(m_ptr + size, (u64)m_page_size) / (u64)m_page_size);
            u32 const page_count_to_commit = page_count - m_page_count_current;
            u64       commit_base          = ((u64)m_page_count_current * m_page_size);
            m_vmem->commit(toaddress(m_address, commit_base), m_page_size, page_count_to_commit);
            m_page_count_current += page_count_to_commit;
        }
        u64 const offset = m_ptr;
        m_ptr += size;
        return toaddress(m_address, offset);
    }

    // Book-keeping for chunks requires to allocate/deallocate blocks of data
    // Power-of-2 sizes, minimum size = 8, maximum_size = 16384
    // @note: returned index to the user is u32[u16(page-index):u16(item-index)]
    class superfsa_t
    {
    public:
        void initialize(superheap_t& heap, xvmem* vmem, u64 address_range, u32 num_pages_to_cache);
        void deinitialize(superheap_t& heap);

        u32  alloc(u32 size);
        void dealloc(u32 index);

        struct page_t : public llnode_t
        {
            u16      m_item_size;
            u16      m_item_index;
            u16      m_item_count;
            u16      m_item_max;
            u16      m_dummy;
            llhead_t m_item_freelist;

            void initialize(u32 size, u32 pagesize)
            {
                m_item_size  = size;
                m_item_index = 0;
                m_item_count = 0;
                m_item_max   = pagesize / size;
                m_dummy      = 0x10DA;
                m_item_freelist.reset();
            }

            inline bool is_full() const { return m_item_count == m_item_max; }
            inline bool is_empty() const { return m_item_count == 0; }
            inline u32  ptr2idx(void* const ptr, void* const elem) const { return (u32)(((u64)elem - (u64)ptr) / m_item_size); }
            inline u32* idx2ptr(void* const ptr, u32 const index) const { return (u32*)((xbyte*)ptr + (index * (u32)m_item_size)); }

            void* allocate(void* page_address)
            {
                m_item_count++;
                if (m_item_freelist.is_nil() == false)
                {
                    llindex_t const  ielem = m_item_freelist;
                    llindex_t* const pelem = (llindex_t*)idx2ptr(page_address, ielem.get());
                    m_item_freelist        = pelem[0];
                    return (void*)pelem;
                }
                else if (m_item_index < m_item_max)
                {
                    u16 const ielem = m_item_index++;
                    return (void*)idx2ptr(page_address, ielem);
                }
                // panic
                m_item_count -= 1;
                return nullptr;
            }
            void deallocate(void* page_address, u16 item_index)
            {
                llindex_t* const pelem = (llindex_t*)idx2ptr(page_address, item_index);
                pelem[0]               = m_item_freelist;
                m_item_freelist        = item_index;
                m_item_count -= 1;
            }
        };

        inline void* pageindex_to_pageaddress(u16 ipage) const { return toaddress(m_address, (u64)ipage * m_page_size); }
        inline void* idx2ptr(u32 i) const
        {
            u16 const           pageindex = i >> 16;
            u16 const           itemindex = i & 0xFFFF;
            page_t const* const ppage     = &m_page_array[pageindex];
            void* const         paddr     = pageindex_to_pageaddress(pageindex);
            return ppage->idx2ptr(paddr, itemindex);
        }

        inline u32 ptr2idx(void* ptr) const
        {
            u32 const           pageindex = (u32)(todistance(m_address, ptr) / m_page_size);
            page_t const* const ppage     = &m_page_array[pageindex];
            void* const         paddr     = pageindex_to_pageaddress(pageindex);
            u32 const           itemindex = ppage->ptr2idx(paddr, ptr);
            return (pageindex << 16) | (itemindex & 0xFFFF);
        }

        xvmem*           m_vmem;
        void*            m_address;
        u64              m_address_range;
        u32              m_page_count;
        u32              m_page_size;
        page_t*          m_page_array;
        llist_t          m_free_page_list;
        llist_t          m_cached_page_list;
        static const s32 c_max_num_sizes = 16;
        llhead_t         m_used_page_list_per_size[c_max_num_sizes];
    };

    void superfsa_t::initialize(superheap_t& heap, xvmem* vmem, u64 address_range, u32 num_pages_to_cache)
    {
        m_vmem         = vmem;
        u32 attributes = 0;
        m_vmem->reserve(address_range, m_page_size, attributes, m_address);
        m_address_range = address_range;
        m_page_count    = (u32)(address_range / (u64)m_page_size);
        m_page_array    = (page_t*)heap.allocate(m_page_count * sizeof(page_t));
        m_free_page_list.initialize(m_page_array + num_pages_to_cache, m_page_count - num_pages_to_cache, m_page_count);
        for (u32 i = 0; i < m_page_count; i++)
            m_page_array[i].initialize(8, 0);
        for (u32 i = 0; i < c_max_num_sizes; i++)
            m_used_page_list_per_size[i].reset();

        if (num_pages_to_cache > 0)
        {
            m_cached_page_list.initialize(m_page_array, num_pages_to_cache, num_pages_to_cache);
            m_vmem->commit(m_address, m_page_size, num_pages_to_cache);
        }
    }

    void superfsa_t::deinitialize(superheap_t& heap)
    {
        // let the heap destroy itself, we do not need to deallocate
        // NOTE: Do we need to decommit the cached pages, or is 'release' enough?
        m_vmem->release(m_address, m_address_range);
    }

    u32 superfsa_t::alloc(u32 size)
    {
        size               = xalign(size, 8);
        size               = xceilpo2(size);
        s32 const c        = (xcountTrailingZeros(size) - 3) - 1;
        void*     paddress = nullptr;
        page_t*   ppage    = nullptr;
        u32       ipage    = 0xffffffff;
        ASSERT(c < c_max_num_sizes);
        if (m_used_page_list_per_size[c].is_nil())
        {
            // Get a page and initialize that page for this size
            if (!m_cached_page_list.is_empty())
            {
                ipage = m_cached_page_list.remove_headi(m_page_array).get();
                ppage = &m_page_array[ipage];
                ppage->initialize(size, m_page_size);
            }
            else if (!m_free_page_list.is_empty())
            {
                ipage = m_free_page_list.remove_headi(m_page_array).get();
                ppage = &m_page_array[ipage];
                ppage->initialize(size, m_page_size);
                m_vmem->commit(ppage, m_page_size, 1);
            }
        }
        if (ppage != nullptr)
        {
            paddress  = toaddress(m_address, (u64)ipage * m_page_size);
            void* ptr = ppage->allocate(paddress);
            if (ppage->is_empty())
            {
                m_used_page_list_per_size[c].remove_head(m_page_array);
            }
            return ptr2idx(ptr);
        }
        else
        {
            return 0xffffffff;
        }
    }

    void superfsa_t::dealloc(u32 i)
    {
        u16 const     pageindex = i >> 16;
        u16 const     itemindex = i & 0xFFFF;
        page_t* const ppage     = &m_page_array[pageindex];
        void* const   paddr     = pageindex_to_pageaddress(pageindex);
        ppage->deallocate(paddr, itemindex);
        if (ppage->is_full())
        {
            s32 const c = (xcountTrailingZeros(ppage->m_item_size) - 3) - 1;
            ASSERT(c < c_max_num_sizes);
            m_used_page_list_per_size[c].remove_head(m_page_array);
            if (!m_cached_page_list.is_full())
            {
                m_cached_page_list.insert(m_page_array, pageindex);
            }
            else
            {
                m_vmem->decommit(paddr, m_page_size, 1);
                m_free_page_list.insert(m_page_array, pageindex);
            }
        }
    }

    struct superbin_t;

    // @superalloc manages an address range, a list of chunks and a range of allocation sizes.
    struct superalloc_t
    {
        struct chunk_t
        {
            inline chunk_t()
                : m_elem_used(0)
                , m_bin_index(0xffff)
            {
            }
            u16 m_elem_used;
            u16 m_bin_index;
        };

        superalloc_t(u64 memory_range, u64 chunksize, u32 numchunks2cache, u32 numsizes, u32 binbase)
            : m_memory_base(nullptr)
            , m_memory_range(memory_range)
            , m_chunk_size(chunksize)
            , m_chunk_cnt((u32)(memory_range / chunksize))
            , m_chunk_array(nullptr)
            , m_binmaps(nullptr)
            , m_chunk_list(nullptr)
            , m_max_chunks_to_cache(numchunks2cache)
            , m_num_sizes(numsizes)
            , m_bin_base(binbase)
            , m_used_chunk_list_per_size(nullptr)
        {
        }

        void  initialize(void* memory_base, u32 page_size, xvmem* vmem, superheap_t& heap, superfsa_t& fsa);
        void* allocate(superfsa_t& sfsa, u32 size, superbin_t const& bin);
        u32   get_chunkindex(void* ptr) const;
        u32   get_binindex(u32 chunkindex) const;
        u32   deallocate(superfsa_t& sfsa, void* ptr, u32 chunkindex, superbin_t const& bin);

        void  initialize_chunk(superfsa_t& fsa, u32 chunkindex, u32 size, superbin_t const& bin);
        void  deinitialize_chunk(superfsa_t& fsa, u32 chunkindex, superbin_t const& bin);
        void  commit_chunk(u32 chunkindex, u32 size, superbin_t const& bin);
        void  decommit_chunk(u32 chunkindex, superbin_t const& bin);
        void* allocate_from_chunk(superfsa_t& fsa, u32 chunkindex, u32 size, superbin_t const& bin, bool& chunk_is_now_full);
        u32   deallocate_from_chunk(superfsa_t& fsa, u32 chunkindex, void* ptr, superbin_t const& bin, bool& chunk_is_now_empty, bool& chunk_was_full);

        void*     m_memory_base;
        u64       m_memory_range;
        xvmem*    m_vmem;
        u32       m_page_size;
        u64       m_chunk_size;
        u32       m_chunk_cnt;
        chunk_t*  m_chunk_array;
        u32*      m_binmaps;
        llnode_t* m_chunk_list;
        llist_t   m_free_chunk_list;
        llist_t   m_cache_chunk_list;
        u32       m_max_chunks_to_cache;
        u32       m_num_sizes;
        u32       m_bin_base;
        llhead_t* m_used_chunk_list_per_size;
    };

    struct superbin_t
    {
        inline superbin_t(u32 allocsize_mb, u32 allocsize_kb, u32 allocsize_b, u8 binidx, u8 allocindex, u8 use_binmap, u16 count, u16 l1len, u16 l2len)
            : m_alloc_size((xGB * allocsize_mb) + (xKB * allocsize_kb) + (allocsize_b))
            , m_alloc_bin_index(binidx)
            , m_alloc_index(allocindex)
            , m_use_binmap(use_binmap)
            , m_alloc_count(count)
            , m_binmap_l1len(l1len)
            , m_binmap_l2len(l2len)
        {
        }

        static inline s32 size2bin(u32 size)
        {
            u32 const f = xfloorpo2(size);
            s32 const r = xcountTrailingZeros(f >> 4) * 4;
            s32 const t = xcountTrailingZeros(xalign(f, 8) >> 2);
            s32 const i = (int)((size - (f & ~((u32)32 - 1))) >> t) + r;
            ASSERT(i > 0 && i < 256);
            return i - 1;
        }

        u32 m_alloc_size;
        u32 m_alloc_bin_index : 8; // Only one indirection is allowed
        u32 m_alloc_index : 8;     // The index into the allocator that manages us
        u32 m_use_binmap : 1;      // How do we manage a chunk (binmap or page-count)
        u32 m_alloc_count;
        u16 m_binmap_l1len;
        u16 m_binmap_l2len;
    };

    class superallocator_t
    {
    public:
        void initialize(xvmem* vmem);
        void deinitialize();

        void* allocate(u32 size, u32 alignment);
        u32   deallocate(void* ptr);

        /// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
        /// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
        /// The following is a strict data-drive initialization of the bins and allocators, please know what you are doing when modifying any of this.

        static const s32 c_num_bins = 96;
        // superbin_t(allocation size MB, KB, B, bin index, allocator index, use binmap?, maximum allocation count, binmap level 1 length, binmap level 2 length)
        const superbin_t m_asbins[c_num_bins] = {
            superbin_t(0, 0, 8, 0, 0, 1, 8192, 32, 512), superbin_t(0, 0, 16, 1, 0, 1, 4096, 16, 256), superbin_t(0, 0, 24, 2, 0, 1, 2730, 16, 256), superbin_t(0, 0, 32, 3, 0, 1, 2048, 8, 128),  superbin_t(0, 0, 40, 4, 0, 1, 1638, 8, 128),
            superbin_t(0, 0, 48, 5, 0, 1, 1365, 8, 128), superbin_t(0, 0, 56, 6, 0, 1, 1170, 8, 128),  superbin_t(0, 0, 64, 7, 0, 1, 1024, 4, 64),   superbin_t(0, 0, 80, 8, 0, 1, 819, 4, 64),    superbin_t(0, 0, 96, 9, 0, 1, 682, 4, 64),
            superbin_t(0, 0, 112, 10, 0, 1, 585, 4, 64), superbin_t(0, 0, 128, 11, 0, 1, 512, 2, 32),  superbin_t(0, 0, 160, 12, 0, 1, 409, 2, 32),  superbin_t(0, 0, 192, 13, 0, 1, 341, 2, 32),  superbin_t(0, 0, 224, 14, 0, 1, 292, 2, 32),
            superbin_t(0, 0, 256, 15, 0, 1, 256, 2, 16), superbin_t(0, 0, 320, 16, 1, 1, 1024, 4, 64), superbin_t(0, 0, 384, 17, 1, 1, 1024, 4, 64), superbin_t(0, 0, 448, 18, 1, 1, 1024, 4, 64), superbin_t(0, 0, 512, 19, 1, 1, 1024, 4, 64),
            superbin_t(0, 0, 640, 20, 1, 1, 512, 2, 32), superbin_t(0, 0, 768, 21, 1, 1, 512, 2, 32),  superbin_t(0, 0, 896, 22, 1, 1, 512, 2, 32),  superbin_t(0, 1, 0, 23, 1, 1, 512, 2, 32),    superbin_t(0, 1, 256, 24, 1, 1, 256, 2, 16),
            superbin_t(0, 1, 512, 25, 1, 1, 256, 2, 16), superbin_t(0, 1, 768, 26, 1, 1, 256, 2, 16),  superbin_t(0, 2, 0, 27, 1, 1, 256, 2, 16),    superbin_t(0, 2, 512, 28, 1, 1, 128, 2, 8),   superbin_t(0, 3, 0, 29, 1, 1, 128, 2, 8),
            superbin_t(0, 3, 512, 30, 1, 1, 128, 2, 8),  superbin_t(0, 4, 0, 31, 1, 1, 128, 2, 8),     superbin_t(0, 5, 0, 32, 1, 1, 64, 2, 4),      superbin_t(0, 6, 0, 33, 1, 1, 64, 2, 4),      superbin_t(0, 7, 0, 34, 1, 1, 64, 2, 4),
            superbin_t(0, 8, 0, 35, 1, 1, 64, 2, 4),     superbin_t(0, 10, 0, 36, 1, 1, 32, 0, 0),     superbin_t(0, 12, 0, 37, 1, 1, 32, 0, 0),     superbin_t(0, 14, 0, 38, 1, 1, 32, 0, 0),     superbin_t(0, 16, 0, 39, 1, 1, 32, 0, 0),
            superbin_t(0, 20, 0, 40, 1, 1, 16, 0, 0),    superbin_t(0, 24, 0, 41, 1, 1, 16, 0, 0),     superbin_t(0, 28, 0, 42, 1, 1, 16, 0, 0),     superbin_t(0, 32, 0, 43, 1, 1, 16, 0, 0),     superbin_t(0, 40, 0, 44, 1, 1, 8, 0, 0),
            superbin_t(0, 48, 0, 45, 1, 1, 8, 0, 0),     superbin_t(0, 56, 0, 46, 1, 1, 8, 0, 0),      superbin_t(0, 64, 0, 47, 1, 1, 8, 0, 0),      superbin_t(0, 80, 0, 48, 1, 1, 4, 0, 0),      superbin_t(0, 96, 0, 49, 1, 1, 4, 0, 0),
            superbin_t(0, 112, 0, 50, 1, 1, 4, 0, 0),    superbin_t(0, 128, 0, 51, 1, 1, 4, 0, 0),     superbin_t(0, 160, 0, 52, 1, 1, 2, 0, 0),     superbin_t(0, 192, 0, 53, 1, 1, 2, 0, 0),     superbin_t(0, 224, 0, 54, 1, 1, 2, 0, 0),
            superbin_t(0, 256, 0, 55, 1, 1, 2, 0, 0),    superbin_t(0, 320, 0, 56, 2, 0, 1, 0, 0),     superbin_t(0, 384, 0, 57, 2, 0, 1, 0, 0),     superbin_t(0, 448, 0, 58, 2, 0, 1, 0, 0),     superbin_t(0, 512, 0, 59, 2, 0, 1, 0, 0),
            superbin_t(0, 640, 0, 60, 3, 0, 1, 0, 0),    superbin_t(0, 768, 0, 61, 3, 0, 1, 0, 0),     superbin_t(0, 896, 0, 62, 3, 0, 1, 0, 0),     superbin_t(1, 0, 0, 63, 3, 0, 1, 0, 0),       superbin_t(1, 256, 0, 64, 4, 0, 1, 0, 0),
            superbin_t(1, 512, 0, 65, 4, 0, 1, 0, 0),    superbin_t(1, 768, 0, 66, 4, 0, 1, 0, 0),     superbin_t(2, 0, 0, 67, 4, 0, 1, 0, 0),       superbin_t(2, 512, 0, 68, 5, 0, 1, 0, 0),     superbin_t(3, 0, 0, 69, 5, 0, 1, 0, 0),
            superbin_t(3, 512, 0, 70, 5, 0, 1, 0, 0),    superbin_t(4, 0, 0, 71, 5, 0, 1, 0, 0),       superbin_t(5, 0, 0, 72, 6, 0, 1, 0, 0),       superbin_t(6, 0, 0, 73, 6, 0, 1, 0, 0),       superbin_t(7, 0, 0, 74, 6, 0, 1, 0, 0),
            superbin_t(8, 0, 0, 75, 6, 0, 1, 0, 0),      superbin_t(10, 0, 0, 76, 7, 0, 1, 0, 0),      superbin_t(12, 0, 0, 77, 7, 0, 1, 0, 0),      superbin_t(14, 0, 0, 78, 7, 0, 1, 0, 0),      superbin_t(16, 0, 0, 79, 7, 0, 1, 0, 0),
            superbin_t(20, 0, 0, 80, 8, 0, 1, 0, 0),     superbin_t(24, 0, 0, 81, 8, 0, 1, 0, 0),      superbin_t(28, 0, 0, 82, 8, 0, 1, 0, 0),      superbin_t(32, 0, 0, 83, 8, 0, 1, 0, 0),      superbin_t(40, 0, 0, 84, 9, 0, 1, 0, 0),
            superbin_t(48, 0, 0, 85, 9, 0, 1, 0, 0),     superbin_t(56, 0, 0, 86, 9, 0, 1, 0, 0),      superbin_t(64, 0, 0, 87, 9, 0, 1, 0, 0),      superbin_t(80, 0, 0, 88, 10, 0, 1, 0, 0),     superbin_t(96, 0, 0, 89, 10, 0, 1, 0, 0),
            superbin_t(112, 0, 0, 90, 10, 0, 1, 0, 0),   superbin_t(128, 0, 0, 91, 10, 0, 1, 0, 0),    superbin_t(160, 0, 0, 92, 11, 0, 1, 0, 0),    superbin_t(192, 0, 0, 93, 11, 0, 1, 0, 0),    superbin_t(224, 0, 0, 94, 11, 0, 1, 0, 0),
            superbin_t(256, 0, 0, 95, 11, 0, 1, 0, 0)};

        static const s32 c_num_allocators               = 12;
        superalloc_t     m_allocators[c_num_allocators] = {
            superalloc_t(xMB * 128, xKB * 64, 256, 16, 0),   // memory range, chunk size, number of chunks to cache, number of sizes, bin base
            superalloc_t(xMB * 384, xKB * 512, 256, 40, 16), //
            superalloc_t(xMB * 512, xKB * 512, 2, 4, 56),    //
            superalloc_t(xGB * 1, xMB * 1, 0, 4, 60),        //
            superalloc_t(xGB * 1, xMB * 2, 0, 4, 64),        //
            superalloc_t(xGB * 1, xMB * 4, 0, 4, 68),        //
            superalloc_t(xGB * 1, xMB * 8, 0, 4, 72),        //
            superalloc_t(xGB * 1, xMB * 16, 0, 4, 76),       //
            superalloc_t(xGB * 1, xMB * 32, 0, 4, 80),       //
            superalloc_t(xGB * 1, xMB * 64, 0, 4, 84),       //
            superalloc_t(xGB * 2, xMB * 128, 0, 4, 88),      //
            superalloc_t(xGB * 4, xMB * 256, 0, 4, 92)       //
        };

        static const u64 c_address_range                     = (u64)16 * xGB;
        static const u64 c_address_divisor                   = (u64)1 * xGB;
        static const s32 c_address_map_size                  = 16;
        u8 const         m_allocator_map[c_address_map_size] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 11, 11, 11, 11};

        static const u32 c_internal_heap_address_range = 32 * xMB;
        static const u32 c_internal_heap_pre_size      = 2 * xMB;
        static const u32 c_internal_fsa_address_range  = 32 * xMB;
        static const u32 c_internal_fsa_pre_page_count = 512;

        /// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
        /// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

        void*       m_address_base;
        xvmem*      m_vmem;
        u32         m_page_size;
        superheap_t m_internal_heap;
        superfsa_t  m_internal_fsa;
    };

    void superalloc_t::initialize(void* memory_base, u32 page_size, xvmem* vmem, superheap_t& heap, superfsa_t& fsa)
    {
        m_memory_base = memory_base;
        m_page_size   = page_size;
        m_vmem        = vmem;

        m_chunk_array = (superalloc_t::chunk_t*)heap.allocate(m_chunk_cnt * sizeof(superalloc_t::chunk_t));
        m_chunk_list  = (llnode_t*)heap.allocate(m_chunk_cnt * sizeof(llnode_t));
        m_binmaps     = (u32*)heap.allocate(m_chunk_cnt * sizeof(u32));
        for (u32 i = 0; i < m_chunk_cnt; ++i)
        {
            m_chunk_array[i] = superalloc_t::chunk_t();
            m_binmaps[i]     = 0xffffffff;
        }

        m_free_chunk_list.initialize(m_chunk_list + m_max_chunks_to_cache, m_chunk_cnt - m_max_chunks_to_cache, m_chunk_cnt - m_max_chunks_to_cache);

        m_cache_chunk_list.initialize(m_chunk_list, m_max_chunks_to_cache, m_max_chunks_to_cache);
        u32 const num_physical_pages = (u32)(((u64)m_chunk_size * m_max_chunks_to_cache) / m_page_size);
        m_vmem->commit(m_memory_base, m_page_size, num_physical_pages);

        m_used_chunk_list_per_size = (llhead_t*)heap.allocate(m_num_sizes * sizeof(llhead_t));
        for (u32 i = 0; i < m_num_sizes; i++)
            m_used_chunk_list_per_size[i].reset();
    }

    void* superalloc_t::allocate(superfsa_t& sfsa, u32 size, superbin_t const& bin)
    {
        llhead_t chunkindex = m_used_chunk_list_per_size[bin.m_alloc_bin_index - m_bin_base];
        if (chunkindex.is_nil())
        {
            chunkindex = m_cache_chunk_list.remove_headi(m_chunk_list);
            if (chunkindex.is_nil())
            {
                chunkindex = m_free_chunk_list.remove_headi(m_chunk_list);
                if (chunkindex.is_nil())
                {
                    // panic; reason OOO
                    return nullptr;
                }
                initialize_chunk(sfsa, chunkindex.get(), size, bin);
                commit_chunk(chunkindex.get(), size, bin);
            }
            else
            {
                initialize_chunk(sfsa, chunkindex.get(), size, bin);
                if (bin.m_use_binmap == 0)
                {
                    commit_chunk(chunkindex.get(), size, bin);
                }
            }
            m_used_chunk_list_per_size[bin.m_alloc_bin_index - m_bin_base] = chunkindex;
        }

        bool        chunk_is_now_empty = false;
        void* const ptr                = allocate_from_chunk(sfsa, chunkindex.get(), size, bin, chunk_is_now_empty);
        if (chunk_is_now_empty)
        {
            m_used_chunk_list_per_size[bin.m_alloc_bin_index - m_bin_base].remove_headi(m_chunk_list);
        }
        return ptr;
    }

    u32 superalloc_t::get_chunkindex(void* ptr) const { return (u32)(todistance(m_memory_base, ptr) / m_chunk_size); }
    u32 superalloc_t::get_binindex(u32 chunkindex) const
    {
        ASSERT(chunkindex < m_chunk_cnt);
        chunk_t& chunk = m_chunk_array[chunkindex];
        return chunk.m_bin_index;
    }

    u32 superalloc_t::deallocate(superfsa_t& sfsa, void* ptr, u32 chunkindex, superbin_t const& bin)
    {
        chunk_t& chunk = m_chunk_array[chunkindex];
        chunk.m_elem_used -= 1;

        bool      chunk_is_now_empty = false;
        bool      chunk_was_full     = false;
        u32 const size               = deallocate_from_chunk(sfsa, chunkindex, ptr, bin, chunk_is_now_empty, chunk_was_full);

        if (chunk_is_now_empty)
        {
            deinitialize_chunk(sfsa, chunkindex, bin);
            if (!m_cache_chunk_list.is_full())
            {
                m_cache_chunk_list.insert(m_chunk_list, chunkindex);
            }
            else
            {
                decommit_chunk(chunkindex, bin);
                m_free_chunk_list.insert(m_chunk_list, chunkindex);
            }
        }
        else if (chunk_was_full)
        {
            m_used_chunk_list_per_size[bin.m_alloc_bin_index - m_bin_base].insert(m_chunk_list, chunkindex);
        }
        return size;
    }

    void superalloc_t::initialize_chunk(superfsa_t& fsa, u32 chunkindex, u32 size, superbin_t const& bin)
    {
        if (bin.m_use_binmap == 1)
        {
            u32 const ibinmap     = fsa.alloc(sizeof(binmap_t));
            binmap_t* binmap      = (binmap_t*)fsa.idx2ptr(ibinmap);
            m_binmaps[chunkindex] = ibinmap;
            if (bin.m_alloc_count > 32)
            {
                binmap->m_l1_offset = fsa.alloc(bin.m_binmap_l1len);
                binmap->m_l2_offset = fsa.alloc(bin.m_binmap_l2len);
                u16* l1             = (u16*)fsa.idx2ptr(binmap->m_l1_offset);
                u16* l2             = (u16*)fsa.idx2ptr(binmap->m_l2_offset);
                binmap->init(bin.m_alloc_count, l1, bin.m_binmap_l1len, l2, bin.m_binmap_l2len);
            }
            else
            {
                binmap->m_l1_offset = 0xffffffff;
                binmap->m_l2_offset = 0xffffffff;
                binmap->init(bin.m_alloc_count, nullptr, 0, nullptr, 0);
            }
        }
        else
        {
            u32 const num_physical_pages = alignto(size, m_page_size) / m_page_size;
            m_binmaps[chunkindex]        = num_physical_pages;
        }
        chunk_t& chunk    = m_chunk_array[chunkindex];
        chunk.m_bin_index = bin.m_alloc_bin_index;
        chunk.m_elem_used = 0;
    }

    void superalloc_t::deinitialize_chunk(superfsa_t& fsa, u32 chunkindex, superbin_t const& bin)
    {
        if (bin.m_use_binmap == 1)
        {
            binmap_t* binmap = (binmap_t*)fsa.idx2ptr(m_binmaps[chunkindex]);
            if (binmap->m_l1_offset != 0xffffffff)
            {
                fsa.dealloc(binmap->m_l1_offset);
                fsa.dealloc(binmap->m_l2_offset);
            }
            fsa.dealloc(m_binmaps[chunkindex]);
            m_binmaps[chunkindex] = 0xffffffff;
        }

        // NOTE: For debug purposes
        chunk_t& chunk    = m_chunk_array[chunkindex];
        chunk.m_bin_index = 0xffff;
        chunk.m_elem_used = 0xffff;
    }

    void superalloc_t::commit_chunk(u32 chunkindex, u32 size, superbin_t const& bin)
    {
        if (bin.m_use_binmap == 1)
        {
            u32 const   num_physical_pages = m_chunk_size / m_page_size;
            void* const chunkaddress       = toaddress(m_memory_base, (m_chunk_size * chunkindex));
            m_vmem->commit(chunkaddress, m_page_size, num_physical_pages);
        }
        else
        {
            u32 const num_physical_pages_committed = m_binmaps[chunkindex];
            u32 const num_physical_pages_required  = alignto(size, m_page_size);
            // if this is a chunk not managed by a binmap than we need to
            // check here if this chunk has enough pages committed.
            if (num_physical_pages_required > num_physical_pages_committed)
            {
                u32 const   num_pages_commit = num_physical_pages_required - num_physical_pages_committed;
                void* const chunkaddress     = toaddress(m_memory_base, (m_chunk_size * chunkindex) + (u64)num_physical_pages_committed * m_page_size);
                m_vmem->commit(chunkaddress, m_page_size, num_pages_commit);
            }
            else
            {
                u32 const   num_pages_decommit = num_physical_pages_required - num_physical_pages_committed;
                void* const chunkaddress       = toaddress(m_memory_base, (m_chunk_size * chunkindex) + (u64)num_physical_pages_required * m_page_size);
                m_vmem->decommit(chunkaddress, m_page_size, num_pages_decommit);
            }
        }
    }

    void superalloc_t::decommit_chunk(u32 chunkindex, superbin_t const& bin)
    {
        if (bin.m_use_binmap == 0)
        {
            u32 const   num_physical_pages = m_binmaps[chunkindex];
            void* const chunkaddress       = toaddress(m_memory_base, (m_chunk_size * chunkindex));
            m_vmem->decommit(chunkaddress, m_page_size, num_physical_pages);
        }
    }

    void* superalloc_t::allocate_from_chunk(superfsa_t& fsa, u32 chunkindex, u32 size, superbin_t const& bin, bool& chunk_is_now_full)
    {
        void* ptr = nullptr;
        if (bin.m_use_binmap == 1)
        {
            binmap_t* binmap = (binmap_t*)fsa.idx2ptr(m_binmaps[chunkindex]);
            u16*      l1     = (u16*)fsa.idx2ptr(binmap->m_l1_offset);
            u16*      l2     = (u16*)fsa.idx2ptr(binmap->m_l2_offset);
            u32 const i      = binmap->findandset(bin.m_alloc_count, l1, l2);
            ptr              = toaddress(m_memory_base, (m_chunk_size * chunkindex) + i * bin.m_alloc_size);
        }
        else
        {
            ptr = toaddress(m_memory_base, (m_chunk_size * chunkindex));
        }

        chunk_t& chunk = m_chunk_array[chunkindex];
        chunk.m_elem_used += 1;
        chunk_is_now_full = chunk.m_elem_used == bin.m_alloc_count;

        return ptr;
    }

    u32 superalloc_t::deallocate_from_chunk(superfsa_t& fsa, u32 chunkindex, void* ptr, superbin_t const& bin, bool& chunk_is_now_empty, bool& chunk_was_full)
    {
        u32 size;
        if (bin.m_use_binmap == 1)
        {
            u32 const i      = (u32)(todistance(m_memory_base, ptr) / bin.m_alloc_size);
            binmap_t* binmap = (binmap_t*)fsa.idx2ptr(m_binmaps[chunkindex]);
            u16*      l1     = (u16*)fsa.idx2ptr(binmap->m_l1_offset);
            u16*      l2     = (u16*)fsa.idx2ptr(binmap->m_l2_offset);
            binmap->clr(bin.m_alloc_count, l1, l2, i);
            size = bin.m_alloc_size;
        }
        else
        {
            u32 const num_physical_pages = m_binmaps[chunkindex];
            size                         = num_physical_pages * m_page_size;
        }

        chunk_t& chunk = m_chunk_array[chunkindex];
        chunk_was_full = chunk.m_elem_used == bin.m_alloc_count;
        chunk.m_elem_used -= 1;
        chunk_is_now_empty = chunk.m_elem_used == 0;

        return size;
    }

    void superallocator_t::initialize(xvmem* vmem)
    {
        m_vmem = vmem;
        m_internal_heap.initialize(m_vmem, c_internal_heap_address_range, c_internal_heap_pre_size);
        m_internal_fsa.initialize(m_internal_heap, m_vmem, c_internal_fsa_address_range, c_internal_fsa_pre_page_count);

        u32 const attrs = 0;
        m_vmem->reserve(c_address_range, m_page_size, attrs, m_address_base);

        void* address_base = m_address_base;
        for (s32 i = 0; i < c_num_allocators; ++i)
        {
            m_allocators[i].initialize(address_base, m_page_size, m_vmem, m_internal_heap, m_internal_fsa);
            address_base = toaddress(address_base, alignto(m_allocators[i].m_memory_range, c_address_divisor));
        }
    }

    void superallocator_t::deinitialize()
    {
        m_internal_fsa.deinitialize(m_internal_heap);
        m_internal_heap.deinitialize();

        m_vmem->release(m_address_base, c_address_range);
        m_vmem = nullptr;
    }

    void* superallocator_t::allocate(u32 size, u32 alignment)
    {
        size                 = alignto(size, alignment);
        u32 const binindex   = m_asbins[superbin_t::size2bin(size)].m_alloc_bin_index;
        s32 const allocindex = m_asbins[binindex].m_alloc_index;
        void*     ptr        = m_allocators[allocindex].allocate(m_internal_fsa, size, m_asbins[binindex]);
        return ptr;
    }

    u32 superallocator_t::deallocate(void* ptr)
    {
        u32 const mapindex   = (u32)(todistance(m_address_base, ptr) / c_address_divisor);
        u32 const allocindex = m_allocator_map[mapindex];
        u32 const chunkindex = m_allocators[allocindex].get_chunkindex(ptr);
        u32 const binindex   = m_allocators[allocindex].get_binindex(chunkindex);
        u32 const size       = m_allocators[allocindex].deallocate(m_internal_fsa, ptr, chunkindex, m_asbins[binindex]);
        return size;
    }

} // namespace xcore
