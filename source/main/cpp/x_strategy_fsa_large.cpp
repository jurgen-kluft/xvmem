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
    struct xblock_info_t
    {
        void reset()
        {
            m_clr = 0xff;
            m_set = 0;
        }
        u8 m_clr;
        u8 m_set;
    };

    // One block is able to 'cover 32 * 8 = 256 elements
    // Block info will tell use quickly where to find a
    // free element.
    struct xblock_t
    {
        u32 m_words[8];
    };

    static u32  s_allocsize_to_bits(u32 allocsize, u32 pagesize, u32 bw, u32 ws);
    static u32  s_bits_to_allocsize(u32 b, u32 w, u32 pagesize);
    static u32  s_allocsize_to_bwidth(u32 allocsize, u32 pagesize);
    static u64  s_allocsize_to_blockrange(u32 allocsize, u32 pagesize);
    static bool s_has_empty_slot(u32 slot, u32 ws);
    static u32  s_get_empty_slot(u32 slot, u32 ws);
    static u32  s_set_slot_empty(u32 slot, u32 ws);
    static u32  s_set_slot_occupied(u32 slot, u32 ws);
    static u32  s_get_slot_value(u32 slot, u32 bw, u32 ws);
    static u32  s_clr_slot_value(u32 slot, u32 bw, u32 ws);
    static u32  s_set_slot_value(u32 slot, u32 bw, u32 ws, u32 ab);
    static u32  s_get_slot_mask(u32 slot, u32 bw, u32 ws);

    static inline bool is_block_full(xblock_info_t* block) { return block->m_set == 0xff; }
    static inline bool is_block_empty(xblock_info_t* block) { return block->m_clr == 0xff; }
    static inline u32  get_word_index(u8 words_set) { return (u32)xfindFirstBit((u16) ~((u16)words_set | (u16)0xff00)); }

    class xalloc_fsa_large : public xalloc
    {
    public:
        virtual void* v_allocate(u32 size, u32 alignment) X_FINAL;
        virtual u32   v_deallocate(void* ptr) X_FINAL;
        virtual void  v_release() X_FINAL;

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        xalloc*           m_main_heap;
        xfsa*             m_node_heap;
        void*             m_address_base;
        u64               m_address_range;
        u32               m_allocsize;
        u32               m_pagesize;
        u32               m_block_count;
        xblock_t**        m_block_array;
        xblock_info_t*    m_binfo_array;
        llnode*           m_list_array;
        llist             m_block_empty_list;
        llist             m_block_used_list;
        llist             m_block_full_list;
    };

    xalloc* create_alloc_fsa_large(xalloc* main_heap, xfsa* node_heap, void* mem_base, u64 mem_range, u32 pagesize, u32 allocsize)
    {
        ASSERT(sizeof(xblock_t) == node_heap->size());

        u64 const         block_range = s_allocsize_to_blockrange(allocsize, pagesize);
        u32 const         block_count = (u32)(mem_range / block_range);
        xblock_t**        block_array = (xblock_t**)main_heap->allocate(sizeof(xblock_t*) * block_count);
        xblock_info_t*    binfo_array = (xblock_info_t*)main_heap->allocate(sizeof(xblock_info_t) * block_count);
        llnode* list_nodes  = (llnode*)main_heap->allocate(sizeof(llnode) * block_count);

        xalloc_fsa_large* instance = main_heap->construct<xalloc_fsa_large>();
        instance->m_main_heap      = main_heap;
        instance->m_node_heap      = node_heap;
        instance->m_address_base   = mem_base;
        instance->m_address_range  = mem_range;
        instance->m_allocsize      = xceilpo2(allocsize);
        instance->m_pagesize       = pagesize;
        instance->m_block_count    = block_count;
        instance->m_block_array    = block_array;
        instance->m_binfo_array    = binfo_array;
        instance->m_list_array     = list_nodes;

        // Initialize the block list by linking all blocks into the empty list
        instance->m_block_empty_list.initialize(list_nodes, block_count, block_count);
        instance->m_block_used_list = llist(0, block_count);
        instance->m_block_full_list = llist(0, block_count);

        // All block pointers are initially NULL
        for (u32 i = 0; i < block_count; ++i)
        {
            instance->m_binfo_array[i].reset();
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
                m_node_heap->deallocate(m_block_array[i]);
                m_block_array[i] = nullptr;
            }
        }

        m_main_heap->deallocate(m_block_array);
        m_main_heap->deallocate(m_binfo_array);
        m_main_heap->deallocate(m_list_array);
        m_main_heap->destruct(this);
    }

    static inline xblock_t* get_block_at(xalloc_fsa_large* instance, u32 bi)
    {
        ASSERT(bi < instance->m_block_count);
        return instance->m_block_array[bi];
    }

    static inline xblock_info_t* get_binfo_at(xalloc_fsa_large* instance, u32 bi)
    {
        ASSERT(bi < instance->m_block_count);
        return &instance->m_binfo_array[bi];
    }

    static xblock_t* create_block_at(xalloc_fsa_large* instance, u32 bi)
    {
        ASSERT(sizeof(xblock_t) == instance->m_node_heap->size());
        xblock_t* block = (xblock_t*)instance->m_node_heap->allocate();
        for (s32 i = 0; i < 8; ++i)
            block->m_words[i] = 0;
        instance->m_block_array[bi] = block;
        return block;
    }

    static void destroy_block_at(xalloc_fsa_large* instance, u32 bi)
    {
        xblock_t* block = instance->m_block_array[bi];
        instance->m_node_heap->deallocate(block);
        instance->m_block_array[bi] = nullptr;
        instance->m_binfo_array[bi].reset();
    }

    static void* allocate_from(xalloc_fsa_large* instance, u32 bi, u32 allocsize)
    {
        xblock_t*      block = instance->m_block_array[bi];
        xblock_info_t* binfo = &instance->m_binfo_array[bi];
        ASSERT(!is_block_full(binfo));

        u32 const bw  = s_allocsize_to_bwidth(instance->m_allocsize, instance->m_pagesize);
        u32 const wi  = get_word_index(binfo->m_set);                                           // index of word that is not full
        u32       wd  = block->m_words[wi];                                                     // word data
        u32 const ws  = s_get_empty_slot(wd, bw);                                               // get index of empty slot in word data
        u32 const ew  = 1 << bw;                                                                // number of elements per word
        u32 const ab  = s_allocsize_to_bits(allocsize, instance->m_pagesize, bw, ws);           // get the actual bits of the requested alloc-size
        u64 const bs  = s_allocsize_to_blockrange(instance->m_allocsize, instance->m_pagesize); // memory size of one block
        void*     ptr = x_advance_ptr(instance->m_address_base, (u64)bi * bs);                  // memory base of this block
        ptr           = x_advance_ptr(ptr, ((wi * ew) + ws) * instance->m_allocsize);           // the word-index and slot index determine the offset
        wd            = s_set_slot_value(wd, bw, ws, ab);                                       // write the bits into the element slot
        wd            = s_set_slot_occupied(wd, ws);                                            // mark this slot as occupied
        if (!s_has_empty_slot(wd, bw))
        {
            binfo->m_set = binfo->m_set | (1 << wi); // Mark this word as full
        }
        binfo->m_clr       = binfo->m_clr & ~(1 << wi); // Mark this word as not clear
		block->m_words[wi] = wd;
        return ptr;
    }

    static u32 deallocate_from(xalloc_fsa_large* instance, u32 const bi, void* const ptr)
    {
        xblock_t* const      block = instance->m_block_array[bi];
        xblock_info_t* const binfo = &instance->m_binfo_array[bi];

        void* const block_base = x_advance_ptr(instance->m_address_base, (u64)bi * s_allocsize_to_blockrange(instance->m_allocsize, instance->m_pagesize));
        u32 const   i          = (u32)(((u64)ptr - (u64)block_base) / (u64)instance->m_allocsize);
        u32 const   bw         = s_allocsize_to_bwidth(instance->m_allocsize, instance->m_pagesize); // element width in bits
        u32 const   ew         = 1 << bw;                                                            // number of elements per word
        u32 const   wi         = i >> bw;                                                            // word index
        u32 const   ws         = i & (ew - 1);                                                       // word slot index
        u32 const   np         = s_get_slot_value(block->m_words[wi], bw, ws);                       // get the content of the element
        block->m_words[wi]     = s_clr_slot_value(block->m_words[wi], bw, ws);                       // mask out the element
        block->m_words[wi]     = s_set_slot_empty(block->m_words[wi], ws);                           // mark this slot as empty
        binfo->m_set           = binfo->m_set & ~(1 << wi);                                          // make it known that this word is not full
        if (block->m_words[wi] == 0)
        {
            binfo->m_clr = binfo->m_clr | (1 << wi); // make it known that this word is clear
        }
        return (np * instance->m_pagesize);
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
                llnode* pnode = m_block_empty_list.remove_head(m_list_array);
                llindex const     inode = m_block_empty_list.node2idx(m_list_array, pnode);
                xblock_t*         block = create_block_at(this, inode.get());
                m_block_used_list.insert(m_list_array, inode);
                return allocate_from(this, inode.get(), size);
            }
        }
        else
        {
            llindex const  bi    = m_block_used_list.m_head;
            xblock_info_t* binfo = get_binfo_at(this, bi.get());
            void*          ptr   = allocate_from(this, bi.get(), size);
            if (is_block_full(binfo))
            {
                m_block_used_list.remove_item(m_list_array, bi);
                m_block_full_list.insert(m_list_array, bi);
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
        u64 const block_range = s_allocsize_to_blockrange(m_allocsize, m_pagesize);
        u32 const block_index = (u32)(((u64)ptr - (u64)m_address_base) / block_range);
        ASSERT(block_index < m_block_count);
        xblock_info_t* binfo = get_binfo_at(this, block_index);
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
                m_block_used_list.remove_item(m_list_array, block_index);
                destroy_block_at(this, block_index);
                m_block_empty_list.insert(m_list_array, block_index);
            }
        }
        return size;
    }

    u32 s_bits_to_allocsize(u32 b, u32 bw, u32 pagesize)
	{

		return (b + 1) * pagesize; 
	}

    u32 s_allocsize_to_bits(u32 allocsize, u32 pagesize, u32 bw, u32 ws)
    {
        ASSERT(bw >= 1 && bw <= 5);
        u32 const n = ((allocsize + pagesize - 1) / pagesize);
        return n - 1;
    }

    //                      bits     slot -> final number of bits / number of slots / 1<<shift
    // <=   64KB   -   0 =   0    +   1   ->   1  / 32 / 5
    // <=  128KB   -   1 =   1    +   1   ->   2  / 16 / 4
    // <=  256KB   -   3 =   2    +   1   ->   4  /  8 / 3
    // <=  512KB   -   7 =   3    +   1   ->   4  /  8 / 3
    // <=    1MB   -  15 =   4    +   1   ->   8  /  4 / 2
    // <=    2MB   -  31 =   5    +   1   ->   8  /  4 / 2
    // <=    4MB   -  63 =   6    +   1   ->   8  /  4 / 2
    // <=    8MB   - 127 =   7    +   1   ->   8  /  4 / 2
    // <=   16MB   - 255 =   8    +   1   ->   16 /  2 / 1
    // <=   32MB   - 511 =   9    +   1   ->   16 /  2 / 1

    u32 s_allocsize_to_bwidth(u32 allocsize, u32 pagesize)
    {
        u32 const n = ((allocsize + pagesize - 1) / pagesize);
        u16 const p = (u16)xceilpo2(n) - 1;
        u64 const i = 0x1111111122223345;
        u32 const b = 16 - xcountLeadingZeros(p);
        u32 const w = (((i >> (4 * b)) & 0xF));
        return w;
    }

    u64 s_allocsize_to_blockrange(u32 allocsize, u32 pagesize)
    {
        // Compute the memory range of a xblock_t
        u16 const bw = s_allocsize_to_bwidth(allocsize, pagesize);
        u16 const n  = (8 * sizeof(u32) * 8) / (32 >> bw);
        u64 const s  = (u64)n * (u64)allocsize;
        return s;
    }

    bool s_has_empty_slot(u32 slot, u32 bw)
    {
        // Slot 'occupied' bits are the lowest part of the 'slot' integer
        u32 const mask = ((u64)1 << (1 << bw)) - 1;
        return (slot & mask) != mask;
    }

    u32 s_get_empty_slot(u32 slot, u32 bw)
    {
        u32 const mask = ((u64)1 << (1 << bw)) - 1;
        slot           = ~((slot & mask) | ~mask);

        // e.g. slot = 0x000000EF, w = 8 => mask = 0x000000FF

        // so the bits indicating occupied slots are in the lower 8 bits
        // mask those bits and or with the inversed-mask, 0x000000EF | 0xFFFFFF00
        // inverse it to get: 0x00000010

        // find the first bit set
        s32 const esi = xcountTrailingZeros(slot);
        return esi;
    }

    u32 s_set_slot_empty(u32 slot, u32 ws) { return slot & ~(1 << ws); }
    u32 s_set_slot_occupied(u32 slot, u32 ws) { return slot | (1 << ws); }

    u32 s_get_slot_value(u32 slot, u32 bw, u32 ws)
    {
        ASSERT(bw >= 1 && bw <= 5);
        u32 const w = 32 >> bw;
        u64 const n = (1 << (w - 1)) - 1;
        u32 const s = 1 << bw;
        u32 const v = (((u64)slot >> s) >> (ws * (w - 1))) & n;
        return v + 1;
    }

    u32 s_clr_slot_value(u32 slot, u32 bw, u32 ws)
    {
        ASSERT(bw >= 1 && bw <= 5);
        u32 const w    = 32 >> bw;
        u64 const n    = (1 << (w - 1)) - 1;
        u32 const s    = 1 << bw;
        u32 const mask = (u32)(((n << (ws * (w - 1))) << s));
        return slot & ~mask;
    }

    u32 s_set_slot_value(u32 slot, u32 bw, u32 ws, u32 ab)
    {
        ASSERT(bw >= 1 && bw <= 5);
        u32 const w = 32 >> bw;
        u64 const n = ab;
        u32 const s = 1 << bw;
        u32 const v = (u32)(((n << (ws * (w - 1))) << s));
        return slot | v;
    }

    u32 s_get_slot_mask(u32 slot, u32 bw, u32 ws)
    {
        ASSERT(bw >= 1 && bw <= 5);
        u32 const w    = 32 >> bw;
        u64 const n    = (1 << (w - 1)) - 1;
        u32 const s    = 1 << bw;
        u32 const mask = (u32)(((n << (ws * (w - 1))) << s) | ((u64)1 << ws));
        return mask;
    }

    u32  xfsa_large_utils::allocsize_to_bits(u32 allocsize, u32 pagesize, u32 bw, u32 ws) { return s_allocsize_to_bits(allocsize, pagesize, bw, ws); }
    u32  xfsa_large_utils::bits_to_allocsize(u32 b, u32 w, u32 pagesize) { return s_bits_to_allocsize(b, w, pagesize); }
    u32  xfsa_large_utils::allocsize_to_bwidth(u32 allocsize, u32 pagesize) { return s_allocsize_to_bwidth(allocsize, pagesize); }
    u64  xfsa_large_utils::allocsize_to_blockrange(u32 allocsize, u32 pagesize) { return s_allocsize_to_blockrange(allocsize, pagesize); }
    bool xfsa_large_utils::has_empty_slot(u32 slot, u32 bw) { return s_has_empty_slot(slot, bw); }
    u32  xfsa_large_utils::get_empty_slot(u32 slot, u32 bw) { return s_get_empty_slot(slot, bw); }
    u32  xfsa_large_utils::set_slot_empty(u32 slot, u32 ws) { return s_set_slot_empty(slot, ws); }
    u32  xfsa_large_utils::set_slot_occupied(u32 slot, u32 ws) { return s_set_slot_occupied(slot, ws); }
    u32  xfsa_large_utils::get_slot_value(u32 slot, u32 bw, u32 ws) { return s_get_slot_value(slot, bw, ws); }
    u32  xfsa_large_utils::clr_slot_value(u32 slot, u32 bw, u32 ws) { return s_clr_slot_value(slot, bw, ws); }
    u32  xfsa_large_utils::set_slot_value(u32 slot, u32 bw, u32 ws, u32 ab) { return s_set_slot_value(slot, bw, ws, ab); }
    u32  xfsa_large_utils::get_slot_mask(u32 slot, u32 bw, u32 ws) { return s_get_slot_mask(slot, bw, ws); }

} // namespace xcore
