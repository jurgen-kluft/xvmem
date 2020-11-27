
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
    struct BinMap
    {
        struct Config
        {
            Config(u8 l1_len, u8 l2_len, u32 count)
                : m_l1_len(l1_len)
                , m_l2_len(l2_len)
                , m_count(count)
            {
                ASSERT((m_count <= 32) || (m_count <= (m_l2_len * 16)));
                ASSERT((m_count <= 32) || ((m_l1_len * 16) <= m_l2_len));
            }

            u8  const m_l1_len;
            u8  const m_l2_len;
            u16 const m_count;
        };

        BinMap(u32 count)
            : m_l0(0)
            , m_free_index(0)
        {
        }

        inline u16* get_l1() const { return (u16*)this + (sizeof(BinMap) / 2); }
        inline u16* get_l2(Config const& cfg) const { return (u16*)(this + (sizeof(BinMap) / 2) + cfg.m_l1_len); }

        void Init(Config const& cfg);
        void Set(Config const& cfg, u32 bin);
        void Clr(Config const& cfg, u32 bin);
        bool Get(Config const& cfg, u32 bin) const;
        u32  Find(Config const& cfg) const;
 
        u32 m_l0;
        u16 m_free_index;
    };

    // Page(s) Commit/Decommit
    // There will be different types due to allocation sizes being smaller or being larger than the page-size.
    // Alloc Size:
    // <=    256, node_width = u16 (ref), granularity = 1
    //  >    256, node width =  u8 (ref), granularity = 1
    //  >=  64KB, node width =  u8 (ref), granularity = AllocSize/65536
    //  >= 512KB, node width = u16 (cnt), granularity = AllocSize/65536

    struct PageManagement
    {
        enum {
            COUNT_MASK = 0x0001,
            COUNT_REFS = 0x0000,
            COUNT_PAGES = 0x0001,
            NODE_BITS_MASK = 0x00F0,
            NODE_BITS_U8 = 0x0010,
            NODE_BITS_U16 = 0x0020,
        };

        u16   m_page_granularity;
        u16   m_flags;
        u32   m_num_nodes;
        void* m_nodes; // u8* or u16*

        void Allocate(u32 pageStart, u16 pages)
        {
            if ((m_flags&COUNT_MASK)==COUNT_PAGES)
            {
                // Set the page-count on the node
                u32 nodeIndex = pageStart / m_page_granularity;
                if ((m_flags & NODE_BITS_MASK) == NODE_BITS_U16)
                {
                    u16* nodes = (u16*)m_nodes;
                    nodes[nodeIndex] = pages;
                }
                else
                {
                    u8* nodes = (u8*)m_nodes;
                    ASSERT(pages < 256);
                    nodes[nodeIndex] = (u8)pages;
                }
            }
            else if ((m_flags&COUNT_MASK)==COUNT_REFS)
            {
                // Increase the ref count of that page
                u32 nodeHeadIndex = pageStart / m_page_granularity;
                u32 nodeTailIndex = (pageStart + pages) / m_page_granularity;
                if ((m_flags & NODE_BITS_MASK) == NODE_BITS_U16)
                {
                    u16* nodes = (u16*)m_nodes;
                    nodes[nodeHeadIndex] += 1;
                    nodes[nodeTailIndex] += 1;
                }
                else
                {
                    u8* nodes = (u8*)m_nodes;
                    nodes[nodeHeadIndex] += 1;
                    nodes[nodeTailIndex] += 1;
                }
            }
            else
            {
                ASSERT(false);
            }
        }

        void Deallocate(u32 page_start, u32 pages)
        {
            // Decrease the ref count of that page
        }
    };

    struct Alloc
    {
        struct Chunk
        {
            u16 m_elem_used;
            u16 m_elem_size;
        };

        void*             m_memory_base;
        u64               m_memory_range;
        u64               m_chunk_size;
        u32               m_chunks_cnt;
        Chunk*            m_chunks;
        BinMap**          m_binmaps;
        xalist_t::node_t* m_chunk_listnodes;
        xalist_t          m_free_chunk_list;
        xalist_t::index   m_used_chunk_list_per_size[32];
    };

    void Initialize(Alloc::Chunk& c)
    {
        c.m_elem_used = 0;
        c.m_elem_size = 0;
    }

    void Initialize(Alloc& a, xvmem* vmem)
    {
        u32 page_size;
        vmem->reserve(a.m_memory_range, page_size, 0, a.m_memory_base);

        for (s32 i = 0; i < a.m_chunks_cnt; ++i)
        {
            Initialize(a.m_chunks[i]);
            a.m_binmaps[i] = nullptr;
        }
        a.m_free_chunk_list.initialize(&a.m_chunk_listnodes[0], a.m_chunks_cnt, a.m_chunks_cnt);
        for (s32 i = 0; i < 32; i++)
        {
            a.m_used_chunk_list_per_size[i] = xalist_t::NIL;
        }
    }

    void* Allocate(Alloc& region, u32 size, u32 bin)
    {
        // Get the page from 'm_used_chunk_list_per_size[bin]'
        //   If it is NIL then take one from 'm_free_chunk_list_cache'
        //   If it is NIL then take one from 'm_free_chunk_list'
        //   Add it to 'm_used_chunk_list_per_size[bin]'

        // Pop an element from the page, if page is empty set 'm_used_chunk_list_per_size[bin]' to NIL

        return nullptr;
    }

    u32 Deallocate(Alloc& region, void* ptr)
    {
        // Convert 'ptr' to page-index and elem-index
        // Convert m_chunks[page-index].m_elem_size to bin-index
        // Copy m_elem_size from chunk to return it from this function
        // Push the now free element to the page, if page was full then add it to 'm_used_chunk_list_per_size[bin]'
        // If page is now totally empty add it to the 'm_free_chunk_list_cache'
        //  If 'm_free_chunk_list_cache' is full then decommit the page add it to the 'm_free_chunk_list'

        return 0;
    }

    void ResetArray(u32 count, u32 len, u16* data)
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
    void BinMap::Init(Config const& cfg)
    {
        // Set those bits that we never touch to '1' the rest to '0'
        u16 l0len = cfg.m_count;
        if (cfg.m_count > 32)
        {
            u16* const l2 = get_l2(cfg);
            ResetArray(cfg.m_count, cfg.m_l2_len, l2);
            u16* const l1 = get_l1();
            ResetArray(cfg.m_l2_len, cfg.m_l1_len, l1);
            l0len = cfg.m_l1_len;
        }
        m_l0 = 0xffffffff << (l0len & (32 - 1));
    }

    void BinMap::Set(Config const& cfg, u32 k)
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

    void BinMap::Clr(Config const& cfg, u32 k)
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

    bool BinMap::Get(Config const& cfg, u32 k) const
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

    u32 BinMap::Find(Config const& cfg) const
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
