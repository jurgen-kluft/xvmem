
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
    static inline u64 alignto(u64 value, u64 alignment) { return (value + (alignment - 1)) & ~(alignment - 1); }
    static inline void* toaddress(void* base, u64 offset) { return (void*)((u64)base + offset);  }

    // Can only allocate, used internally to allocate initially required memory
    class superheap
    {
    public:
        void  initialize(xvmem* vmem, u64 memory_range, u64 size_to_pre_allocate);
        void* allocate(u32 size);

        void* m_address;
        xvmem* m_vmem;
        u32   m_size_alignment;
        u32   m_page_size;
        u32   m_page_count_current;
        u32   m_page_count_maximum;
        u64   m_ptr;
    };

    void superheap::initialize(xvmem* vmem, u64 memory_range, u64 size_to_pre_allocate)
    {
        u32 attributes = 0;
        m_vmem = vmem;
        m_vmem->reserve(memory_range, m_page_size, attributes, m_address);
        m_size_alignment = 32;
        m_page_count_maximum = memory_range / m_page_size;
        m_page_count_current = size_to_pre_allocate;
        m_ptr = 0;
    }

    void* superheap::allocate(u32 size)
    {
        size = alignto(size, m_size_alignment);
        u64 ptr_max = ((u64)m_page_count_current * m_page_size);
        if ((m_ptr + size) > ptr_max)
        {
            // add more pages
            u32 const page_count = alignto(m_ptr + size, m_page_size) / (u64)m_page_size;
            u32 const page_count_to_commit = page_count - m_page_count_current;
            u64 commit_base = ((u64)m_page_count_current * m_page_size);
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
    class superfsa
    {
    public:
        void  initialize(superheap* heap, u64 address_range, u32 num_pages_to_cache);

        u32   alloc(u32 size);
        void  dealloc(u32 index);

        void* idx2ptr(u32 i) const;
        u32   ptr2idx(void* ptr) const;

    private:
        struct page : public xalist_t::node_t
        {
            u16             m_item_size;
            u16             m_item_index;
            u16             m_item_available;
            xalist_t::head  m_item_freelist;
        };

        xvmem*   m_vmem;
        void*    m_address;
        u64      m_address_range;
        u32      m_page_size;
        page*    m_page_array;
        xalist_t m_free_page_list;
        xalist_t m_cached_page_list;
        xalist_t m_used_page_list_per_size[16];
    };

    struct binmap
    {
        struct config
        {
            config(u8 l1_len, u8 l2_len, u32 count)
                : m_l1_len(l1_len)
                , m_l2_len(l2_len)
                , m_count(count)
            {
                ASSERT((m_count <= 32) || (m_count <= (m_l2_len * 16)));
                ASSERT((m_count <= 32) || ((m_l1_len * 16) <= m_l2_len));
            }

            u8 const  m_l1_len;
            u8 const  m_l2_len;
            u16 const m_count;
        };

        binmap(u32 count)
            : m_l0(0)
            , m_free_index(0)
        {
        }

        inline u16* get_l1() const { return (u16*)this + (sizeof(binmap) / 2); }
        inline u16* get_l2(config const& cfg) const { return (u16*)(this + (sizeof(binmap) / 2) + cfg.m_l1_len); }

        void init(config const& cfg);
        void set(config const& cfg, u32 bin);
        void clr(config const& cfg, u32 bin);
        bool get(config const& cfg, u32 bin) const;
        u32  find(config const& cfg) const;

        u32 m_l0;
        u32 m_free_index;
        u32 m_l1_offset;
        u32 m_l2_offset;
    };

    struct asbin
    {
        u32 m_alloc_size;
        u32 m_alloc_bin : 8;
        u32 m_alloc_index : 8;
        u32 m_use_binmaps : 1;
        u32 m_use_allocmaps : 1;
    };

    struct superalloc
    {
        struct chunk
        {
            u16 m_elem_used;
            u16 m_elem_size;
        };

        superalloc() {}
        superalloc(void* memory_base, u64 memory_range, u64 chunksize) {}

        void*             m_memory_base;
        u64               m_memory_range;
        u64               m_chunk_size;
        u32               m_chunk_cnt;
        chunk*            m_chunk_array;
        u32*              m_binmaps;
        u32*              m_allocmaps;
        xalist_t::node_t* m_chunk_list;
        xalist_t          m_free_chunk_list;
        xalist_t          m_cache_chunk_list;
        xalist_t::head    m_used_chunk_list_per_size[32];
    };

    class superallocator
    {
    public:
        const asbin m_asbins[128] = {
            asbin(),
            asbin()
        };

        superalloc m_allocators[32] = {
            superalloc(nullptr, 0, 0), //
            superalloc(nullptr, 0, 0), //
            superalloc(nullptr, 0, 0), //
            superalloc(nullptr, 0, 0), //
            superalloc(nullptr, 0, 0), //
            superalloc(nullptr, 0, 0), //
            superalloc(nullptr, 0, 0), //
            superalloc(nullptr, 0, 0), //
            superalloc(nullptr, 0, 0)  //
        };

        superfsa m_internal_fsa;
    };

    void initialize(superalloc::chunk& c)
    {
        c.m_elem_used = 0;
        c.m_elem_size = 0;
    }

    void initialize(superalloc& a, xvmem* vmem)
    {
        u32 page_size;
        vmem->reserve(a.m_memory_range, page_size, 0, a.m_memory_base);

        for (s32 i = 0; i < a.m_chunk_cnt; ++i)
        {
            initialize(a.m_chunk_array[i]);
            a.m_binmaps[i] = 0xffffffff;
        }
        a.m_free_chunk_list.initialize(&a.m_chunk_list[0], a.m_chunk_cnt, a.m_chunk_cnt);
        for (s32 i = 0; i < 32; i++)
        {
            a.m_used_chunk_list_per_size[i] = xalist_t::NIL;
        }
    }

    void* allocate(superalloc& a, u32 size, u32 bin)
    {
        // Get the chunk from 'm_used_chunk_list_per_size[bin]'
        //   If it is NIL then take one from 'm_free_chunk_list_cache'
        //   If it is NIL then take one from 'm_free_chunk_list'
        //   Initialize Chunk using ChunkInfo related to bin
        //   If according to ChunkInfo we need a BinMap, allocate it and set offset
        //   Add it to 'm_used_chunk_list_per_size[bin]'

        // Remove an element from the chunk, if chunk is empty set 'm_used_chunk_list_per_size[bin]' to NIL

        return nullptr;
    }

    u32 deallocate(superalloc& a, void* ptr)
    {
        // Convert 'ptr' to chunk-index and elem-index
        // Convert m_chunks[chunk-index].m_elem_size to bin-index
        // Copy m_elem_size from chunk to return it from this function
        // Push the now free element to the chunk, if chunk was full then add it to 'm_used_chunk_list_per_size[bin]'
        // If chunk is now totally empty add it to the 'm_free_chunk_list_cache'
        //  If 'm_free_chunk_list_cache' is full then decommit the chunk add it to the 'm_free_chunk_list'

        return 0;
    }

    void resetarray(u32 count, u32 len, u16* data)
    {
        for (u32 i = 0; i < len; i++)
            data[i] = 0;
        u32 wi2 = count / 16;
        u32 wd2 = 0xffff << (count & (16 - 1));
        for (; wi2 < len; wi2++)
        {
            data[wi2] = wd2;
            wd2       = 0xffff;
        }
    }
    void binmap::init(config const& cfg)
    {
        // Set those bits that we never touch to '1' the rest to '0'
        u16 l0len = cfg.m_count;
        if (cfg.m_count > 32)
        {
            u16* const l2 = get_l2(cfg);
            resetarray(cfg.m_count, cfg.m_l2_len, l2);
            u16* const l1 = get_l1();
            resetarray(cfg.m_l2_len, cfg.m_l1_len, l1);
            l0len = cfg.m_l1_len;
        }
        m_l0 = 0xffffffff << (l0len & (32 - 1));
    }

    void binmap::set(config const& cfg, u32 k)
    {
        if (cfg.m_count <= 32)
        {
            u32 const bi0 = 1 << (k & (32 - 1));
            u32 const wd0 = m_l0 | bi0;
            m_l0          = wd0;
        }
        else
        {
            u16* const l2  = get_l2(cfg);
            u32 const  wi2 = k / 16;
            u16 const  bi2 = (u16)1 << (k & (16 - 1));
            u16 const  wd2 = l2[wi2] | bi2;
            if (wd2 == 0xffff)
            {
                u16* const l1  = get_l1();
                u32 const  wi1 = wi2 / 16;
                u16 const  bi1 = 1 << (wi1 & (16 - 1));
                u16 const  wd1 = l1[wi1] | bi1;
                if (wd1 == 0xffff)
                {
                    u32 const bi0 = 1 << (wi1 & (32 - 1));
                    u32 const wd0 = m_l0 | bi0;
                    m_l0          = wd0;
                }
                l1[wi1] = wd1;
            }
            l2[wi2] = wd2;
        }
    }

    void binmap::clr(config const& cfg, u32 k)
    {
        if (cfg.m_count <= 32)
        {
            u32 const bi0 = 1 << (k & (32 - 1));
            u32 const wd0 = m_l0 & ~bi0;
            m_l0          = wd0;
        }
        else
        {
            u16* const l2  = get_l2(cfg);
            u32 const  wi2 = k / 16;
            u16 const  bi2 = (u16)1 << (k & (16 - 1));
            u16 const  wd2 = l2[wi2];
            if (wd2 == 0xffff)
            {
                u16* const l1 = get_l1();

                u32 const wi1 = wi2 / 16;
                u16 const bi1 = 1 << (wi1 & (16 - 1));
                u16 const wd1 = l1[wi1];
                if (wd1 == 0xffff)
                {
                    u32 const bi0 = 1 << (wi1 & (32 - 1));
                    u32 const wd0 = m_l0 & ~bi0;
                    m_l0          = wd0;
                }
                l1[wi1] = wd1 & ~bi1;
            }
            l2[wi2] = wd2 & ~bi2;
        }
    }

    bool binmap::get(config const& cfg, u32 k) const
    {
        if (cfg.m_count <= 32)
        {
            u32 const bi0 = 1 << (k & (32 - 1));
            return (m_l0 & bi0) != 0;
        }
        else
        {
            u16* const l2  = get_l2(cfg);
            u32 const  wi2 = k / 16;
            u16 const  bi2 = (u16)1 << (k & (16 - 1));
            u16 const  wd2 = l2[wi2];
            return (wd2 & bi2) != 0;
        }
    }

    u32 binmap::find(config const& cfg) const
    {
        u32 const bi0 = (u32)xfindFirstBit(~m_l0);
        if (cfg.m_count <= 32)
        {
            return bi0;
        }
        else
        {
            u32 const  wi1 = bi0 * 16;
            u16* const l1  = get_l1();
            u32 const  bi1 = (u32)xfindFirstBit((u16)~l1[wi1]);
            u32 const  wi2 = bi1 * 16;
            u16* const l2  = get_l2(cfg);
            u32 const  bi2 = (u32)xfindFirstBit((u16)~l2[wi2]);
            return bi2 + wi2 * 16;
        }
    }
} // namespace xcore
