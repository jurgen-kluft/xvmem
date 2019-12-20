#ifndef _X_ALLOCATOR_COALESCE_H_
#define _X_ALLOCATOR_COALESCE_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xbase/x_allocator.h"
#include "xbase/x_hibitset.h"
#include "xvmem/private/x_bst.h"

namespace xcore
{
    struct naddr_t : public xbst::index_based::node_t
    {
        static u32 const NIL            = 0xffffffff;
        static u32 const FLAG_COLOR_RED = 0x10000000;
        static u32 const FLAG_FREE      = 0x20000000;
        static u32 const FLAG_USED      = 0x40000000;
        static u32 const FLAG_LOCKED    = 0x80000000;
        static u32 const FLAG_MASK      = 0xF0000000;

        u32 m_addr;      // addr = base_addr(m_addr * size_step)
        u32 m_flags;     // [Allocated, Free, Locked, Color]
        u32 m_size;      // [m_size = (m_size & ~FLAG_MASK) * size_step]
        u32 m_prev_addr; // previous node in memory, can be free, can be allocated
        u32 m_next_addr; // next node in memory, can be free, can be allocated

        void init()
        {
            clear();
            m_addr      = 0;
            m_flags     = 0;
            m_size      = 0;
            m_prev_addr = naddr_t::NIL;
            m_next_addr = naddr_t::NIL;
        }

        inline void* get_addr(void* baseaddr, u64 size_step) const { return (void*)((u64)baseaddr + ((u64)m_addr * size_step)); }
        inline void  set_addr(void* baseaddr, u64 size_step, void* addr) { m_addr = (u32)(((u64)addr - (u64)baseaddr) / size_step); }
        inline u64   get_size(u64 size_step) const { return (u64)m_size * size_step; }
        inline void  set_size(u64 size, u64 size_step) { m_size = (u32)(size / size_step); }

        inline void set_locked() { m_flags = m_flags | FLAG_LOCKED; }
        inline void set_used(bool used) { m_flags = m_flags | FLAG_USED; }
        inline bool is_free() const { return (m_flags & FLAG_MASK) == FLAG_FREE; }
        inline bool is_locked() const { return (m_flags & FLAG_MASK) == FLAG_LOCKED; }

        inline void set_color_red() { m_flags = m_flags | FLAG_COLOR_RED; }
        inline void set_color_black() { m_flags = m_flags & ~FLAG_COLOR_RED; }
        inline bool is_color_red() const { return (m_flags & FLAG_COLOR_RED) == FLAG_COLOR_RED; }
        inline bool is_color_black() const { return (m_flags & FLAG_COLOR_RED) == 0; }

        XCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    class xcoalescee
    {
    public:
        xcoalescee();

        // Main API
        void* allocate(u32 size, u32 alignment);
        void  deallocate(void* p);

        // Coalesce related functions
        void initialize(xalloc* main_heap, xfsadexed* node_heap, void* mem_addr, u64 mem_size, u32 size_min, u32 size_max, u32 size_step);
        void release();

        void remove_from_addr_chain(u32 inode, naddr_t* pnode);
        void add_to_addr_db(u32 inode, naddr_t* pnode);
        bool pop_from_addr_db(void* ptr, u32& inode, naddr_t*& pnode);

        void split_node(u32 inode, naddr_t* pnode, u64 size);
        bool find_bestfit(u64 size, u32 alignment, naddr_t*& out_pnode, u32& out_inode, u64& out_nodeSize);

        void add_to_size_db(u32 inode, naddr_t* pnode);
        void remove_from_size_db(u32 inode, naddr_t* pnode);

        inline void alloc_node(u32& inode, naddr_t*& node)
        {
            node  = (naddr_t*)m_node_heap->allocate();
            inode = m_node_heap->ptr2idx(node);
        }

        inline void dealloc_node(u32 inode, naddr_t* pnode)
        {
            ASSERT(m_node_heap->idx2ptr(inode) == pnode);
            m_node_heap->deallocate(pnode);
        }

        inline naddr_t* idx2naddr(u32 idx)
        {
            naddr_t* pnode = nullptr;
            if (idx != naddr_t::NIL)
                pnode = (naddr_t*)m_node_heap->idx2ptr(idx);
            return pnode;
        }

        inline u32 calc_size_slot(u64 size) const
        {
            if (size > m_alloc_size_max)
                return m_size_db_cnt;
            ASSERT(size >= m_alloc_size_min);
            u32 const slot = (size - m_alloc_size_min) / m_alloc_size_step;
            ASSERT(slot < m_size_db_cnt);
            return slot;
        }

        xalloc*    m_main_heap;
        xfsadexed* m_node_heap;
        void*      m_memory_addr;
        u64        m_memory_size;

        u32       m_alloc_size_min;
        u32       m_alloc_size_max;
        u32       m_alloc_size_step;
        xbst::index_based::tree_t    m_size_db_config;
        u32       m_size_db_cnt;
        u32*      m_size_db;
        xhibitset m_size_db_occupancy;

        xbst::index_based::tree_t	m_addr_db_config;
        u32    m_addr_db;
    };
} // namespace xcore

#endif // _X_ALLOCATOR_COALESCE_H_