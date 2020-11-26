
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
        u8  m_l1_len;
        u8  m_l2_len;
        u16 m_count;
        u32 m_l0;

        BinMap(u32 count)
            : m_l1_len(0)
            , m_l2_len(0)
            , m_count(count)
            , m_l0(0)
        {
        }

        inline u16* get_l1() const { return (u16*)this + 2; }
        inline u16* get_l2() const { return (u16*)(this + 2 + m_l1_len); }

        void Init();
        void Set(u32 bin);
        void Clr(u32 bin);
        bool Get(u32 bin) const;
        u32  Find() const;
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

    void BinMap::Init()
    {
        u16* const l1 = get_l1();
        u16* const l2 = get_l2();

        // Set those bits that we never touch to '1' the rest to '0'
        ASSERT((m_count <= 32) || (m_count <= (m_l2_len * 16)));
        for (u32 i = 0; i < m_l2_len; i++)
            l2[i] = 0;
        for (u32 i = 0; i < m_l1_len; i++)
            l1[i] = 0;
        if (m_count > 32)
        {
            {
                u32 wi2 = m_count / 16;
                u32 wd2 = 0xffff << (m_count & (16 - 1));
                for (; wi2 < m_l2_len; wi2++)
                {
                    l2[wi2] = wd2;
                    wd2     = 0xffff;
                }
            }
            {
                u32 wi1 = m_l2_len / 16;
                u32 wd1 = 0xffff << (m_l2_len & (16 - 1));
                for (; wi1 < m_l1_len; wi1++)
                {
                    l1[wi1] = wd1;
                    wd1     = 0xffff;
                }
            }
            m_l0 = 0xffffffff << (m_l1_len & (32 - 1));
        }
        else
        {
            m_l0 = 0xffffffff << (m_count & (32 - 1));
        }
    }

    void BinMap::Set(u32 bin)
    {
        if (m_count <= 32)
        {
            u32 const bi0 = 1 << (bin & (32 - 1));
            u32 const wd0 = m_l0 | bi0;
            m_l0          = wd0;
        }
        else
        {
            u16* const l2  = get_l2();
            u32 const  wi2 = bin / 16;
            u16 const  bi2 = (u16)1 << (bin & (16 - 1));
            u16 const  wd2 = l2[wi2] | bi2;
            if (wd2 == 0xffff)
            {
                u16* const l1 = get_l1();

                u32 const wi1 = wi2 / 16;
                u16 const bi1 = 1 << (wi1 & (16 - 1));
                u16 const wd1 = l1[wi1] | bi1;
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

    void BinMap::Clr(u32 bin)
    {
        if (m_count <= 32)
        {
            u32 const bi0 = 1 << (bin & (32 - 1));
            u32 const wd0 = m_l0 & ~bi0;
            m_l0          = wd0;
        }
        else
        {
            u16* const l2  = get_l2();
            u32 const  wi2 = bin / 16;
            u16 const  bi2 = (u16)1 << (bin & (16 - 1));
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

    bool BinMap::Get(u32 bin) const
    {
        if (m_count <= 32)
        {
            u32 const bi0 = 1 << (bin & (32 - 1));
            return (m_l0 & bi0) != 0;
        }
        else
        {
            u16* const l2  = get_l2();
            u32 const  wi2 = bin / 16;
            u16 const  bi2 = (u16)1 << (bin & (16 - 1));
            u16 const  wd2 = l2[wi2];
            return (wd2 & bi2) != 0;
        }
    }

    u32 BinMap::Find() const
    {
        if (m_count <= 32)
        {
            u32 const bi0 = (u32)xfindFirstBit(~m_l0);
            return bi0;
        }
        else
        {
            u32 const  bi0 = (u32)xfindFirstBit(~m_l0);
            u32 const  wi1 = bi0 * 16;
            u16* const l1  = get_l1();
            u32 const  bi1 = (u32)xfindFirstBit((u16)~l1[wi1]);
            u32 const  wi2 = bi1 * 16;
            u16* const l2  = get_l2();
            u32 const  bi2 = (u32)xfindFirstBit((u16)~l2[wi2]);
            return bi2 + wi2 * 16;
        }
    }
} // namespace xcore
