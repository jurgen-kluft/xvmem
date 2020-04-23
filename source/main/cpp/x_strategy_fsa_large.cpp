#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"
#include "xbase/x_hibitset.h"

#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_strategy_fsa_large.h"
#include "xvmem/private/x_doubly_linked_list.h"

namespace xcore
{
    static void* advance_ptr(void* ptr, u64 size) { return (void*)((uptr)ptr + size); }

    static u32 allocsize_to_bits(u32 allocsize, u32 pagesize);
    static u32 bits_to_allocsize(u32 b, u32 w, u32 pagesize);
    static u32 allocsize_to_bwidth(u32 allocsize, u32 pagesize);
    static u64 allocsize_to_blockrange(u32 allocsize, u32 pagesize);

    struct xbinfo_t
    {
        void reset()
        {
            m_words_clr = 0xff;
            m_words_set = 0;
        }
        u8 m_words_clr;
        u8 m_words_set;
    };

    struct xblock_t
    {
        u32 m_words[8];
    };

    static inline bool is_block_full(xbinfo_t* block) { return block->m_words_set == 0xff; }
    static inline bool is_block_empty(xbinfo_t* block) { return block->m_words_clr == 0xff; }
    static inline u32  get_word_index(u8 words_set) { return xfindFirstBit(~(words_set | 0xffffff00)); }
    static bool        has_empty_slot(u32 slot, u32 w);
    static u32         get_empty_slot(u32 slot, u32 w);

    class xalloc_fsa_large : public xalloc
    {
    public:
        virtual void* v_allocate(u32 size, u32 alignment) X_FINAL;
        virtual u32   v_deallocate(void* ptr) X_FINAL;
        virtual void  v_release();

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        xalloc*                m_allocator;
        void*                  m_address_base;
        u64                    m_address_range;
        u32                    m_allocsize;
        u32                    m_pagesize;
        u32                    m_block_count;
        xblock_t**             m_block_array;
        xbinfo_t*              m_binfo_array;
        xarray_list_t::node_t* m_list_array;
        xarray_list_t          m_block_empty_list;
        xarray_list_t          m_block_used_list;
        xarray_list_t          m_block_full_list;
    };

    xalloc* create_alloc_fsa_large(xalloc* allocator, void* mem_base, u64 mem_range, u32 pagesize, u32 allocsize)
    {
        u64 const               block_range = allocsize_to_blockrange(allocsize, pagesize);
        u32 const               block_count = (u32)(mem_range / block_range);
        xblock_t**              block_array = (xblock_t**)allocator->allocate(sizeof(xblock_t*) * block_count);
        xbinfo_t*               binfo_array = (xbinfo_t*)allocator->allocate(sizeof(xbinfo_t) * block_count);
        xarray_list_t::node_t* list_nodes  = (xarray_list_t::node_t*)allocator->allocate(sizeof(xarray_list_t::node_t) * block_count);

        xalloc_fsa_large* instance   = allocator->construct<xalloc_fsa_large>();
        instance->m_allocator        = allocator;
        instance->m_address_base     = mem_base;
        instance->m_address_range    = mem_range;
        instance->m_allocsize        = xceilpo2(allocsize);
        instance->m_pagesize         = pagesize;
        instance->m_block_count      = block_count;
        instance->m_block_array      = block_array;
        instance->m_binfo_array      = binfo_array;
        instance->m_list_array       = list_nodes;
        instance->m_block_used_list  = xarray_list_t();
        instance->m_block_full_list  = xarray_list_t();
        instance->m_block_empty_list = xarray_list_t();
		instance->m_block_empty_list.m_head = 0;

		// Initialize the block list by linking all blocks into the empty list
        for (u32 i = 0; i < block_count; ++i)
        {
            instance->m_list_array[i].m_next = i + 1;
            instance->m_list_array[i].m_prev = i - 1;
        }
        instance->m_list_array[0].m_prev     = block_count - 1;
        instance->m_list_array[block_count - 1].m_next = 0;

		// All block pointers are initially NULL
        for (u32 i = 0; i < block_count; ++i)
        {
            instance->m_block_array[i] = nullptr;
        }

        return instance;
    }

    void xalloc_fsa_large::v_release()
    {
        u32 const c = m_block_count;
        for (u32 i = 0; i < c; ++i)
        {
            if (m_block_array[i] != nullptr)
            {
                m_allocator->deallocate(m_block_array[i]);
            }
        }

        m_allocator->deallocate(m_block_array);
        m_allocator->deallocate(m_binfo_array);
        m_allocator->deallocate(m_list_array);
        m_allocator->deallocate(this);
    }

    static xblock_t* get_block_at(xalloc_fsa_large* instance, u32 i) { return instance->m_block_array[i]; }
    static xbinfo_t* get_binfo_at(xalloc_fsa_large* instance, u32 i) { return &instance->m_binfo_array[i]; }

    static xblock_t* create_block_at(xalloc_fsa_large* instance, u32 block_index)
    {
        xblock_t* block = (xblock_t*)instance->m_allocator->allocate(sizeof(xblock_t));
        for (s32 i = 0; i < 8; ++i)
            block->m_words[i] = 0;
        instance->m_block_array[block_index] = block;
        instance->m_binfo_array[block_index].reset();
        return block;
    }

    static void destroy_block_at(xalloc_fsa_large* instance, u32 i)
    {
        xblock_t* block = instance->m_block_array[i];
        instance->m_allocator->deallocate(block);
        instance->m_block_array[i] = nullptr;
    }

    static void* allocate_from(xalloc_fsa_large* instance, u32 index, u32 allocsize)
    {
        xblock_t* block = instance->m_block_array[index];
        xbinfo_t* binfo = &instance->m_binfo_array[index];

        u32 const w  = allocsize_to_bwidth(instance->m_allocsize, instance->m_pagesize);
        u32 const wi = get_word_index(binfo->m_words_set);                 // index of word that is not full
        u32 const wd = block->m_words[wi];                                 // word data
        u32 const ws = get_empty_slot(wd, w);                              // get index of empty slot in word data
        u32 const ew = 32 / w;                                             // number of elements per word
        u32 const ab = allocsize_to_bits(allocsize, instance->m_pagesize); // get the actual bits of the requested alloc-size

        u64 const bs  = allocsize_to_blockrange(instance->m_allocsize, instance->m_pagesize); // memory size of one block
        void*     ptr = advance_ptr(instance->m_address_base, (u64)index * bs);               // memory base of this block
        ptr           = advance_ptr(ptr, ((wi * ew) + ws) * instance->m_allocsize);           // the word-index and slot index determine the offset

        block->m_words[wi] = block->m_words[wi] | (ab << (ws * w)); // write the bits into the element slot
        if (!has_empty_slot(block->m_words[wi], w))
        {
            binfo->m_words_set = binfo->m_words_set | (1 << wi); // Mark this word as full
        }
        binfo->m_words_clr = binfo->m_words_clr & ~(1 << wi); // Mark this word as not clear

        return ptr;
    }

    static u32 deallocate_from(xalloc_fsa_large* instance, u32 const index, void* const ptr)
    {
        xblock_t* const block = instance->m_block_array[index];
        xbinfo_t* const binfo = &instance->m_binfo_array[index];

        void* const block_base = advance_ptr(instance->m_address_base, (u64)index * allocsize_to_blockrange(instance->m_allocsize, instance->m_pagesize));
        u32 const   i          = (u32)(((u64)ptr - (u64)block_base) / (u64)instance->m_allocsize);
        u32 const   w          = allocsize_to_bwidth(instance->m_allocsize, instance->m_pagesize); // element width in bits
        u32 const   ew         = 32 / w;                                                           // number of elements per word
        u32 const   wi         = i / ew;                                                           // word index
        u32 const   ei         = i - (wi * ew);                                                    // element index
        u32 const   em         = ((1 << w) - 1);                                                   // element mask
        u32 const   np         = (block->m_words[wi] >> (w * ei)) & em;                            // get the content of the element
        block->m_words[wi]     = block->m_words[wi] & ~(em << (w * ei));                           // mask out the element
        binfo->m_words_set     = binfo->m_words_set & ~(1 << wi);                                  // make it known that this word is not full
        if (block->m_words[wi] == 0)
        {
            binfo->m_words_clr = binfo->m_words_clr | (1 << wi); // make it known that this word is clear
        }
        return bits_to_allocsize(np, w, instance->m_pagesize);
    }

    void* xalloc_fsa_large::v_allocate(u32 size, u32 alignment)
    {
        // Obtain a usable block
        // From block allocate an entry
        // When block is now full add it to 'full' list
        if (m_block_used_list.is_empty())
        {
            if (!m_block_empty_list.is_empty())
            {
                xarray_list_t::node_t* pnode = m_block_empty_list.remove_head(m_list_array);
				u16 const inode     = m_block_empty_list.node2idx(m_list_array, pnode);
                xblock_t* block = create_block_at(this, inode);
                m_block_used_list.insert(m_list_array, inode);
                return allocate_from(this, inode, size);
            }
        }
        else
        {
            u16 const i     = m_block_used_list.m_head;
            xbinfo_t* binfo = get_binfo_at(this, i);
            void*     ptr   = allocate_from(this, i, size);
            if (is_block_full(binfo))
            {
				m_block_used_list.remove_item(m_list_array, i);
				m_block_full_list.insert(m_list_array, i);
            }
            return ptr;
        }
        return nullptr;
    }

    u32 xalloc_fsa_large::v_deallocate(void* ptr)
    {
        // Determine block that we are part of
        // Deallocate this entry from the block
        // If block was full remove it from 'full' list
        // If block is now empty -> free block
        // Else add block to 'used' list
        u32       size        = 0;
        u64 const block_range = allocsize_to_blockrange(m_allocsize, m_pagesize);
        u32 const block_index = (u32)(((u64)ptr - (u64)m_address_base) / block_range);
        xbinfo_t* binfo       = get_binfo_at(this, block_index);
        if (is_block_full(binfo))
        {
            size = deallocate_from(this, block_index, ptr);
			m_block_full_list.remove_item(m_list_array, block_index);
			m_block_used_list.insert(m_list_array, block_index);
        }
        else
        {
            size = deallocate_from(this, block_index, ptr);
            if (is_block_empty(binfo))
            {
				m_block_full_list.remove_item(m_list_array, block_index);
                destroy_block_at(this, block_index);
				m_block_empty_list.insert(m_list_array, block_index);
            }
        }
        return size;
    }

    u32 bits_to_allocsize(u32 b, u32 w, u32 pagesize)
    {
        if (w == 1)
        {
            return pagesize;
        }
        else if (w == 2)
        {
            u32 const r = 0x2;
            u32 const n = (b & (r - 1)) + (((b & (r - 1)) == 0) ? r : 0);
            return n * pagesize;
        }
        else if (w == 4)
        {
            u32 const r = 0x8;
            u32 const n = (b & (r - 1)) + (((b & (r - 1)) == 0) ? r : 0);
            return n * pagesize;
        }
        else if (w == 8)
        {
            u32 const r = 0x80;
            u32 const n = (b & (r - 1)) + (((b & (r - 1)) == 0) ? r : 0);
            return n * pagesize;
        }
        else if (w == 16)
        {
            u32 const r = 0x8000;
            u32 const n = (b & (r - 1)) + (((b & (r - 1)) == 0) ? r : 0);
            return n * pagesize;
        }
        else
        {
            ASSERT(false); // 'w' should be 1,2,4,8 or 16
            return pagesize;
        }
    }

    u32 allocsize_to_bits(u32 allocsize, u32 pagesize)
    {
        u32 const p = xceilpo2((allocsize + pagesize - 1) / pagesize);
        if (p & 0xFF00)
        {
            u32 const n = ((allocsize + pagesize - 1) / pagesize);
            return 0x8000 | n;
        }
        else if (p & 0x00F0)
        {
            u32 const n = ((allocsize + pagesize - 1) / pagesize);
            return 0x80 | n;
        }
        else if (p & 0x000C)
        {
            u32 const n = ((allocsize + pagesize - 1) / pagesize);
            return 0x8 | n;
        }
        else if (p & 0x0002)
        {
            u32 const n = ((allocsize + pagesize - 1) / pagesize);
            return 0x2 | n;
        }
        else if (p & 0x0001)
        {
            return 0x1;
        }
        else
        {
            ASSERT(false); // p should fall into the above conditions
            return 0x0;
        }
    }

    u32 allocsize_to_bwidth(u32 allocsize, u32 pagesize)
    {
        u32 const p = xceilpo2((allocsize + pagesize - 1) / pagesize);
        u32       w;
        if (p & 0xff00)
            w = 16;
        else if (p & 0x00f0)
            w = 8;
        else if (p & 0x000c)
            w = 4;
        else if (p & 0x0002)
            w = 2;
        else if (p & 0x0001)
            w = 1;

        return w;
    }

    u64 allocsize_to_blockrange(u32 allocsize, u32 pagesize)
    {
        // Compute the memory range of a xblock_t
        u16 const w = allocsize_to_bwidth(allocsize, pagesize);
        u16 const n = (8 * sizeof(u32) * 8) / w;
        u64 const s = (u64)n * (u64)allocsize;
        return s;
    }

    bool has_empty_slot(u32 slot, u32 w)
    {
        switch (w)
        {
            case 1: return (slot & 0xffffffff) != 0xffffffff;
            case 2: return (slot & 0xaaaaaaaa) != 0xaaaaaaaa;
            case 4: return (slot & 0x88888888) != 0x88888888;
            case 8: return (slot & 0x80808080) != 0x80808080;
            case 16: return (slot & 0x80008000) != 0x80008000;
        }
        return false;
    }

    u32 get_empty_slot(u32 slot, u32 w)
    {
        u32 b = 0;
        if (w == 1)
        {
            if ((slot & 0x0000ffff) == 0xffff)
            {
                slot = slot >> 16;
                b += 16;
            }
            if ((slot & 0x000000ff) == 0xff)
            {
                slot = slot >> 8;
                b += 8;
            }
            if ((slot & 0x0000000f) == 0xf)
            {
                slot = slot >> 4;
                b += 4;
            }
            if ((slot & 0x00000003) == 0x3)
            {
                slot = slot >> 2;
                b += 2;
            }
            if ((slot & 0x00000001) == 0x1)
            {
                slot = slot >> 1;
                b += 1;
            }
        }
        else if (w == 2)
        {
            if ((slot & 0x0000aaaa) == 0xaaaa)
            {
                slot = slot >> 16;
                b += 8;
            }
            if ((slot & 0x000000aa) == 0xaa)
            {
                slot = slot >> 8;
                b += 4;
            }
            if ((slot & 0x0000000a) == 0xa)
            {
                slot = slot >> 4;
                b += 2;
            }
            if ((slot & 0x00000002) == 0x2)
            {
                slot = slot >> 2;
                b += 1;
            }
        }
        else if (w == 4)
        {
            if ((slot & 0x00008888) == 0x8888)
            {
                slot = slot >> 16;
                b += 4;
            }
            if ((slot & 0x00000088) == 0x88)
            {
                slot = slot >> 8;
                b += 2;
            }
            if ((slot & 0x00000008) == 0x8)
            {
                slot = slot >> 4;
                b += 1;
            }
        }
        else if (w == 8)
        {
            if ((slot & 0x00008080) == 0x8080)
            {
                slot = slot >> 16;
                b += 2;
            }
            if ((slot & 0x00000080) == 0x80)
            {
                slot = slot >> 8;
                b += 1;
            }
        }
        else if (w == 16)
        {
            if ((slot & 0x00008000) == 0x8000)
            {
                slot = slot >> 16;
                b += 1;
            }
        }
        return b;
    }



} // namespace xcore
