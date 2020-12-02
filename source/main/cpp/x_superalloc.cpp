
#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_doubly_linked_list.h"
#include "xvmem/private/x_binmap.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    static inline u64   alignto(u64 value, u64 alignment) { return (value + (alignment - 1)) & ~(alignment - 1); }
    static inline u32   alignto(u32 value, u32 alignment) { return (value + (alignment - 1)) & ~(alignment - 1); }
    static inline void* toaddress(void* base, u64 offset) { return (void*)((u64)base + offset); }

    // Can only allocate, used internally to allocate initially required memory
    class superheap_t
    {
    public:
        void  initialize(xvmem* vmem, u64 memory_range, u64 size_to_pre_allocate);
        void* allocate(u32 size);

        void*  m_address;
        xvmem* m_vmem;
        u32    m_size_alignment;
        u32    m_page_size;
        u32    m_page_count_current;
        u32    m_page_count_maximum;
        u64    m_ptr;
    };

    void superheap_t::initialize(xvmem* vmem, u64 memory_range, u64 size_to_pre_allocate)
    {
        u32 attributes = 0;
        m_vmem         = vmem;
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
        void initialize(superheap_t* heap, xvmem* vmem, u64 address_range, u32 num_pages_to_cache);

        u32  alloc(u32 size);
        void dealloc(u32 index);

        void* idx2ptr(u32 i) const;
        u32   ptr2idx(void* ptr) const;

    private:
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

            inline u32 ptr2idx(void* const ptr, void* const elem) const { return (u32)(((u64)elem - (u64)ptr) / m_item_size); }

            inline u32* idx2ptr(void* const ptr, u32 const index) const { return (u32*)((xbyte*)ptr + ((u64)index * (u64)m_item_size)); }

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

        void* pageindex_to_address(u16 ipage) const { return toaddress(m_address, (u64)ipage * m_page_size); }

        xvmem*   m_vmem;
        void*    m_address;
        u64      m_address_range;
        u32      m_page_count;
        u32      m_page_size;
        page_t*  m_page_array;
        llist_t  m_free_page_list;
        llist_t  m_cached_page_list;
        llhead_t m_used_page_list_per_size[16];
    };

    void superfsa_t::initialize(superheap_t* heap, xvmem* vmem, u64 address_range, u32 num_pages_to_cache)
    {
        m_vmem         = vmem;
        u32 attributes = 0;
        m_vmem->reserve(address_range, m_page_size, attributes, m_address);
        m_address_range = address_range;
        m_page_count    = (u32)(address_range / (u64)m_page_size);
        m_page_array    = (page_t*)heap->allocate(m_page_count * sizeof(page_t));
        m_free_page_list.initialize(m_page_array + num_pages_to_cache, m_page_count - num_pages_to_cache, m_page_count);
        for (u32 i = 0; i < m_page_count; i++)
            m_page_array[i].initialize(8, 0);
        for (u32 i = 0; i < 16; i++)
            m_used_page_list_per_size[i].reset();

        if (num_pages_to_cache > 0)
        {
            m_cached_page_list = llist_t(0, num_pages_to_cache);
            for (u32 i = 0; i < num_pages_to_cache; i++)
            {
                m_cached_page_list.insert_tail(m_page_array, i);
            }
            m_vmem->commit(m_address, m_page_size, num_pages_to_cache);
        }
    }

    u32 superfsa_t::alloc(u32 size)
    {
        size               = alignto(size, 8);
        size               = xceilpo2(size);
        s32 const c        = (xcountTrailingZeros(size) - 3) - 1;
        void*     paddress = nullptr;
        page_t*   ppage    = nullptr;
        u32       ipage    = 0xffffffff;
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
        void* const   paddr     = pageindex_to_address(pageindex);
        ppage->deallocate(paddr, itemindex);
        if (ppage->is_full())
        {
            s32 const c = (xcountTrailingZeros(ppage->m_item_size) - 3) - 1;
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

    void* superfsa_t::idx2ptr(u32 i) const
    {
        u16 const           pageindex = i >> 16;
        u16 const           itemindex = i & 0xFFFF;
        page_t const* const ppage     = &m_page_array[pageindex];
        void* const         paddr     = pageindex_to_address(pageindex);
        return ppage->idx2ptr(paddr, itemindex);
    }

    u32 superfsa_t::ptr2idx(void* ptr) const
    {
        u32 const           pageindex = (u32)(((u64)ptr - (u64)m_address) / m_page_size);
        page_t const* const ppage     = &m_page_array[pageindex];
        void* const         paddr     = pageindex_to_address(pageindex);
        u32 const           itemindex = ppage->ptr2idx(paddr, ptr);
        return (pageindex << 16) | (itemindex & 0xFFFF);
        ;
    }

    // @superalloc manages an address range, a list of chunks and a range of allocation sizes.
    struct superalloc_t
    {
        struct chunk_t
        {
            u16 m_elem_used;
            u16 m_elem_size;
        };

        superalloc_t() {}
        superalloc_t(void* memory_base, u64 memory_range, u64 chunksize, u32 numchunks2cache) {}

        void*     m_memory_base;
        u64       m_memory_range;
        u64       m_chunk_size;
        u32       m_chunk_cnt;
        chunk_t*  m_chunk_array;
        u32*      m_binmaps;
        u32*      m_allocmaps;
        llnode_t* m_chunk_list;
        llist_t   m_free_chunk_list;
        llist_t   m_cache_chunk_list;
        llhead_t  m_used_chunk_list_per_size[32];
    };

    class superallocator
    {
    public:
        struct bin_t
        {
            inline bin_t()
                : m_alloc_size(0)
                , m_alloc_bin(0)
                , m_alloc_index(0)
                , m_use_binmaps(0)
                , m_use_allocmaps(0)
            {
            }

            inline bin_t(u32 allocsize, u8 bin, u8 allocindex, u8 use_binmaps, u8 use_allocmaps)
                : m_alloc_size(allocsize)
                , m_alloc_bin(bin)
                , m_alloc_index(allocindex)
                , m_use_binmaps(use_binmaps)
                , m_use_allocmaps(use_allocmaps)
            {
            }
            u32 m_alloc_size;
            u32 m_alloc_bin : 8;
            u32 m_alloc_index : 8; // The index into the allocator that manages us
            u32 m_use_binmaps : 1; // How do we manage a chunk (binmaps or allocmaps)
            u32 m_use_allocmaps : 1;
        };

        // every allocation size has settings have some mapping info as well
        // as info that indicates how to initialize a chunk.
        const bin_t m_asbins[128] = {
            bin_t( 8,0,0,1,0),   // alloc-size=8
            bin_t(16,0,0,1,0)
        };

        superalloc_t m_allocators[32] = {
            superalloc_t(nullptr, 0, 0, 0), //
            superalloc_t(nullptr, 0, 0, 0), //
            superalloc_t(nullptr, 0, 0, 0), //
            superalloc_t(nullptr, 0, 0, 0), //
            superalloc_t(nullptr, 0, 0, 0), //
            superalloc_t(nullptr, 0, 0, 0), //
            superalloc_t(nullptr, 0, 0, 0), //
            superalloc_t(nullptr, 0, 0, 0), //
            superalloc_t(nullptr, 0, 0, 0)  //
        };

        superheap_t m_internal_heap;
        superfsa_t  m_internal_fsa;
    };

    void initialize(superalloc_t::chunk_t& c)
    {
        c.m_elem_used = 0;
        c.m_elem_size = 0;
    }

    void initialize(superalloc_t& a, xvmem* vmem, superheap_t& heap)
    {
        a.m_chunk_array = (superalloc_t::chunk_t*)heap.allocate(a.m_chunk_cnt * sizeof(superalloc_t::chunk_t));
        a.m_chunk_list  = (llnode_t*)heap.allocate(a.m_chunk_cnt * sizeof(llnode_t));
        a.m_binmaps     = (u32*)heap.allocate(a.m_chunk_cnt * sizeof(u32));
        a.m_allocmaps   = (u32*)heap.allocate(a.m_chunk_cnt * sizeof(u32));
        for (u32 i = 0; i < a.m_chunk_cnt; ++i)
        {
            initialize(a.m_chunk_array[i]);
            a.m_binmaps[i] = 0xffffffff;
        }
        a.m_free_chunk_list.initialize(&a.m_chunk_list[0], a.m_chunk_cnt, a.m_chunk_cnt);
        for (s32 i = 0; i < 32; i++)
        {
            a.m_used_chunk_list_per_size[i].reset();
        }

        u32 page_size;
        vmem->reserve(a.m_memory_range, page_size, 0, a.m_memory_base);
    }

    void* allocate(superalloc_t& a, u32 size, u32 bin)
    {
        // Get the chunk from 'm_used_chunk_list_per_size[bin]'
        //   If it is NIL then take one from 'm_free_chunk_list_cache'
        //   If it is NIL then take one from 'm_free_chunk_list'
        //   Initialize Chunk using ChunkInfo related to bin (need binmap or allocmap?)
        //   If according to ChunkInfo we need a BinMap, allocate it and set offset
        //   Add it to 'm_used_chunk_list_per_size[bin]'
        //   Commit needed pages

        // Remove an element from the chunk, if chunk is empty set 'm_used_chunk_list_per_size[bin]' to NIL

        return nullptr;
    }

    u32 deallocate(superalloc_t& a, void* ptr)
    {
        // Convert 'ptr' to chunk-index and elem-index
        // Convert m_chunks[chunk-index].m_elem_size to bin-index
        // Copy m_elem_size from chunk to return it from this function
        // Push the now free element to the chunk, if chunk is not empty then add it to 'm_used_chunk_list_per_size[bin]'
        //  otherwise add it to the 'm_cached_chunk_list'.
        // If chunk is empty add it to the 'm_free_chunk_list_cache'
        //  If 'm_free_chunk_list_cache' is full then decommit the chunk add it to the 'm_free_chunk_list'

        return 0;
    }

} // namespace xcore
