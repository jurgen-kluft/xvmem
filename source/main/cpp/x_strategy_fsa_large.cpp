#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"
#include "xbase/x_hibitset.h"

#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_strategy_fsa_large.h"

namespace xcore
{
    namespace xfsa_large
    {
        static void* advance_ptr(void* ptr, u64 size) { return (void*)((uptr)ptr + size); }

        static u32 allocsize_to_bits(u32 allocsize, u32 pagesize);
        static u32 bits_to_allocsize(u32 b, u32 w, u32 pagesize);
        static u32 allocsize_to_bwidth(u32 allocsize, u32 pagesize);
        static u32 allocsize_to_blockrange(u32 allocsize, u32 pagesize);

        const u16 INDEX_NIL = 0xffff;

        struct xlist_t
        {
            u16 m_next; // Next element after this element
            u16 m_prev; // Previous element before this element
        };

        static void add_to_list(xlist_t* list, u16& head, u16 item);
        static bool is_empty_list(u16 const head) { return head == INDEX_NIL; }
        static u16  pop_from_list(xlist_t* list, u16& head);
        static void remove_from_list(xlist_t* list, u16& head, u16 item);

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

        struct xinstance_t
        {
            XCORE_CLASS_PLACEMENT_NEW_DELETE

            xalloc*    m_allocator;
            void*      m_address_base;
            u64        m_address_range;
            u32        m_allocsize;
            u32        m_pagesize;
            u32        m_block_count;
            xblock_t** m_block_array;
            xbinfo_t*  m_binfo_array;
            xlist_t*   m_list_array;
            u16        m_block_empty_list;
            u16        m_block_used_list;
            u16        m_block_full_list;
        };

        //  64 KB,  1 = 8KB + 2KB + 4KB = 14KB
        // 128 KB,  2 = 8KB + 2KB + 4KB = 14KB
        // 256 KB,  4 = 8KB + 2KB + 4KB = 14KB
        // 512 KB,  4 = 8KB + 2KB + 4KB = 14KB
        //   1 MB,  8 = 4KB + 1KB + 2KB =  7KB
        //   2 MB,  8 = 2KB + 512B +  1KB =  3KB + 512B
        //   4 MB,  8 = 1KB + 256B + 512B =  1KB + 768B
        //   8 MB,  8 = 512B + 128B + 256B = 896B
        //  16 MB, 16 = 512B + 128B + 256B = 896B
        //  32 MB, 16 = 256B +  64B + 128B = 448B

        // e.g. 64KB, memory range = 16 GB, block_count = 16 GB / 16 MB = 1024 blocks
        //      block_array = 1024 * 8 = 8192 bytes
        //      binfo_array = 1024 * 2 = 2048 bytes
        //      list_array  = 1024 * 4 = 4096 bytes
        // e.g. 1M, memory range = 16 GB, block_count = 16 GB / (1MB * 32) = 512 blocks
        //      block_array = 512 * 8 = 4096 bytes
        //      binfo_array = 512 * 2 = 1024 bytes
        //      list_array  = 512 * 4 = 2048 bytes

        xinstance_t* create(xalloc* allocator, void* mem_base, u64 mem_range, u32 pagesize, u32 allocsize)
        {
            u32 const  block_range = allocsize_to_blockrange(allocsize, pagesize);
            u32 const  block_count = (u32)(mem_range / block_range);
            xblock_t** block_array = (xblock_t**)allocator->allocate(sizeof(xblock_t*) * block_count);
            xbinfo_t*  binfo_array = (xbinfo_t*)allocator->allocate(sizeof(xbinfo_t) * block_count);
            xlist_t*   list_nodes  = (xlist_t*)allocator->allocate(sizeof(xlist_t) * block_count);

            xinstance_t* instance        = allocator->construct<xinstance_t>();
            instance->m_allocator        = allocator;
            instance->m_address_base     = mem_base;
            instance->m_address_range    = mem_range;
            instance->m_allocsize        = xceilpo2(allocsize);
            instance->m_pagesize         = pagesize;
            instance->m_block_count      = block_count;
            instance->m_block_array      = block_array;
            instance->m_binfo_array      = binfo_array;
            instance->m_list_array       = list_nodes;
            instance->m_block_empty_list = 0;
            instance->m_block_used_list  = INDEX_NIL;
            instance->m_block_full_list  = INDEX_NIL;

            u32 const c = block_count;
            for (u32 i = 0; i < c; ++i)
            {
                instance->m_block_array[i] = nullptr;
            }
            for (u32 i = 0; i < c; ++i)
            {
                instance->m_list_array[i].m_next = i + 1;
                instance->m_list_array[i].m_prev = i - 1;
            }
            instance->m_list_array[0].m_prev     = c - 1;
            instance->m_list_array[c - 1].m_next = 0;

            return instance;
        }

        void destroy(xinstance_t* instance)
        {
            u32 const c = instance->m_block_count;
            for (u32 i = 0; i < c; ++i)
            {
                if (instance->m_block_array[i] != nullptr)
                {
                    instance->m_allocator->deallocate(instance->m_block_array[i]);
                }
            }

            instance->m_allocator->deallocate(instance->m_block_array);
            instance->m_allocator->deallocate(instance->m_binfo_array);
            instance->m_allocator->deallocate(instance->m_list_array);
            instance->m_allocator->deallocate(instance);
        }

        static xblock_t* get_block_at(xinstance_t* instance, u32 i) { return instance->m_block_array[i]; }
        static xbinfo_t* get_binfo_at(xinstance_t* instance, u32 i) { return &instance->m_binfo_array[i]; }

        static xblock_t* create_block_at(xinstance_t* instance, u32 block_index)
        {
            xblock_t* block = (xblock_t*)instance->m_allocator->allocate(sizeof(xblock_t));
            for (s32 i = 0; i < 8; ++i)
                block->m_words[i] = 0;
            instance->m_block_array[block_index] = block;
            instance->m_binfo_array[block_index].reset();
            return block;
        }

        static void destroy_block_at(xinstance_t* instance, u32 i)
        {
            xblock_t* block = instance->m_block_array[i];
            instance->m_allocator->deallocate(block);
            instance->m_block_array[i] = nullptr;
        }

        static void* allocate_from(xinstance_t* instance, u32 index, u32 allocsize)
        {
            xblock_t* block = instance->m_block_array[index];
            xbinfo_t* binfo = &instance->m_binfo_array[index];

            u32 const w  = allocsize_to_bwidth(instance->m_allocsize, instance->m_pagesize);
            u32 const wi = get_word_index(binfo->m_words_set);                 // index of word that is not full
            u32 const wd = block->m_words[wi];                                 // word data
            u32 const ws = get_empty_slot(wd, w);                              // get index of empty slot in word data
            u32 const ew = 32 / w;                                             // number of elements per word
            u32 const ab = allocsize_to_bits(allocsize, instance->m_pagesize); // get the actual bits of the requested alloc-size

            u32 const bs  = allocsize_to_blockrange(instance->m_allocsize, instance->m_pagesize); // memory size of one block
            void*     ptr = advance_ptr(instance->m_address_base, index * bs);                    // memory base of this block
            ptr           = advance_ptr(ptr, ((wi * ew) + ws) * instance->m_allocsize);           // the word-index and slot index determine the offset

            block->m_words[wi] = block->m_words[wi] | (ab << (ws * w)); // write the bits into the element slot
            if (!has_empty_slot(block->m_words[wi], w))
            {
                binfo->m_words_set = binfo->m_words_set | (1 << wi); // Mark this word as full
            }
            binfo->m_words_clr = binfo->m_words_clr & ~(1 << wi); // Mark this word as not clear

            return ptr;
        }

        static u32 deallocate_from(xinstance_t* instance, u32 const index, void* const ptr)
        {
            xblock_t* const block = instance->m_block_array[index];
            xbinfo_t* const binfo = &instance->m_binfo_array[index];

            void* const block_base = advance_ptr(instance->m_address_base, index * allocsize_to_blockrange(instance->m_allocsize, instance->m_pagesize));
            u32 const   i          = (u32)(((u64)ptr - (u64)block_base) / instance->m_allocsize);
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

        void* allocate(xinstance_t* instance, u32 size, u32 alignment)
        {
            // Obtain a usable block
            // From block allocate an entry
            // When block is now full add it to 'full' list
            if (is_empty_list(instance->m_block_used_list))
            {
                if (!is_empty_list(instance->m_block_empty_list))
                {
                    u16 const i     = pop_from_list(instance->m_list_array, instance->m_block_empty_list);
                    xblock_t* block = create_block_at(instance, i);
                    add_to_list(instance->m_list_array, instance->m_block_used_list, i);
                    return allocate_from(instance, i, size);
                }
            }
            else
            {
                u16 const i     = instance->m_block_used_list;
                xbinfo_t* binfo = get_binfo_at(instance, i);
                void*     ptr   = allocate_from(instance, i, size);
                if (is_block_full(binfo))
                {
                    remove_from_list(instance->m_list_array, instance->m_block_used_list, i);
                    add_to_list(instance->m_list_array, instance->m_block_full_list, i);
                }
                return ptr;
            }
            return nullptr;
        }

        u32 deallocate(xinstance_t* instance, void* ptr)
        {
            // Determine block that we are part of
            // Deallocate this entry from the block
            // If block was full remove it from 'full' list
            // If block is now empty -> free block
            // Else add block to 'used' list
            u32       size        = 0;
            u32 const block_range = allocsize_to_blockrange(instance->m_allocsize, instance->m_pagesize);
            u32 const block_index = (u32)(((u64)ptr - (u64)instance->m_address_base) / block_range);
            xbinfo_t* binfo       = get_binfo_at(instance, block_index);
            if (is_block_full(binfo))
            {
                size = deallocate_from(instance, block_index, ptr);
                remove_from_list(instance->m_list_array, instance->m_block_full_list, block_index);
                add_to_list(instance->m_list_array, instance->m_block_used_list, block_index);
            }
            else
            {
                size = deallocate_from(instance, block_index, ptr);
                if (is_block_empty(binfo))
                {
                    remove_from_list(instance->m_list_array, instance->m_block_used_list, block_index);
                    destroy_block_at(instance, block_index);
                    add_to_list(instance->m_list_array, instance->m_block_empty_list, block_index);
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

        u32 allocsize_to_blockrange(u32 allocsize, u32 pagesize)
        {
            // Compute the memory range of a xblock_t
            u16 const w = allocsize_to_bwidth(allocsize, pagesize);
            u16 const n = (8 * sizeof(u32) * 8) / w;
            u32 const s = n * allocsize;
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

        void add_to_list(xlist_t* list, u16& head, u16 item)
        {
            if (is_empty_list(head))
            {
                head              = item;
                list[head].m_next = head;
                list[head].m_prev = head;
            }
            else
            {
                u16 const next    = head;
                u16 const prev    = list[head].m_prev;
                list[item].m_next = next;
                list[item].m_prev = prev;
                list[next].m_prev = item;
                list[prev].m_next = item;
                head              = item;
            }
        }

        static inline bool is_list_size_one(xlist_t* list, u16 i) { return (list[i].m_next == i && list[i].m_prev == i); }

        u16 pop_from_list(xlist_t* list, u16& head)
        {
            u16 const i = head;
            if (!is_empty_list(head))
            {
                if (is_list_size_one(list, head))
                {
                    head = INDEX_NIL;
                }
                else
                {
                    u16 const next    = list[head].m_next;
                    u16 const prev    = list[head].m_prev;
                    list[prev].m_next = next;
                    list[next].m_prev = prev;

                    head = next;
                }
                list[i].m_next = INDEX_NIL;
                list[i].m_prev = INDEX_NIL;
            }
            return i;
        }

        void remove_from_list(xlist_t* list, u16& head, u16 i)
        {
            if (is_list_size_one(list, head))
            {
                ASSERT(head == i);
                head = INDEX_NIL;
            }
            else
            {
                u16 const next    = list[i].m_next;
                u16 const prev    = list[i].m_prev;
                list[prev].m_next = next;
                list[next].m_prev = prev;
            }
            list[i].m_next = INDEX_NIL;
            list[i].m_prev = INDEX_NIL;
        }

    } // namespace xfsa_large
} // namespace xcore
