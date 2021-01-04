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

    struct superpage_t
    {
        u16 m_item_size;
        u16 m_item_index;
        u16 m_item_count;
        u16 m_item_max;
        u16 m_item_freelist;
        u16 m_next;

        void initialize(u32 size, u32 pagesize)
        {
            m_item_size     = size;
            m_item_index    = 0;
            m_item_count    = 0;
            m_item_max      = pagesize / size;
            m_item_freelist = 0xffff;
            m_next          = 0x10DA;
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

    struct superpages_t
    {
        void  initialize(superheap_t& heap, xvmem* vmem, u64 address_range, u32 size_to_pre_allocate);
        void  deinitialize(superheap_t& heap);
        u16   checkout_page(u32 const alloc_size);
        void  release_page(u16 index);
        void* address_of_page(u16 ipage) const { return toaddress(m_address, (u64)ipage * m_page_size); }

        xvmem*       m_vmem;
        void*        m_address;
        u64          m_address_range;
        u32          m_page_count;
        u32          m_page_size;
        superpage_t* m_page_array;
        llnode_t*    m_page_list;
        llist_t      m_free_page_list;
        llist_t      m_cached_page_list;
    };

    void superpages_t::initialize(superheap_t& heap, xvmem* vmem, u64 address_range, u32 size_to_pre_allocate)
    {
        m_vmem         = vmem;
        u32 attributes = 0;
        m_vmem->reserve(address_range, m_page_size, attributes, m_address);
        m_address_range              = address_range;
        m_page_count                 = (u32)(address_range / (u64)m_page_size);
        m_page_array                 = (superpage_t*)heap.allocate(m_page_count * sizeof(superpage_t));
        m_page_list                  = (llnode_t*)heap.allocate(m_page_count * sizeof(llnode_t));
        u32 const num_pages_to_cache = xalignUp(size_to_pre_allocate, m_page_size) / m_page_size;
        ASSERT(num_pages_to_cache <= m_page_count);
        m_free_page_list.initialize(sizeof(llnode_t), m_page_list, num_pages_to_cache, m_page_count - num_pages_to_cache, m_page_count);
        for (u32 i = 0; i < m_page_count; i++)
            m_page_array[i].initialize(8, 0);

        if (num_pages_to_cache > 0)
        {
            m_cached_page_list.initialize(sizeof(llnode_t), m_page_list, 0, num_pages_to_cache, num_pages_to_cache);
            m_vmem->commit(m_address, m_page_size, num_pages_to_cache);
        }
    }

    void superpages_t::deinitialize(superheap_t& heap)
    {
        // NOTE: Do we need to decommit physical pages, or is 'release' enough?
        m_vmem->release(m_address, m_address_range);
    }

    u16 superpages_t::checkout_page(u32 const alloc_size)
    {
        u16          ipage = NIL;
        superpage_t* ppage = nullptr;

        // Get a page and initialize that page for this size
        if (!m_cached_page_list.is_empty())
        {
            ipage = m_cached_page_list.remove_headi(sizeof(llnode_t), m_page_list);
            ppage = &m_page_array[ipage];
            ppage->initialize(alloc_size, m_page_size);
        }
        else if (!m_free_page_list.is_empty())
        {
            ipage = m_free_page_list.remove_headi(sizeof(llnode_t), m_page_list);
            ppage = &m_page_array[ipage];
            ppage->initialize(alloc_size, m_page_size);
            m_vmem->commit(ppage, m_page_size, 1);
        }
        return ipage;
    }

    void superpages_t::release_page(u16 pageindex)
    {
        superpage_t* const ppage = &m_page_array[pageindex];
        void* const        paddr = address_of_page(pageindex);
        if (ppage->is_empty())
        {
            if (!m_cached_page_list.is_full())
            {
                m_cached_page_list.insert(sizeof(llnode_t), m_page_list, pageindex);
            }
            else
            {
                m_vmem->decommit(paddr, m_page_size, 1);
                m_free_page_list.insert(sizeof(llnode_t), m_page_list, pageindex);
            }
        }
    }

    // Book-keeping for chunks requires to allocate/deallocate blocks of data
    // Power-of-2 sizes, minimum size = 8, maximum_size = 32768
    // @note: returned index to the user is u32[u16(page-index):u16(item-index)]
    // Can re-use superarray_t instead of having custom code, or superarray_t
    // can re-use page_t.
    class superfsa_t
    {
    public:
        void initialize(superheap_t& heap, xvmem* vmem, u64 address_range, u32 num_pages_to_cache);
        void deinitialize(superheap_t& heap);

        u32  alloc(u32 size);
        void dealloc(u32 index);

        inline void* idx2ptr(u32 i) const
        {
            u16 const                pageindex = i >> 16;
            u16 const                itemindex = i & 0xFFFF;
            superpage_t const* const ppage     = &m_pages.m_page_array[pageindex];
            void* const              paddr     = m_pages.address_of_page(pageindex);
            return ppage->idx2ptr(paddr, itemindex);
        }

        inline u32 ptr2idx(void* ptr) const
        {
            u32 const                pageindex = (u32)(todistance(m_pages.m_address, ptr) / m_pages.m_page_size);
            superpage_t const* const ppage     = &m_pages.m_page_array[pageindex];
            void* const              paddr     = m_pages.address_of_page(pageindex);
            u32 const                itemindex = ppage->ptr2idx(paddr, ptr);
            return (pageindex << 16) | (itemindex & 0xFFFF);
        }

        superpages_t     m_pages;
        static const s32 c_max_num_sizes = 16;
        llhead_t         m_used_page_list_per_size[c_max_num_sizes];
    };

    void superfsa_t::initialize(superheap_t& heap, xvmem* vmem, u64 address_range, u32 size_to_pre_allocate)
    {
        m_pages.initialize(heap, vmem, address_range, size_to_pre_allocate);
        for (u32 i = 0; i < c_max_num_sizes; i++)
            m_used_page_list_per_size[i].reset();
    }

    void superfsa_t::deinitialize(superheap_t& heap) { m_pages.deinitialize(heap); }

    u32 superfsa_t::alloc(u32 size)
    {
        size                  = xalignUp(size, (u32)8);
        size                  = xceilpo2(size);
        s32 const    c        = (xcountTrailingZeros(size) - 3);
        void*        paddress = nullptr;
        superpage_t* ppage    = nullptr;
        u32          ipage    = 0xffffffff;
        ASSERT(c >= 0 && c < c_max_num_sizes);
        if (m_used_page_list_per_size[c].is_nil())
        {
            // Get a page and initialize that page for this size
            u16 const ipage = m_pages.checkout_page(size);
            m_used_page_list_per_size[c].insert(sizeof(llnode_t), m_pages.m_page_list, ipage);
        }
        else
        {
            ipage = m_used_page_list_per_size[c].m_index;
            ppage = &m_pages.m_page_array[ipage];
        }

        if (ppage != nullptr)
        {
            paddress  = toaddress(m_pages.m_address, (u64)ipage * m_pages.m_page_size);
            void* ptr = ppage->allocate(paddress);
            if (ppage->is_full())
            {
                m_used_page_list_per_size[c].remove_head(sizeof(llnode_t), m_pages.m_page_list);
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
        u16 const          pageindex = i >> 16;
        u16 const          itemindex = i & 0xFFFF;
        superpage_t* const ppage     = &m_pages.m_page_array[pageindex];
        void* const        paddr     = m_pages.address_of_page(pageindex);
        ppage->deallocate(paddr, itemindex);
        if (ppage->is_empty())
        {
            s32 const c = (xcountTrailingZeros(ppage->m_item_size) - 3);
            ASSERT(c >= 0 && c < c_max_num_sizes);
            m_used_page_list_per_size[c].remove_item(sizeof(llnode_t), m_pages.m_page_list, pageindex);
            m_pages.release_page(pageindex);
        }
    }

    class superfreelist_t
    {
    public:
        void initialize(superheap_t& heap, xvmem* vmem, u64 address_range, u32 num_pages_to_cache, u16 item_size);
        void deinitialize(superheap_t& heap);

        u16  alloc();
        void dealloc(u16 index);

        inline void* idx2ptr(u16 i) const
        {
            u16 const                pageindex = i / m_items_per_page;
            u16 const                itemindex = i - (pageindex * m_items_per_page);
            superpage_t const* const ppage     = &m_pages.m_page_array[pageindex];
            void* const              paddr     = m_pages.address_of_page(pageindex);
            return ppage->idx2ptr(paddr, itemindex);
        }

        inline u16 ptr2idx(void* ptr) const
        {
            u32 const                pageindex = (u32)(todistance(m_pages.m_address, ptr) / m_pages.m_page_size);
            superpage_t const* const ppage     = &m_pages.m_page_array[pageindex];
            void* const              paddr     = m_pages.address_of_page(pageindex);
            u32 const                itemindex = ppage->ptr2idx(paddr, ptr);
            return (pageindex << 16) | (itemindex & 0xFFFF);
        }

        template <typename T> T* base() { return (T*)m_pages.m_address; }
        template <typename T> T* at(u16 i) { return (T*)idx2ptr(i); }

        u32          m_item_size;
        u32          m_items_per_page;
        superpages_t m_pages;
        llhead_t     m_active_pages;
    };

    void superfreelist_t::initialize(superheap_t& heap, xvmem* vmem, u64 address_range, u32 size_to_pre_allocate, u16 item_size)
    {
        m_item_size = item_size;
        m_pages.initialize(heap, vmem, address_range, size_to_pre_allocate);
        m_active_pages.reset();
        m_items_per_page = m_pages.m_page_size / m_item_size;
    }

    void superfreelist_t::deinitialize(superheap_t& heap) { m_pages.deinitialize(heap); }

    u16 superfreelist_t::alloc()
    {
        void*        paddress = nullptr;
        superpage_t* ppage    = nullptr;
        u32          ipage    = 0xffffffff;
        if (m_active_pages.is_nil())
        {
            // Get a page and initialize that page for this size
            u16 const ipage = m_pages.checkout_page(m_item_size);
            m_active_pages.insert(sizeof(llnode_t), m_pages.m_page_list, ipage);
        }
        else
        {
            ipage = m_active_pages.m_index;
            ppage = &m_pages.m_page_array[ipage];
        }

        if (ppage != nullptr)
        {
            paddress  = toaddress(m_pages.m_address, (u64)ipage * m_pages.m_page_size);
            void* ptr = ppage->allocate(paddress);
            if (ppage->is_full())
            {
                m_active_pages.remove_head(sizeof(llnode_t), m_pages.m_page_list);
            }
            return ptr2idx(ptr);
        }
        else
        {
            return NIL;
        }
    }

    void superfreelist_t::dealloc(u16 i)
    {
        u16 const          pageindex = i / m_items_per_page;
        u16 const          itemindex = i - (pageindex * m_items_per_page);
        superpage_t* const ppage     = &m_pages.m_page_array[pageindex];
        void* const        paddr     = m_pages.address_of_page(pageindex);
        ppage->deallocate(paddr, itemindex);
        if (ppage->is_empty())
        {
            m_active_pages.remove_item(sizeof(llnode_t), m_pages.m_page_list, pageindex);
            m_pages.release_page(pageindex);
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

        m_free_chunk_list.initialize(sizeof(llnode_t), m_chunk_list, m_max_chunks_to_cache, m_chunk_cnt - m_max_chunks_to_cache, m_chunk_cnt);
        if (m_max_chunks_to_cache > 0)
        {
            m_cache_chunk_list.initialize(sizeof(llnode_t), m_chunk_list, 0, m_max_chunks_to_cache, m_max_chunks_to_cache);
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
            chunkindex.m_index = m_cache_chunk_list.remove_headi(sizeof(llnode_t), m_chunk_list);
            if (chunkindex.is_nil())
            {
                chunkindex.m_index = m_free_chunk_list.remove_headi(sizeof(llnode_t), m_chunk_list);
                if (chunkindex.is_nil())
                {
                    // panic; reason OOO
                    return nullptr;
                }
                initialize_chunk(sfsa, chunkindex.m_index, size, bin);
                commit_chunk(chunkindex.m_index, size, bin);
            }
            else
            {
                initialize_chunk(sfsa, chunkindex.m_index, size, bin);
                if (bin.m_use_binmap == 0)
                {
                    commit_chunk(chunkindex.m_index, size, bin);
                }
            }
            m_used_chunk_list_per_size[bin.m_alloc_bin_index - m_bin_base].insert(sizeof(llnode_t), m_chunk_list, chunkindex.m_index);
        }

        bool        chunk_is_now_full = false;
        void* const ptr               = allocate_from_chunk(sfsa, chunkindex.m_index, size, bin, chunk_is_now_full);
        if (chunk_is_now_full) // Chunk is full, no more allocations possible
        {
            m_used_chunk_list_per_size[bin.m_alloc_bin_index - m_bin_base].remove_headi(sizeof(llnode_t), m_chunk_list);
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
                m_used_chunk_list_per_size[bin.m_alloc_bin_index - m_bin_base].remove_item(sizeof(llnode_t), m_chunk_list, chunkindex);
            }
            deinitialize_chunk(sfsa, chunkindex, bin);
            if (!m_cache_chunk_list.is_full())
            {
                m_cache_chunk_list.insert(sizeof(llnode_t), m_chunk_list, chunkindex);
            }
            else
            {
                decommit_chunk(chunkindex, bin);
                m_free_chunk_list.insert(sizeof(llnode_t), m_chunk_list, chunkindex);
            }
        }
        else if (chunk_was_full)
        {
            m_used_chunk_list_per_size[bin.m_alloc_bin_index - m_bin_base].insert(sizeof(llnode_t), m_chunk_list, chunkindex);
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
    struct region_t
    {
        struct chunk_t : llnode_t
        {
            u16 m_elem_used;
            u16 m_bin_index;
            u32 m_bin_map;
            u32 m_page_index;
        };

        struct block_t : llnode_t
        {
            lhead_t  m_chunks_list_free;
            llhead_t m_chunks_list_cached;
            u32*     m_chunks_array;
            u16      m_config_index;
            u16      m_chunks_used;
        };

        struct config_t
        {
            config_t(u16 redirect, u16 chunks_max, u16 chunks_shift)
                : m_redirect(redirect)
                , m_chunks_max(chunks_max)
                , m_chunks_shift(chunks_shift)
            {
            }
            u8  m_redirect;
            u16 m_chunks_max;
            u8  m_chunks_shift;
        };

        static const s32 c_num_configs            = 32;
        const config_t   c_configs[c_num_configs] = {
            config_t(0, 0, 0),      config_t(0, 0, 0),    config_t(0, 0, 0),    config_t(0, 0, 0),      config_t(0, 0, 0),      config_t(0, 0, 0),     config_t(0, 0, 0),     config_t(0, 0, 0),
            config_t(0, 0, 0),      config_t(0, 0, 0),    config_t(0, 0, 0),    config_t(0, 0, 0),      config_t(0, 0, 0),      config_t(0, 0, 0),     config_t(0, 0, 0),     config_t(0, 0, 0),
            config_t(16, 2048, 16), config_t(0, 0, 0),    config_t(0, 0, 0),    config_t(19, 2048, 19), config_t(20, 1024, 20), config_t(21, 512, 21), config_t(22, 256, 22), config_t(23, 128, 23),
            config_t(24, 64, 24),   config_t(25, 32, 25), config_t(26, 16, 26), config_t(27, 8, 27),    config_t(28, 4, 28),    config_t(29, 2, 29),   config_t(0, 0, 0),     config_t(0, 0, 0),
        };

        static inline u32 chunk_size_to_config_index(u32 const chunk_size)
        {
            s32 const config_index = xcountTrailingZeros(xceilpo2(chunk_size));
            return config_index;
        }

        u16 obtain_block(u32 const config_index)
        {
            u16 const num_chunks = c_configs[config_index].m_chunks_max;

            u16 const block_index = m_blocks_list_free.remove_headi(sizeof(block_t), m_blocks_array);
            block_t*  block       = &m_blocks_array[block_index];

            block->unlink();
            block->m_chunks_array = (u32*)m_fsa.alloc(sizeof(u16) * num_chunks);
            block->m_config_index = config_index;
            block->m_chunks_used  = 0;

            list_t chunks_list_free;
            chunks_list_free.initialize(sizeof(lindex_t), (lnode_t*)block->m_chunks_array, 0, num_chunks, num_chunks);
            block->m_chunks_list_free = chunks_list_free.m_head;
            block->m_chunks_list_cached.reset();
            return block_index;
        }

        llindex_t allocate_chunk(u32 chunk_size, u8 bin_index)
        {
            u32 const config_index = chunk_size_to_config_index(chunk_size);
            u16       block_index  = NIL;
            if (m_block_per_group_list_active[config_index].is_nil())
            {
                block_index = obtain_block(config_index);
                m_block_per_group_list_active[config_index].insert(sizeof(block_t), m_blocks_array, block_index);
            }
            else
            {
                block_index = m_block_per_group_list_active[config_index].m_index;
            }

            // Here we have a block where we can get a chunk from
            block_t* block       = &m_blocks_array[block_index];
            u16      chunk_index = NIL;
            if (!block->m_chunks_list_cached.is_nil())
            {
                chunk_index = block->m_chunks_list_cached.remove_headi(sizeof(chunk_t), m_chunks_array.base<chunk_t>());
                block->m_chunks_used += 1;
            }
            else if (!block->m_chunks_list_free.is_nil())
            {
                u32 const index     = block->m_chunks_list_free.remove_i(sizeof(lnode_t), (lnode_t*)block->m_chunks_array);
                chunk_t*  chunk     = (chunk_t*)m_chunks_array.alloc();
                chunk->m_bin_index  = bin_index;
                chunk->m_bin_map    = 0xffffffff;
                chunk->m_page_index = (block_index << (m_blocks_shift - m_page_shift)) + (index << (c_configs[block->m_config_index].m_chunks_shift - m_page_shift));
                chunk->m_elem_used  = 0;
                chunk->unlink();
                chunk_index                  = m_chunks_array.ptr2idx(chunk);
                block->m_chunks_array[index] = chunk_index;
                block->m_chunks_used += 1;
            }
            else
            {
                // Error, this segment should have been removed from 'm_segment_per_chunk_size_active'
                ASSERT(false);
            }

            // Check if block is now empty
            if (block->m_chunks_used == c_configs[config_index].m_chunks_max)
            {
                m_block_per_group_list_active[config_index].remove_headi(sizeof(block_t), m_blocks_array);
            }

            // Return the chunk index
            return chunk_index;
        }

        void deallocate_chunk(chunk_t* chunk)
        {
            u32 block_index;
            u32 block_chunk_index;
            u32 chunk_index;
            get_from_page_index(chunk->m_page_index, block_index, block_chunk_index, chunk_index);

            block_t*  block        = &m_blocks_array[block_index];
            u32 const config_index = block->m_config_index;

            if (block->m_chunks_used == c_configs[config_index].m_chunks_max)
            {
                m_block_per_group_list_active[config_index].insert(sizeof(block_t), m_blocks_array, block_index);
            }

            block->m_chunks_used -= 1;
            block->m_chunks_list_cached.insert(sizeof(chunk_t), m_chunks_array.base<llnode_t>(), chunk_index);
            if (block->m_chunks_used == 0)
            {
                // NOTE: Destroy the resources of this block (decommit cached chunks, cached chunks, chunks array)
                m_block_per_group_list_active[config_index].remove_item(sizeof(block_t), m_blocks_array, block_index);
                m_blocks_list_free.insert(sizeof(block_t), m_blocks_array, block_index);
            }
        }

        void* get_chunk_base_address(u32 const page_index) const { return toaddress(m_address_base, page_index * m_page_size); }
        void  get_from_page_index(u32 const page_index, u32& block_index, u32& chunk_index_in_block, u32& chunk_index)
        {
            u32 const page_index_to_block_index_shift       = (m_blocks_shift - m_page_shift);
            block_index                                     = page_index >> page_index_to_block_index_shift;
            block_t*  block                                 = &m_blocks_array[block_index];
            u32 const block_page_index                      = page_index & ((1 << page_index_to_block_index_shift) - 1);
            u32 const block_page_index_to_chunk_index_shift = (c_configs[block->m_config_index].m_chunks_shift - m_page_shift);
            chunk_index_in_block                            = block_page_index >> block_page_index_to_chunk_index_shift;
            chunk_index                                     = block->m_chunks_array[chunk_index_in_block];
        }

        chunk_t* get_chunk_from_page_index(u32 const page_index)
        {
            u32 block_index;
            u32 chunk_index_in_block;
            u32 chunk_index;
            get_from_page_index(page_index, block_index, chunk_index_in_block, chunk_index);
            return m_chunks_array.at<chunk_t>(chunk_index);
        }

        block_t* get_block_from_page_index(u32 const page_index)
        {
            u32 const block_index = page_index >> (m_blocks_shift - m_page_shift);
            return &m_blocks_array[block_index];
        }

        // When deallocating, call this to get the page-index which you can than use
        // to get the 'chunk_t*'.
        u32 address_to_page_index(void* ptr) const { return (u32)(todistance(m_address_base, ptr) >> m_page_shift); }

        superfsa_t      m_fsa;
        superfreelist_t m_chunks_array;
        llhead_t        m_block_per_group_list_active[32];
        void*           m_address_base;
        u64             m_address_range;
        u32             m_page_size;
        u32             m_page_shift;   // e.g. 16 (1<<16 = 64 KB)
        s16             m_blocks_shift; // e.g. 25 (1<<30 =  1 GB)
        block_t*        m_blocks_array;
        llist_t         m_blocks_list_free;
    };

    // Example Config:
    //
    //     Region: 128 GB
    //     Block :   1 GB
    //
    //  C :  Chunk Size - Redirect Index - Max Chunk Count - Binmaps Or Page-Count
    //  0 :       4 KB  -          NA    -        NA       -      NA
    //  1 :       8 KB  -          NA    -        NA       -      NA
    //  2 :      16 KB  -          NA    -        NA       -      NA
    //  3 :      32 KB  -          NA    -        NA       -      NA
    //  4 :      64 KB  -           4    -      2048       -      Binmaps
    //  5 :     128 KB  -          NA    -        NA       -      NA
    //  6 :     256 KB  -          NA    -        NA       -      NA
    //  7 :     512 KB  -           8    -      2048       -      Page-Count
    //  8 :       1 MB  -           8    -      1024       -      Page-Count
    //  9 :       2 MB  -           9    -       512       -      Page-Count
    // 10 :       4 MB  -          11    -       256       -      Page-Count
    // 11 :       8 MB  -          11    -       128       -      Page-Count
    // 12 :      16 MB  -          13    -        64       -      Page-Count
    // 13 :      32 MB  -          13    -        32       -      Page-Count
    // 14 :      64 MB  -          14    -        16       -      Page-Count
    // 15 :     128 MB  -          15    -         8       -      Page-Count
    // 16 :     256 MB  -          16    -         4       -      Page-Count
    // 17 :     512 MB  -          17    -         2       -      Page-Count

} // namespace xcore
