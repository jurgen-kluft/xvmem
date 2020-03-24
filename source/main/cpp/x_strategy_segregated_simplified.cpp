#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"
#include "xbase/x_hibitset.h"

#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_binarysearch_tree.h"
#include "xvmem/private/x_strategy_segregated.h"

namespace xcore
{
    namespace xsegregatedstrat2
    {
        const u16 INDEX_NIL = 0xffff;

        struct xmspace_t
        {
            u32 m_alloc_info;    // The allocation info (w,b) managed by this space
            u16 m_alloc_count;   // Number of allocation done in this space
            u16 m_alloc_max;     // Maximum number of allocation that this space can hold
            u32 m_word_free;     // One bit says if a word is full '1' or can be used '0'
            u32 m_word_array[1]; // The array of words that follow this structure in memory
        };

        struct xmlist_t
        {
            xmlist_t()
                : m_next(INDEX_NIL)
                , m_prev(INDEX_NIL)
            {
            }
            u16 m_next; // Next element after this element
            u16 m_prev; // Previous element before this element
        };

        static void add_to_list(xmlist_t* list, u16& head, u16 item)
        {
            if (head == INDEX_NIL)
            {
                head              = item;
                list[head].m_next = head;
                list[head].m_prev = head;
            }
            else
            {
                u16 const prev    = list[head].m_prev;
                list[item].m_next = head;
                list[item].m_prev = prev;
                list[prev].m_next = item;
                list[head].m_prev = item;
            }
        }

        static void remove_from_list(xmlist_t* list, u16& head, u16 item)
        {
            if (head == item)
            {
                head = list[head].m_next;
            }
            u16 const next    = list[item].m_next;
            u16 const prev    = list[item].m_prev;
            list[prev].m_next = next;
            list[next].m_prev = prev;
        }

        struct xmspaces_t
        {
            xmspaces_t()
                : m_address_base(nullptr)
                , m_address_range(0)
                , m_alloc(nullptr)
                , m_mspaces_array(nullptr)
                , m_mspaces_list(nullptr)
                , m_mspaces_count(0)
                , m_mspaces_free(0)
            {
            }
            void*       m_address_base;             // Address base
            u64         m_address_range;            // Address range
            xalloc*     m_alloc;                    // Internal allocator
            xmspace_t** m_mspaces_array;            // Address range is divided into mspaces
            xmlist_t*   m_mspaces_list;             // List nodes belonging to mspaces
            u16         m_mspaces_count;            // The number of mspaces managed
            u16         m_mspaces_free;             // The head index of free mspaces
            u16         m_mspaces_free_by_size[16]; // Free and empty nodes
            u16         m_mspaces_used_by_size[16]; // Used and not yet full/empty
            u16         m_mspaces_full_by_size[16]; // Full nodes
        };

        void         init_mspaces(xmspaces_t* ms);
        static void* advance_ptr(void* ptr, u64 size) { return (void*)((uptr)ptr + size); }
        static u32   allocsize_powerof2(u32 allocsize);
        static u32   allocsize_to_info(u32 allocsize);
        static u32   allocsize_to_bits(u32 allocsize);
        static u64   allocsize_to_nodesize(u32 allocsize);
        static bool  has_empty_slot(u32 slot, u32 w);
        static u32   find_empty_slot(u32 slot, u32 w);

        static void*      allocate(xmspace_t* n, void* base_address, u32 allocsize);
        static void*      allocate(xmspaces_t* t, u32 allocsize);
        static u32        deallocate(xmspace_t* node, void* baseaddress, void* ptr);
        static u32        deallocate(xmspaces_t* t, void* ptr);
        static bool       is_full(xmspace_t* n);
        static bool       was_full(xmspace_t* n);
        static bool       is_empty(xmspace_t* n);
        static xmspace_t* get_space_at(xmspaces_t* t, u32 i);
        static xmspace_t* obtain_space(xmspaces_t* t, u32 allocsize);
        static xmspace_t* alloc_space(xmspaces_t* t, u32 allocsize);
        static void       free_space_at(xmspaces_t* t, u32 i);

        static void remove_from_full_list(xmspaces_t* t, xmspace_t* n) {}
        static void remove_from_used_list(xmspaces_t* t, xmspace_t* n) {}
        static void remove_from_empty_list(xmspaces_t* t, xmspace_t* n) {}
        static void remove_from_list(xmspaces_t* t, xmspace_t* n) {}
        static void add_to_full_list(xmspaces_t* t, xmspace_t* n) {}
        static void add_to_used_list(xmspaces_t* t, xmspace_t* n) {}
        static void add_to_empty_list(xmspaces_t* t, xmspace_t* n) {}

        void init_mspaces(xmspaces_t* ms, xalloc* allocator, void* address_base, u64 address_range)
        {
            ms->m_address_base  = address_base;
            ms->m_address_range = address_range;
            ms->m_alloc         = allocator;

            ms->m_mspaces_count = (address_range / 1024) / (64 * 1024);
            ms->m_mspaces_free  = 0;

            u32 const c         = ms->m_mspaces_count;
            ms->m_mspaces_array = (xmspace_t**)allocator->allocate(sizeof(xmspace_t*) * c);
            ms->m_mspaces_list  = (xmlist_t*)allocator->allocate(sizeof(xmlist_t) * c);

            for (u32 i = 0; i < c; ++i)
            {
                ms->m_mspaces_array[i]       = nullptr;
                ms->m_mspaces_list[i].m_next = i + 1;
                ms->m_mspaces_list[i].m_prev = i - 1;
            }
            ms->m_mspaces_list[0].m_prev     = INDEX_NIL;
            ms->m_mspaces_list[c - 1].m_next = INDEX_NIL;

            for (s32 i = 0; i < 16; ++i)
            {
                ms->m_mspaces_free_by_size[i] = INDEX_NIL;
                ms->m_mspaces_used_by_size[i] = INDEX_NIL;
                ms->m_mspaces_full_by_size[i] = INDEX_NIL;
            }
        }

        void destroy_mspaces(xmspaces_t* ms)
        {
            if (ms->m_alloc != nullptr)
            {
                u32 const c = ms->m_mspaces_count;
                for (u32 i = 0; i < c; ++i)
                {
                    if (ms->m_mspaces_array[i] != nullptr)
                    {
                        ms->m_alloc->deallocate(ms->m_mspaces_array[i]);
                        ms->m_mspaces_array[i] = nullptr;
                    }
                }
                ms->m_alloc->deallocate(ms->m_mspaces_array);
                ms->m_alloc->deallocate(ms->m_mspaces_list);

                ms->m_alloc = nullptr;
            }
        }

        void* allocate(xmspace_t* n, void* baseaddress, u32 allocsize)
        {
            u32 const z = allocsize_to_bits(allocsize);
            u32 const w = (n->m_alloc_info >> 8) & 0xff;
            u32 const b = (n->m_alloc_info >> 0) & 0xff;
            u32 const i = xfindFirstBit(~n->m_word_free);
            u32 const s = n->m_word_array[i];
            const u32 e = find_empty_slot(s, w);
            const u32 m = (2 << w) - 1;
            const u32 t = s | (z << (e * w));
            if (!has_empty_slot(t, w))
            {
                n->m_word_free |= 1 << i; // Mark slot as full
            }
            n->m_word_array[i] = t;
            n->m_alloc_count += 1;
            return advance_ptr(baseaddress, e * (1 << b) * (64 * 1024));
        }

        void* allocate(xmspaces_t* t, u32 allocsize)
        {
            xmspace_t* s = obtain_space(t, allocsize);
            if (s == nullptr)
                return nullptr;
            remove_from_list(t, s);
            void* ptr = allocate(s, t->m_address_base, allocsize);
            if (is_full(s))
            {
                add_to_full_list(t, s);
            }
            else
            {
                add_to_used_list(t, s);
            }
            return ptr;
        }

        u32 deallocate(xmspace_t* s, void* baseaddress, void* ptr)
        {
            u64 const d         = (u64)ptr - (u64)baseaddress;
            u32 const eb        = (s->m_alloc_info >> 0) & 0xff; // Bit-Width of one element
            u32 const ew        = (s->m_alloc_info >> 8) & 0xff; // Power-Of-2-Bit-Width of one element
            u32 const em        = (2 << ew) - 1;                 // Mask of one element
            u32 const es        = (1 << eb) * (64 * 1024);       // Size of one element
            u32 const ei        = d / es;                        // Index of the element
            u32 const eu        = 32 / ew;                       // Number of elements in one word
            u32 const wi        = ei / eu;                       // Word Index of element
            u32 const n         = (s->m_word_array[wi] >> (ei & (eu - 1))) & em;
            s->m_word_array[wi] = s->m_word_array[wi] & ~(em << (ei & (eu - 1)));
            s->m_alloc_count -= 1;
            return n; // Return the number of pages that where actually committed
        }

        u32 deallocate(xmspaces_t* t, void* ptr)
        {
            u64 const  d = ((u64)ptr - (u64)t->m_address_base);
            u64        w = t->m_address_range / t->m_mspaces_count;
            u64        i = d / w;
            u64        o = i * w;
            xmspace_t* s = get_space_at(t, i);
            u32 const  n = deallocate(s, (void*)o, ptr);
            if (was_full(s))
            {
                remove_from_full_list(t, s);
                add_to_used_list(t, s);
            }
            else
            {
                if (is_empty(s))
                {
                    remove_from_used_list(t, s);
                    free_space_at(t, i);
                }
            }
            return n;
        }

        bool is_full(xmspace_t* s)
        {
            u16 const cnt = s->m_alloc_count;
            u16 const max = s->m_alloc_max;
            return cnt == max;
        }

        bool was_full(xmspace_t* s)
        {
            u16 const cnt = s->m_alloc_count;
            u16 const max = s->m_alloc_max;
            return (cnt + 1) == max;
        }

        bool is_empty(xmspace_t* s)
        {
            u16 const cnt = s->m_alloc_count;
            return cnt == 0;
        }

        xmspace_t* get_space_at(xmspaces_t* t, u32 i)
        {
            ASSERT(i < t->m_mspaces_count);
            ASSERT(t->m_mspaces_array[i] != nullptr);
        }

        xmspace_t* alloc_space(xmspaces_t* t, u32 allocsize)
        {
            u32 const  m     = t->m_address_range / t->m_mspaces_count;
            u32 const  k     = (m / allocsize_powerof2(allocsize));
            u32 const  extra = (k / 32); // xmspace_t already has 1 entry
            xmspace_t* s     = t->m_alloc->placement<xmspace_t>(extra * sizeof(u32));
            s->m_alloc_info  = allocsize_to_info(allocsize);
            s->m_alloc_count = 0;
            s->m_alloc_max   = k;
            s->m_word_free   = 0;
            return s;
        }

        void free_space(xmspaces_t* t, xmspace_t* s) { t->m_alloc->deallocate(s); }

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

        u32 find_empty_slot(u32 slot, u32 w)
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
        }

        // allocsize_powerof2
        // this will compute the power-of-2 ceiling of the incoming
        // allocsize using page-size as the alignment.
        u32 allocsize_powerof2(u32 allocsize)
        {
            u32 p = (allocsize / (64 * 1024));
            u32 s = p == 0 ? 0 : 1;
            if (p & 0xff00)
            {
                s += 8;
                p = p >> 8;
            }
            if (p & 0x00f0)
            {
                s += 4;
                p = p >> 4;
            }
            if (p & 0x000c)
            {
                s += 2;
                p = p >> 2;
            }
            if (p & 0x0002)
            {
                s += 1;
                // p = p >> 1;
            }
            return (64 * 1024) * (1 << s);
        }

        u32 allocsize_to_bits(u32 allocsize)
        {
            u32 p = (allocsize / (64 * 1024));
            u32 s = p == 0 ? 0 : 1;
            if (p & 0xff00)
            {
                s += 8;
                p = p >> 8;
            }
            if (p & 0x00f0)
            {
                s += 4;
                p = p >> 4;
            }
            if (p & 0x000c)
            {
                s += 2;
                p = p >> 2;
            }
            if (p & 0x0002)
            {
                s += 1;
                // p = p >> 1;
            }
            p = (allocsize / (64 * 1024));
            return (1 << s) | p;
        }

        u64 allocsize_to_nodesize(u32 allocsize)
        {
            u32 p = (allocsize / (64 * 1024));
            u32 s = 1;
            if (p & 0xff00)
            {
                s += 8;
                p = p >> 8;
            }
            if (p & 0x00f0)
            {
                s += 4;
                p = p >> 4;
            }
            if (p & 0x000c)
            {
                s += 2;
                p = p >> 2;
            }
            if (p & 0x0002)
            {
                s += 1;
                // p = p >> 1;
            }
            s = (1 << s);

            u32 c = 1;
            if (s & 0xff00)
                c = 16;
            else if (s & 0x00f0)
                c = 8;
            else if (s & 0x000c)
                c = 4;
            else if (s & 0x0002)
                c = 2;
            u32 const n = 128 / c;

            return n * s * (64 * 1024);
        }

        u32 allocsize_to_info(u32 allocsize)
        {
            u32 p = (allocsize / (64 * 1024));
            u32 w;
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

            u32 b = 1;
            if (p & 0xff00)
            {
                b += 8;
                p = p >> 8;
            }
            if (p & 0x00f0)
            {
                b += 4;
                p = p >> 4;
            }
            if (p & 0x000c)
            {
                b += 2;
                p = p >> 2;
            }
            if (p & 0x0002)
            {
                b += 1;
                // p = p >> 1;
            }
            return (w << 8) | (b);
        }
    } // namespace xsegregatedstrat2
} // namespace xcore
