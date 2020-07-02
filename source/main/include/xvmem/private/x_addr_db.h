#ifndef _X_XVMEM_ADDR_DB_H_
#define _X_XVMEM_ADDR_DB_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xbase/x_allocator.h"

namespace xcore
{
    class xfsadexed;

    namespace xcoalescestrat_direct
    {
        class xsize_db;

        // NOTE: This node should be exactly 16 bytes.
        struct node_t
        {
            static u32 const NIL       = 0xffffffff;
            static u32 const FLAG_FREE = 0x0;
            static u32 const FLAG_USED = 0x80000000;
            static u32 const FLAG_MASK = 0x80000000;
            static u32 const ADDR_MASK = 0x7fffffff;

            u32 m_addr;      // (m_addr * size step) + base addr
            u32 m_size;      // [Free, Locked] + Size
            u32 m_prev_addr; // for linking the nodes by address
            u32 m_next_addr; //

            void init()
            {
                m_addr      = 0;
                m_size      = 0;
                m_prev_addr = node_t::NIL;
                m_next_addr = node_t::NIL;
            }

            XCORE_CLASS_PLACEMENT_NEW_DELETE

            inline u32  get_addr() const { return (u32)(m_addr & ADDR_MASK); }
            inline void set_addr(u32 addr) { m_addr = (m_addr & FLAG_MASK) | (addr & ADDR_MASK); }
            inline u32  get_addr_index(u32 const addr_range) const { return (m_addr & ADDR_MASK) / addr_range; }
            inline void set_size(u32 size, u8 size_index) { m_size = (size_index << 24) | (size >> 10); }
            inline u32  get_size() const { return (m_size & 0x00FFFFFF) << 10; }
            inline u32  get_size_index() const { return ((m_size >> 24) & 0x7F); }
            inline void set_used() { m_addr = m_addr | FLAG_USED; }
            inline void set_free() { m_addr = m_addr & ~FLAG_USED; }
            inline bool is_used() const { return (m_addr & FLAG_MASK) == FLAG_USED; }
            inline bool is_free() const { return (m_addr & FLAG_MASK) == 0; }
        };
        struct xsize_cfg
        {
            u8 size_to_index(u32 size) const
            {
                ASSERT(size > m_alloc_size_min);
                if (size >= m_alloc_size_max)
                    return (u8)m_size_index_count - 1;
                u32 const slot = (size - m_alloc_size_step - m_alloc_size_min) / m_alloc_size_step;
                ASSERT(slot < m_size_index_count);
                return (u8)slot;
            }

            u32 align_size(u32 size) const
            {
                ASSERT(size <= m_alloc_size_max);
                if (size <= m_alloc_size_min)
                    size = m_alloc_size_min + 1;
                // align up
                size = (size + (m_alloc_size_step - 1)) & ~(m_alloc_size_step - 1);
                return size;
            }

            u32 m_size_index_count;
            u32 m_alloc_size_min;
            u32 m_alloc_size_max;
            u32 m_alloc_size_step;
        };

        class xaddr_db
        {
        public:
            void    reset();
            node_t* get_node_with_size_index(u32 const i, xdexer* dexer, u32 size_index);
            node_t* get_node_with_addr(u32 const i, xdexer* dexer, u32 addr);
            void    alloc(u32 inode, node_t* pnode, xfsadexed* node_alloc, xsize_db* size_db, xsize_cfg const& size_cfg);
            void    alloc_by_split(u32 inode, node_t* pnode, u32 size, xfsadexed* node_alloc, xsize_db* size_db, xsize_cfg const& size_cfg);
            void    dealloc(u32 inode, node_t* pnode, bool merge_prev, bool merge_next, xsize_db* size_db, xsize_cfg const& size_cfg, xfsadexed* node_heap);

            void rescan_for_size_index(u32 const addr_index, u8 const size_index, xsize_db* size_db, xdexer* dexer);
            void remove_node(u32 inode, node_t* pnode, xdexer* dexer);
            bool has_size_index(u32 const i, xdexer* dexer, u32 size_index) const;
            u32  count_size_index(u32 const i, xdexer* dexer, u32 size_index) const;

            u32  m_addr_range; // The memory range of one addr node
            u32  m_addr_count; // The number of address nodes
            u32* m_nodes;
        };
    } // namespace xcoalescestrat_direct
} // namespace xcore

#endif // _X_XVMEM_ADDR_DB_H_