#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_doubly_linked_list.h"
#include "xvmem/private/x_singly_linked_list.h"
#include "xvmem/private/x_binmap.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    static inline void* toaddress(void* base, u64 offset) { return (void*)((u64)base + offset); }
    static inline u64   todistance(void* base, void* ptr)
    {
        ASSERT(ptr > base);
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
        size         = xalignUp(size, m_size_alignment);
        u64  ptr_max = ((u64)m_page_count_current * m_page_size);
        u32* m_chunk_array;
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

    class superarray_t
    {
    public:
        void initialize(xvmem* vmem, u64 address_range, u32 item_size)
        {
            m_vmem          = vmem;
            m_address_range = address_range;
            u32 attrs       = 0;
            m_vmem->reserve(m_address_range, m_page_size, attrs, m_address_base);
            m_address_alloc = (xbyte*)m_address_base;
            m_address_end   = m_address_alloc + m_page_size;
            m_vmem->commit(m_address_alloc, m_page_size, 1);
            m_item_size = item_size;
            m_free_list = nullptr;
        }

        void deinitialize()
        {
            m_vmem->release(m_address_base, m_address_range);
            m_address_base  = nullptr;
            m_address_range = 0;
            m_item_size     = 0;
            m_free_list     = nullptr;
            m_address_alloc = nullptr;
            m_address_end   = nullptr;
        }

        void* alloc()
        {
            if (m_free_list != nullptr)
            {
                void* ptr   = m_free_list;
                m_free_list = m_free_list->m_next;
                return ptr;
            }
            else
            {
                if ((m_address_alloc + m_item_size) > m_address_end)
                {
                    m_vmem->commit(m_address_end, m_page_size, 1);
                    m_address_end += m_page_size;
                }
                void* ptr = m_address_alloc;
                m_address_alloc += m_item_size;
                return ptr;
            }
        }

        void dealloc(void* ptr)
        {
            item_t* item = (item_t*)ptr;
            item->m_next = m_free_list;
            m_free_list  = item;
        }

        inline void* idx2ptr(u32 i) const { return toaddress(m_address_base, i * m_item_size); }
        inline u32   ptr2idx(void* ptr) const { return (u32)(todistance(m_address_base, ptr)) / m_item_size; }

        template <typename T> inline T* operator[](s32 index) { return (T*)toaddress(m_address_base, i * m_item_size); }

        struct item_t
        {
            item_t* m_next;
        };
        xvmem*  m_vmem;
        xbyte*  m_address_base;
        u64     m_address_range;
        u32     m_page_size;
        u32     m_item_size;
        item_t* m_free_list;
        xbyte*  m_address_alloc;
        xbyte*  m_address_end;
    };

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

        struct page_t
        {
            u16 m_item_size;
            u16 m_item_index;
            u16 m_item_count;
            u16 m_item_max;
            u16 m_dummy;
            u16 m_item_freelist;

            void initialize(u32 size, u32 pagesize)
            {
                m_item_size     = size;
                m_item_index    = 0;
                m_item_count    = 0;
                m_item_max      = pagesize / size;
                m_dummy         = 0x10DA;
                m_item_freelist = 0xffff;
            }

            inline bool is_full() const { return m_item_count == m_item_max; }
            inline bool is_empty() const { return m_item_count == 0; }
            inline u32  ptr2idx(void* const ptr, void* const elem) const { return (u32)(((u64)elem - (u64)ptr) / m_item_size); }
            inline u32* idx2ptr(void* const ptr, u32 const index) const { return (u32*)((xbyte*)ptr + (index * (u32)m_item_size)); }

            void* allocate(void* page_address)
            {
                m_item_count++;
                if (m_item_freelist != 0xffff)
                {
                    u16 const  ielem = m_item_freelist;
                    u16* const pelem = (u16*)idx2ptr(page_address, ielem);
                    m_item_freelist  = pelem[0];
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
                u16* const pelem = (u16*)idx2ptr(page_address, item_index);
                pelem[0]         = m_item_freelist;
                m_item_freelist  = item_index;
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
        llnode_t*        m_page_list;
        llist_t          m_free_page_list;
        llist_t          m_cached_page_list;
        static const s32 c_max_num_sizes = 16;
        llhead_t         m_used_page_list_per_size[c_max_num_sizes];
    };

    void superfsa_t::initialize(superheap_t& heap, xvmem* vmem, u64 address_range, u32 size_to_pre_allocate)
    {
        m_vmem         = vmem;
        u32 attributes = 0;
        m_vmem->reserve(address_range, m_page_size, attributes, m_address);
        m_address_range              = address_range;
        m_page_count                 = (u32)(address_range / (u64)m_page_size);
        m_page_array                 = (page_t*)heap.allocate(m_page_count * sizeof(page_t));
        m_page_list                  = (llnode_t*)heap.allocate(m_page_count * sizeof(llnode_t));
        u32 const num_pages_to_cache = xalignUp(size_to_pre_allocate, m_page_size) / m_page_size;
        ASSERT(num_pages_to_cache <= m_page_count);
        m_free_page_list.initialize(m_page_list, num_pages_to_cache, m_page_count - num_pages_to_cache, m_page_count);
        for (u32 i = 0; i < m_page_count; i++)
            m_page_array[i].initialize(8, 0);
        for (u32 i = 0; i < c_max_num_sizes; i++)
            m_used_page_list_per_size[i].reset();

        if (num_pages_to_cache > 0)
        {
            m_cached_page_list.initialize(m_page_list, 0, num_pages_to_cache, num_pages_to_cache);
            m_vmem->commit(m_address, m_page_size, num_pages_to_cache);
        }
    }

    void superfsa_t::deinitialize(superheap_t& heap)
    {
        // NOTE: Do we need to decommit physical pages, or is 'release' enough?
        m_vmem->release(m_address, m_address_range);
    }

    u32 superfsa_t::alloc(u32 size)
    {
        size               = xalignUp(size, (u32)8);
        size               = xceilpo2(size);
        s32 const c        = (xcountTrailingZeros(size) - 3);
        void*     paddress = nullptr;
        page_t*   ppage    = nullptr;
        u32       ipage    = 0xffffffff;
        ASSERT(c >= 0 && c < c_max_num_sizes);
        if (m_used_page_list_per_size[c].is_nil())
        {
            // Get a page and initialize that page for this size
            if (!m_cached_page_list.is_empty())
            {
                ipage = m_cached_page_list.remove_headi(m_page_list).get();
                ppage = &m_page_array[ipage];
                ppage->initialize(size, m_page_size);
            }
            else if (!m_free_page_list.is_empty())
            {
                ipage = m_free_page_list.remove_headi(m_page_list).get();
                ppage = &m_page_array[ipage];
                ppage->initialize(size, m_page_size);
                m_vmem->commit(ppage, m_page_size, 1);
            }
            m_used_page_list_per_size[c].insert(m_page_list, ipage);
        }
        else
        {
            ipage = m_used_page_list_per_size[c].get();
            ppage = &m_page_array[ipage];
        }

        if (ppage != nullptr)
        {
            paddress  = toaddress(m_address, (u64)ipage * m_page_size);
            void* ptr = ppage->allocate(paddress);
            if (ppage->is_full())
            {
                m_used_page_list_per_size[c].remove_head(m_page_list);
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
        if (ppage->is_empty())
        {
            s32 const c = (xcountTrailingZeros(ppage->m_item_size) - 3);
            ASSERT(c >= 0 && c < c_max_num_sizes);
            m_used_page_list_per_size[c].remove_item(m_page_list, pageindex);
            if (!m_cached_page_list.is_full())
            {
                m_cached_page_list.insert(m_page_list, pageindex);
            }
            else
            {
                m_vmem->decommit(paddr, m_page_size, 1);
                m_free_page_list.insert(m_page_list, pageindex);
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

        superalloc_t(const superalloc_t& s)
            : m_memory_base(nullptr)
            , m_memory_range(s.m_memory_range)
            , m_chunk_size(s.m_chunk_size)
            , m_chunk_cnt((u32)(s.m_memory_range / s.m_chunk_size))
            , m_chunk_array(nullptr)
            , m_binmaps(nullptr)
            , m_chunk_list(nullptr)
            , m_max_chunks_to_cache(s.m_max_chunks_to_cache)
            , m_num_sizes(s.m_num_sizes)
            , m_bin_base(s.m_bin_base)
            , m_used_chunk_list_per_size(nullptr)
        {
        }

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
            : m_alloc_size((xMB * allocsize_mb) + (xKB * allocsize_kb) + (allocsize_b))
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
            s32 const t = xcountTrailingZeros(xalignUp(f, (u32)8) >> 2);
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
            , m_address_range(0)
            , m_address_divisor(1)
            , m_address_map_size(0)
            , m_allocator_map(nullptr)
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
            , m_address_divisor(other.m_address_divisor)
            , m_address_map_size(other.m_address_map_size)
            , m_allocator_map(other.m_allocator_map)
            , m_internal_heap_address_range(other.m_internal_heap_address_range)
            , m_internal_heap_pre_size(other.m_internal_heap_pre_size)
            , m_internal_fsa_address_range(other.m_internal_fsa_address_range)
            , m_internal_fsa_pre_size(other.m_internal_fsa_pre_size)
        {
        }

        superallocator_config_t(s32 const num_bins, superbin_t const* asbins, const s32 num_allocators, superalloc_t const* allocators, u64 const address_range, u64 const address_divisor, s32 const address_map_size, u8 const* allocator_map,
                                u32 const internal_heap_address_range, u32 const internal_heap_pre_size, u32 const internal_fsa_address_range, u32 const internal_fsa_pre_size)
            : m_num_bins(num_bins)
            , m_asbins(asbins)
            , m_num_allocators(num_allocators)
            , m_allocators(allocators)
            , m_address_range(address_range)
            , m_address_divisor(address_divisor)
            , m_address_map_size(address_map_size)
            , m_allocator_map(allocator_map)
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
        u64                 m_address_divisor;
        s32                 m_address_map_size;
        u8 const*           m_allocator_map;
        u32                 m_internal_heap_address_range;
        u32                 m_internal_heap_pre_size;
        u32                 m_internal_fsa_address_range;
        u32                 m_internal_fsa_pre_size;
    };

    namespace superallocator_config_desktop_app_t
    {
        // superbin_t(allocation size MB, KB, B, bin index, allocator index, use binmap?, maximum allocation count, binmap level 1 length, binmap level 2 length)
        static const s32        c_num_bins           = 96;
        static const superbin_t c_asbins[c_num_bins] = {
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

        static const s32    c_num_allocators               = 12;
        static superalloc_t c_allocators[c_num_allocators] = {
            superalloc_t(xGB * 1, xKB * 64, 256, 16, 0),   // memory range, chunk size, number of chunks to cache, number of sizes, bin base
            superalloc_t(xGB * 4, xKB * 512, 256, 40, 16), //
            superalloc_t(xGB * 4, xKB * 512, 2, 4, 56),    //
            superalloc_t(xGB * 2, xMB * 1, 0, 4, 60),      //
            superalloc_t(xGB * 2, xMB * 2, 0, 4, 64),      //
            superalloc_t(xGB * 2, xMB * 4, 0, 4, 68),      //
            superalloc_t(xGB * 2, xMB * 8, 0, 4, 72),      //
            superalloc_t(xGB * 2, xMB * 16, 0, 4, 76),     //
            superalloc_t(xGB * 2, xMB * 32, 0, 4, 80),     //
            superalloc_t(xGB * 2, xMB * 64, 0, 4, 84),     //
            superalloc_t(xGB * 4, xMB * 128, 0, 4, 88),    //
            superalloc_t(xGB * 8, xMB * 256, 0, 4, 92)     //
        };

        static const u64 c_address_range                     = (u64)35 * xGB;
        static const u64 c_address_divisor                   = (u64)1 * xGB;
        static const s32 c_address_map_size                  = 35;
        static const u8  c_allocator_map[c_address_map_size] = {0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11};

        static const u32 c_internal_heap_address_range = 32 * xMB;
        static const u32 c_internal_heap_pre_size      = 2 * xMB;
        static const u32 c_internal_fsa_address_range  = 32 * xMB;
        static const u32 c_internal_fsa_pre_size       = 4 * xMB;

        static superallocator_config_t get_config()
        {
            return superallocator_config_t(c_num_bins, c_asbins, c_num_allocators, c_allocators, c_address_range, c_address_divisor, c_address_map_size, c_allocator_map, c_internal_heap_address_range, c_internal_heap_pre_size, c_internal_fsa_address_range,
                                           c_internal_fsa_pre_size);
        }

    }; // namespace superallocator_config_desktop_app_t

    class superallocator_t
    {
    public:
        superallocator_t()
            : m_config()
            , m_allocators(nullptr)
            , m_address_base(nullptr)
            , m_vmem(nullptr)
            , m_page_size(0)
            , m_internal_heap()
            , m_internal_fsa()
        {
        }

        void  initialize(xvmem* vmem, superallocator_config_t const& config);
        void  deinitialize();
        void* allocate(u32 size, u32 alignment);
        u32   deallocate(void* ptr);

        superallocator_config_t m_config;
        superalloc_t*           m_allocators;
        void*                   m_address_base;
        xvmem*                  m_vmem;
        u32                     m_page_size;
        superheap_t             m_internal_heap;
        superfsa_t              m_internal_fsa;
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

        m_free_chunk_list.initialize(m_chunk_list, m_max_chunks_to_cache, m_chunk_cnt - m_max_chunks_to_cache, m_chunk_cnt);
        if (m_max_chunks_to_cache > 0)
        {
            m_cache_chunk_list.initialize(m_chunk_list, 0, m_max_chunks_to_cache, m_max_chunks_to_cache);
            u32 const num_physical_pages = (u32)(((u64)m_chunk_size * m_max_chunks_to_cache) / m_page_size);
            m_vmem->commit(m_memory_base, m_page_size, num_physical_pages);
        }
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
            m_used_chunk_list_per_size[bin.m_alloc_bin_index - m_bin_base].insert(m_chunk_list, chunkindex);
        }

        bool        chunk_is_now_full = false;
        void* const ptr               = allocate_from_chunk(sfsa, chunkindex.get(), size, bin, chunk_is_now_full);
        if (chunk_is_now_full) // Chunk is full, no more allocations possible
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
        bool      chunk_is_now_empty = false;
        bool      chunk_was_full     = false;
        u32 const size               = deallocate_from_chunk(sfsa, chunkindex, ptr, bin, chunk_is_now_empty, chunk_was_full);
        if (chunk_is_now_empty)
        {
            if (!chunk_was_full)
            {
                m_used_chunk_list_per_size[bin.m_alloc_bin_index - m_bin_base].remove_item(m_chunk_list, chunkindex);
            }
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
                binmap->m_l1_offset = fsa.alloc(sizeof(u16) * bin.m_binmap_l1len);
                binmap->m_l2_offset = fsa.alloc(sizeof(u16) * bin.m_binmap_l2len);
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
            u32 const num_physical_pages = xalignUp(size, m_page_size) / m_page_size;
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
            u32 const   num_physical_pages = (u32)(m_chunk_size / m_page_size);
            void* const chunkaddress       = toaddress(m_memory_base, (m_chunk_size * chunkindex));
            m_vmem->commit(chunkaddress, m_page_size, num_physical_pages);
        }
        else
        {
            u32 const num_physical_pages_committed = m_binmaps[chunkindex];
            u32 const num_physical_pages_required  = xalignUp(size, m_page_size) / m_page_size;
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
            ptr              = toaddress(m_memory_base, (u64)(m_chunk_size * chunkindex) + i * bin.m_alloc_size);
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
            void* const chunkaddress = toaddress(m_memory_base, (m_chunk_size * chunkindex));
            u32 const   i            = (u32)(todistance(chunkaddress, ptr) / bin.m_alloc_size);
            ASSERT(i < bin.m_alloc_count);
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

    void superallocator_t::initialize(xvmem* vmem, superallocator_config_t const& config)
    {
        m_config = config;
        m_vmem   = vmem;
        m_internal_heap.initialize(m_vmem, m_config.m_internal_heap_address_range, m_config.m_internal_heap_pre_size);
        m_internal_fsa.initialize(m_internal_heap, m_vmem, m_config.m_internal_fsa_address_range, m_config.m_internal_fsa_pre_size);

        u32 const attrs = 0;
        m_vmem->reserve(m_config.m_address_range, m_page_size, attrs, m_address_base);

        m_allocators = (superalloc_t*)m_internal_heap.allocate(sizeof(superalloc_t) * config.m_num_allocators);
        for (s32 i = 0; i < m_config.m_num_allocators; ++i)
        {
            m_allocators[i] = superalloc_t(config.m_allocators[i]);
        }

        void* address_base = m_address_base;
        for (s32 i = 0; i < m_config.m_num_allocators; ++i)
        {
            m_allocators[i].initialize(address_base, m_page_size, m_vmem, m_internal_heap, m_internal_fsa);
            address_base = toaddress(address_base, xalignUp(m_allocators[i].m_memory_range, m_config.m_address_divisor));
        }
    }

    void superallocator_t::deinitialize()
    {
        m_internal_fsa.deinitialize(m_internal_heap);
        m_internal_heap.deinitialize();

        m_vmem->release(m_address_base, m_config.m_address_range);
        m_vmem = nullptr;
    }

    void* superallocator_t::allocate(u32 size, u32 alignment)
    {
        size                 = xalignUp(size, alignment);
        u32 const binindex   = m_config.m_asbins[superbin_t::size2bin(size)].m_alloc_bin_index;
        s32 const allocindex = m_config.m_asbins[binindex].m_alloc_index;
        void*     ptr        = m_allocators[allocindex].allocate(m_internal_fsa, size, m_config.m_asbins[binindex]);
        return ptr;
    }

    u32 superallocator_t::deallocate(void* ptr)
    {
        u32 const mapindex   = (u32)(todistance(m_address_base, ptr) / m_config.m_address_divisor);
        u32 const allocindex = m_config.m_allocator_map[mapindex];
        u32 const chunkindex = m_allocators[allocindex].get_chunkindex(ptr);
        u32 const binindex   = m_allocators[allocindex].get_binindex(chunkindex);
        u32 const size       = m_allocators[allocindex].deallocate(m_internal_fsa, ptr, chunkindex, m_config.m_asbins[binindex]);
        return size;
    }

    // Managing requests of different chunk-sizes but managed through first a division into blocks, where blocks are
    // divided into segments. Segments contain chunks.
    //
    // Chunk-Sizes are all power-of-2 sizes
    //
    // Functionality:
    //   Allocate
    //    - Handling the request of a new chunk, either creating one or taking one from the cache
    //   Deallocate
    //    - Quickly finding the segment_t*, block_t*, chunk_t* and superalloc_t* that belong to a 'void* ptr'
    //    - Collecting a now empty-chunk and either release or cache it
    //
    //   Get chunk by index
    //   Get address of chunk
    //
    struct chunk_t : llnode_t
    {
        u16 m_elem_used;
        u16 m_bin_index;
        u32 m_bin_map;
        u16 m_segment_index;
    };

    struct region_t
    {
        llindex_t allocate_chunk(u32 chunk_size)
        {
            u32 const size  = xceilpo2(chunk_size);
            s32 const index = (xcountTrailingZeros(size));

            if (m_block_per_group_list_active[index] == llindex_t::NIL)
            {
                // We need to get a cached block, check:
                // - m_block_per_group_list_cached
                // - m_blocks_list_free (consume a new block and add segments to m_segment_per_config_list_free[])
            }
            else
            {
                // See if segment has cached chunks
                // If no cached chunks, find '0' bit in 'm_chunks_binmap'
                //    Create chunk_t object from 'm_chunks_array'
                //    Add to segment, commit physical memory
            }
        }

        void deallocate_chunk(chunk_t* chunk)
        {
            u32 block_index;
            u32 segment_index;
            u32 chunk_index;
            get_from_page_index(chunk->m_page_index, block_index, block_segment_index, segment_index, chunk_index);

            blocks_t*  block   = &m_blocks_array[block_index];
            segment_t* segment = m_segments_array[segment_index];

            segment->m_chunks_used -= 1;
            segment->m_chunks_list_cached.insert(m_chunks_array.base(), chunk_index);
            if (segment->m_chunks_used == 0)
            {
                block->m_segments_list_cached.insert(m_segments_array.base(), segment_index);
                block->m_segments_used -= 1;
                if (block->m_segments_used == 0)
                {
                    // Remove it from the active list of blocks
                    // Insert it in the cached list of blocks
                    m_block_per_group_list_active.remove(m_blocks_array.base(), block_index);
                    m_block_per_group_list_cached.insert(m_blocks_array.base(), block_index);
                }
            }

            // Figure out the segment we belong to
            // If the segment now has no more used chunks we can 'free' the segment
            //    back to the block.
            // If the block now has no more used segments we can 'free' the block
            //    back to the region.
        }

        //@NOTE: Should we cache the chunks at the segment level or global level?
        //@NOTE: Should we cache segments ?

        struct segment_t : llnode_t
        {
            u8       m_config;             //
            u8       m_chunks_shift;       // e.g. 16 (1<<16 = 64 KB, 4 MB / 64 KB = 64 chunks)
            s16      m_chunks_used;        // Count of how many chunks are active
            u16*     m_chunks_array;       //
            llhead_t m_chunks_list_cached; // Doubly linked list of 'chunk_t' items in 'm_chunks_array'
            lhead_t  m_chunks_list_free;   // Singly linked list of 'u16' items in 'm_chunks_array'
            u16      m_block_index;        // Our parent
        };

        struct block_t : llnode_t
        {
            llhead_t m_segments_list_cached;
            lhead_t  m_segments_list_free;
            u16*     m_segments_array;
            u16      m_segments_shift; // e.g. 22 (1<<22 = 4 MB)
            u16      m_segments_used;
        };

        chunk_t* get_chunk(llindex_t i)
        {
            if (i.m_index == llindex_t::NIL)
                return nullptr;
            return m_chunk_array[chunk];
        }

        void* get_chunk_base_address(u32 const page_index) const { return toaddress(m_address_base, page_index * m_page_size); }

        void get_from_page_index(u32 const page_index, u32& block_index, u32& segment_index_in_block, u32& segment_index, u32& chunk_index)
        {
            u32 const page_to_block_shift        = (m_block_shift - m_page_shift);
            block_index                          = page_index >> page_to_block_shift;
            block_t*  block                      = &m_blocks_array[block_index];
            u32 const block_segment_index        = page_index & ((1 << page_to_block_shift) - 1);
            u32 const page_to_segment_shift      = (block->m_segment_shift - m_page_shift);
            segment_index_in_block               = block_segment_index >> page_to_segment_shift;
            u32 const        segment_index       = block->m_segment_array[segment_index_in_block];
            segment_t const* segment             = m_segments_array[segment_index];
            u32 const        segment_chunk_index = block_segment_index & ((1 << page_to_segment_shift) - 1);
            chunk_index                          = segment->m_chunk_array[segment_chunk_index];
        }

        chunk_t* get_chunk(u32 const page_index)
        {
            u32 const  page_to_block_shift   = (m_block_shift - m_page_shift);
            u32 const  block_index           = page_index >> page_to_block_shift;
            block_t*   block                 = &m_blocks_array[block_index];
            u32 const  block_segment_index   = page_index & ((1 << page_to_block_shift) - 1);
            u32 const  page_to_segment_shift = (block->m_segment_shift - m_page_shift);
            u32 const  segment_index         = block_segment_index >> page_to_segment_shift;
            segment_t* segment               = m_segments_array[segment_index];
            u32 const  segment_chunk_index   = block_segment_index & ((1 << page_to_segment_shift) - 1);
            llindex_t  chunk_index           = segment->m_chunk_array[segment_chunk_index];
            return get_chunk(chunk_index);
        }

        bool is_segment_empty(segment_t* seg) const { return seg->m_segment_used == 0; }
        bool is_segment_full(segment_t* seg) const { return seg->m_segment_used == m_segment_configs[seg->m_segment_config]; }

        segment_t* get_segment_from_page_index(u32 const page_index)
        {
            u32 const block_to_page_shift = (m_block_shift - m_page_shift);
            u32 const block_index         = page_index >> block_to_page_shift;
            block_t*  block               = &m_blocks_array[block_index];
            u32 const block_page_index    = page_index & ((1 << block_to_page_shift) - 1);
            u32 const segment_index       = block_page_index >> block->m_segment_shift;
            return m_segments_array[segment_index];
        }

        block_t* get_block_from_page_index(u32 const page_index)
        {
            u32 const block_index = page_index >> (m_block_shift - m_page_shift);
            return &m_blocks_array[block_index];
        }

        struct config_t
        {
            config_t(u8 segment_index = 0, u8 segment_shift = 0, u16 segment_max_chunks = 0, u16 chunk_shift = 0)
                : m_segment_index(segment_index)
                , m_segment_shift(segment_shift)
                , m_segment_max_chunks(segment_max_chunks)
                , m_chunk_shift(chunk_shift)
            {
            }
            u8  m_segment_index;
            u8  m_segment_shift; // Size of segment (1 << shift)
            u16 m_segment_max_chunks;
            u8  m_chunk_shift;
        };

        superarray_t m_chunks_array;
        superarray_t m_segments_array;
        llhead_t     m_segment_per_chunk_size_active[32];
        llhead_t     m_segment_per_chunk_size_cached[32];
        llhead_t     m_block_per_group_list_active[4];
        llhead_t     m_block_per_group_list_cached[4];
        void*        m_address_base;
        u64          m_address_range;
        u32          m_page_size;
        u32          m_page_shift;   // e.g. 16 (1<<16 = 64 KB)
        s16          m_blocks_shift; // e.g. 25 (1<<30 =  1 GB)
        block_t*     m_blocks_array;
        llist_t      m_blocks_list_free;

        static const s32      c_num_configs            = 32;
        static const config_t c_configs[c_num_configs] = {config_t(),
                                                          config_t(),
                                                          config_t(),
                                                          config_t(),
                                                          config_t(),
                                                          config_t(),
                                                          config_t(),
                                                          config_t(),
                                                          config_t(),
                                                          config_t(),
                                                          config_t(),
                                                          config_t(),
                                                          config_t(),
                                                          config_t(),
                                                          config_t(),
                                                          config_t(),
                                                          config_t(0, 22, 1024, 16), // 4MB, 64KB
                                                          config_t(),                // NA
                                                          config_t(),                // NA
                                                          config_t(1, 25, 64, 19),   // 32MB, 512KB
                                                          config_t(1, 25, 32, 20),   // 32MB, 1MB
                                                          config_t(1, 25, 16, 21),   // 32MB, 2MB
                                                          config_t(1, 25, 8, 22),    // 32MB, 4MB
                                                          config_t(2, 28, 64, 23),   // 512MB, 8 MB
                                                          config_t(2, 28, 32, 24),   // 512MB, 16 MB
                                                          config_t(2, 28, 16, 25),   // 512MB, 32 MB
                                                          config_t(2, 28, 8, 26),    // 512MB, 64 MB
                                                          config_t(3, 29, 8, 26),    // 1GB, 128 MB
                                                          config_t(3, 29, 4, 26),    // 1GB, 256 MB
                                                          config_t(3, 29, 2, 26),    // 1GB, 512 MB
                                                          config_t(),
                                                          config_t(),
                                                          config_t()};
    };

    // Example Config:
    //
    //     Region: 128 GB
    //     Block :   1 GB
    //
    //  C :  Chunk Size - Segment Index -  Segment Size  - Binmaps Or Page-Count
    //  0 :       4 KB  -          NA
    //  1 :       8 KB  -          NA
    //  2 :      16 KB  -          NA
    //  3 :      32 KB  -          NA
    //  4 :      64 KB  -           0   -        4 MB    -      Binmaps
    //  5 :     128 KB  -          NA
    //  6 :     256 KB  -          NA
    //  7 :     512 KB  -           1   -       32 MB    -      Page-Count
    //  8 :       1 MB  -           1   -       32 MB    -      Page-Count
    //  9 :       2 MB  -           1   -       32 MB    -      Page-Count
    // 10 :       4 MB  -           1   -       32 MB    -      Page-Count
    // 11 :       8 MB  -           2   -      512 MB    -      Page-Count
    // 12 :      16 MB  -           2   -      512 MB    -      Page-Count
    // 13 :      32 MB  -           2   -      512 MB    -      Page-Count
    // 14 :      64 MB  -           2   -      512 MB    -      Page-Count
    // 15 :     128 MB  -           3   -        1 GB    -      Page-Count
    // 16 :     256 MB  -           3   -        1 GB    -      Page-Count
    // 17 :     512 MB  -           3   -        1 GB    -      Page-Count

} // namespace xcore
