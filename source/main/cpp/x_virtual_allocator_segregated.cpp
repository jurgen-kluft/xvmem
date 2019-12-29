#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/x_virtual_allocator.h"
#include "xvmem/x_virtual_pages.h"
#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_bst.h"

namespace xcore
{
    inline u64 get_size_addr_key(u32 size, u32 addr) { return ((u64)size << 32) | (u64)addr; }

    struct nblock_t : public xbst::index_based::node_t
    {
        static u32 const NIL            = 0xffffffff;
        static u32 const FLAG_COLOR_RED = 0x10000000;
        static u32 const FLAG_FREE      = 0x20000000;
        static u32 const FLAG_USED      = 0x40000000;
        static u32 const FLAG_LOCKED    = 0x80000000;
        static u32 const FLAG_MASK      = 0xF0000000;

        u32 m_addr;      // addr = base_addr(m_addr * size_step)
        u16 m_flags;     // [Allocated, Free, Locked, Color]
        u16 m_level;     // Level index (size)
        u32 m_size;      // Number of bytes of this block
        u32 m_pages;     // Number of pages committed
        u32 m_prev_addr; // previous node in memory, can be free, can be allocated
        u32 m_next_addr; // next node in memory, can be free, can be allocated

        void init()
        {
            clear();
            m_addr      = 0;
            m_flags     = 0;
            m_level     = 0;
            m_size      = 0;
            m_pages     = 0;
            m_prev_addr = NIL;
            m_next_addr = NIL;
        }

        inline void* get_addr(void* baseaddr, u64 size_step) const { return (void*)((u64)baseaddr + ((u64)m_addr * size_step)); }
        inline void  set_addr(void* baseaddr, u64 size_step, void* addr) { m_addr = (u32)(((u64)addr - (u64)baseaddr) / size_step); }
        inline void  set_level_idx(u16 level_index) { m_level = level_index; }
        inline u32   get_level_idx() const { return (u32)m_level; }
        inline u64   get_page_cnt(u64 page_size) const { return (u64)m_pages * page_size; }
        inline void  set_page_cnt(u64 size, u32 page_size) { m_pages = (u32)(size / page_size); }
        inline u64   get_size(u32 size_step) const { return m_size * size_step; }
        inline void  set_size(u64 size, u32 size_step) { m_size = (u32)(size / size_step); }

        inline void set_locked() { m_flags = m_flags | FLAG_LOCKED; }
        inline void set_used(bool used) { m_flags = m_flags | FLAG_USED; }
        inline bool is_free() const { return (m_flags & FLAG_MASK) == FLAG_FREE; }
        inline bool is_locked() const { return (m_flags & FLAG_MASK) == FLAG_LOCKED; }

        inline void set_color_red() { m_flags = m_flags | FLAG_COLOR_RED; }
        inline void set_color_black() { m_flags = m_flags & ~FLAG_COLOR_RED; }
        inline bool is_color_red() const { return (m_flags & FLAG_COLOR_RED) == FLAG_COLOR_RED; }
        inline bool is_color_black() const { return (m_flags & FLAG_COLOR_RED) == 0; }

        inline u64 get_key() const { return get_size_addr_key(m_size, m_addr); }

        XCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    using namespace xbst::index_based;

    namespace bst_color
    {
        s32 get_color_node_f(const xbst::index_based::node_t* lhs)
        {
            nblock_t const* n = (nblock_t const*)(lhs);
            return n->is_color_red() ? xbst::COLOR_RED : xbst::COLOR_BLACK;
        }

        void set_color_node_f(xbst::index_based::node_t* lhs, s32 color)
        {
            nblock_t* n = (nblock_t*)(lhs);
            if (color == xbst::COLOR_BLACK)
                n->set_color_black();
            else
                n->set_color_red();
        }
    } // namespace bst_color

    namespace bst_addr
    {
        u64 get_key_node_f(const xbst::index_based::node_t* lhs)
        {
            nblock_t const* n = (nblock_t const*)(lhs);
            return (u64)n->m_addr;
        }

        s32 compare_node_f(const u64 pkey, const xbst::index_based::node_t* node)
        {
            nblock_t const* n    = (nblock_t const*)(node);
            u32             addr = (u32)pkey;
            if (addr < n->m_addr)
                return -1;
            if (addr > n->m_addr)
                return 1;
            return 0;
        }
    } // namespace bst_addr

    namespace bst_size
    {
        const void* get_key_node_f(const xbst::index_based::node_t* lhs)
        {
            nblock_t const* n   = (nblock_t const*)(lhs);
            u64 const       key = n->get_key();
            return (void*)key;
        }

        s32 compare_node_f(const u64 pkey, const xbst::index_based::node_t* node)
        {
            nblock_t const* n = (nblock_t const*)(node);

            u32 const size = (u32)(pkey >> 32);
            u32 const addr = (u32)(pkey & 0xffffffff);
            // First sort by size
            if (size < n->m_size)
                return -1;
            if (size > n->m_size)
                return 1;
            // Then sort by address
            if (addr < n->m_addr)
                return -1;
            if (addr > n->m_addr)
                return 1;

            return 0;
        }
    } // namespace bst_size

    struct xvmem_range_t
    {
        struct range_t
        {
            void reset()
            {
                m_allocs = 0;
                m_lvli   = 0xffff;
                m_next   = 0xffff;
                m_prev   = 0xffff;
            }
            bool is_unused() const { return m_allocs == 0; }
            void register_alloc() { m_allocs += 1; }
            void register_dealloc()
            {
                ASSERT(m_allocs > 0);
                m_allocs -= 1;
            }

            u16 m_allocs;
            u16 m_lvli;
            u16 m_next;
            u16 m_prev;
        };

        void*    m_mem_address;
        u64      m_range_size;  // The size of a range (e.g. 1 GiB)
        u32      m_range_count; // The full address range is divided into 'n' ranges
        u16      m_freelist;    // The doubly linked list of free ranges
        range_t* m_range;       // The array of memory ranges that make up the full address range

        void init(xalloc* main_heap, void* mem_address, u64 mem_range, u64 sub_range)
        {
            m_mem_address = mem_address;
            m_range_size  = sub_range;
            m_range_count = mem_range / sub_range;
            m_range       = (range_t*)main_heap->allocate(sizeof(u16) * m_range_count, sizeof(void*));
            for (s32 i = 0; i < m_range_count; i++)
            {
                m_range[i].reset();
            }
            for (s32 i = 1; i < m_range_count; i++)
            {
                u16      icurr = i - 1;
                u16      inext = i;
                range_t* pcurr = &m_range[icurr];
                range_t* pnext = &m_range[inext];
                pcurr->m_next  = inext;
                pnext->m_prev  = icurr;
            }
            m_freelist = 0;
        }

        void release(xalloc* main_heap)
        {
            m_mem_address = nullptr;
            m_range_size  = 0;
            m_range_count = 0;
            m_freelist    = 0xffff;
            main_heap->deallocate(m_range);
        }

        bool is_range_empty(void* addr) const
        {
            u32 const idx = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_range_size);
            return m_range[idx].is_unused();
        }

        inline u64 get_range_size() const { return m_range_size; }
        void*      get_range_addr(void* addr) const
        {
            u64 const idx = (u64)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_range_size);
            return m_mem_address + (idx * m_range_size);
        }

        bool obtain_range(void*& addr, u16 lvl_index)
        {
            // Do we still have some sub-ranges free ?
            if (m_freelist != 0xffff)
            {
                u32 const item  = m_freelist;
                range_t*  pcurr = &m_range[item];
                m_freelist      = pcurr->m_next;
                pcurr->m_lvli   = lvl_index;
                pcurr->m_next   = 0xffff;
                pcurr->m_prev   = 0xffff;
                addr            = (void*)((u64)m_mem_address + (m_range_size * item));
                return true;
            }
            return false;
        }

        void release_range(void*& addr)
        {
            // Put this sub-range back into the free-list !
            u32 const iitem = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_range_size);
            range_t*  pcurr = &m_range[m_freelist];
            pcurr->m_prev   = iitem;
            range_t* pitem  = &m_range[iitem];
            pitem->m_prev   = 0xffff;
            pitem->m_next   = m_freelist;
            pitem->m_lvli   = 0xffff;
            m_freelist      = iitem;
        }

        inline u16 ptr2lvl(void* ptr) const
        {
            u32 const idx = (u32)(((uptr)ptr - (uptr)m_mem_address) / (uptr)m_range_size);
            ASSERT(m_range[idx].m_lvli != 0xffff);
            return m_range[idx].m_lvli;
        }

        // We register allocations and de-allocations on every sub-range to know if
        // a sub-range is stil used. We are interested to know when a sub-range is free
        // so that we can release it back.
        void register_alloc(void* addr)
        {
            u32 const idx = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_range_size);
            m_range[idx].register_alloc();
        }

        // Returns the level index
        u16 register_dealloc(void* addr)
        {
            u32 const idx = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_range_size);
            ASSERT(m_range[idx].m_lvli != 0xffff);
            m_range[idx].register_dealloc();
            return m_range[idx].m_lvli;
        }
    };

    static inline void* advance_ptr(void* ptr, u64 size) { return (void*)((uptr)ptr + size); }
    static inline void* align_ptr(void* ptr, u32 alignment) { return (void*)(((uptr)ptr + (alignment - 1)) & ~((uptr)alignment - 1)); }
    static uptr         diff_ptr(void* ptr, void* next_ptr) { return (size_t)((uptr)next_ptr - (uptr)ptr); }

    // Segregated Allocator uses BST's for free blocks and allocated blocks.
    // Every level starts of with one free block that covers the memory range of that level.
    // So we can split nodes and we also coalesce.

    class xvmem_allocator_segregated : public xalloc
    {
    public:
        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);
        virtual void  release();

        struct level_t
        {
            void reset()
            {
                m_free_bst  = nblock_t::NIL;
                m_alloc_bst = nblock_t::NIL;
            }
            u32 m_free_bst;
            u32 m_alloc_bst;
        };

        void initialize(xalloc* main_alloc, xfsadexed* node_alloc, void* vmem_address, u64 vmem_range, u64 level_range, u32 allocsize_min, u32 allocsize_max, u32 allocsize_step, u32 pagesize);

        inline u32 ptr2addr(void* p) const { return (u32)(((u64)p - (u64)m_vmem_base_addr) / m_allocsize_step); }
        inline u64 lvl2size(u16 ilvl) const { return m_allocsize_min + (ilvl * m_allocsize_step); }

        void* allocate_from_level(u16 ilvl, u32 size)
        {
            // If 'free_bst' == NIL, retrieve an additional range from vrange_manager and add it to free_bst
            ASSERT(ilvl < m_level_cnt);
            level_t* plvl = &m_level[ilvl];
            if (plvl->m_free_bst == nblock_t::NIL)
            {
                void* range_base_addr;
                if (!m_vrange_manager.obtain_range(range_base_addr, ilvl))
                {
                    return nullptr;
                }

                // Create a new nblock_t for this range and add it
                nblock_t* pnode = (nblock_t*)m_node_alloc->allocate();
                u32       inode = m_node_alloc->ptr2idx(pnode);
                pnode->init();
                pnode->set_addr(m_vmem_base_addr, m_allocsize_step, range_base_addr);
                pnode->set_page_cnt(m_vrange_manager.m_range_size, m_pagesize);
                xbst::index_based::insert(plvl->m_free_bst, &m_free_bst_config, (void*)m_vrange_manager.m_range_size, inode);
            }

            // Take a node from the 'free' bst of this level
            u32 inode;
            xbst::index_based::find(plvl->m_free_bst, &m_free_bst_config, (void*)size, inode);
            nblock_t* pnode = (nblock_t*)m_node_alloc->idx2ptr(inode);

            u64 const node_size = pnode->get_size(m_allocsize_step);
            if (node_size >= lvl2size(ilvl))
            {
                u32 const inext = pnode->m_next_addr;
                nblock_t* pnext = (nblock_t*)m_node_alloc->idx2ptr(inext);

                // split the node and add it back into 'free'
                nblock_t* psplit    = (nblock_t*)m_node_alloc->allocate();
                u32       isplit    = m_node_alloc->ptr2idx(psplit);
                psplit->m_prev_addr = inode;
                psplit->m_next_addr = inext;
                pnext->m_prev_addr  = isplit;
                pnode->m_next_addr  = isplit;
                void* addr          = pnode->get_addr(m_vmem_base_addr, m_allocsize_step);
                addr                = advance_ptr(addr, size);
                psplit->set_addr(m_vmem_base_addr, m_allocsize_step, addr);
                psplit->set_size(node_size - size, m_pagesize);
                pnode->set_size(size, m_pagesize);

                u64 const key = (void*)psplit->get_key();
                xbst::index_based::insert(plvl->m_free_bst, &m_free_bst_config, key, isplit);
            }

            // Add this allocation into the 'alloc' bst for this level
            u64 const key = (void*)pnode->m_addr;
            xbst::index_based::insert(plvl->m_alloc_bst, &m_alloc_bst_config, key, inode);

            void* ptr = pnode->get_addr(m_vmem_base_addr, m_allocsize_step);
            return ptr;
        }

        inline nblock_t* idx2block(u32 const i)
        {
            if (i == 0xffffffff)
                return nullptr;
            else
                return (nblock_t*)m_node_alloc->idx2ptr(i);
        }

        void add_to_size_db(u32 inode, nblock_t* pnode)
        {
            u32 const ilvl = pnode->m_level;
            u64       key  = (((u64)pnode->m_size << 32) | (u64)(pnode->m_addr));
            xbst::index_based::insert(m_level[ilvl].m_free_bst, &m_free_bst_config, key, inode);
        }

        void remove_from_size_db(u32 inode, nblock_t* pnode)
        {
            u32 const ilvl = pnode->m_level;
            u64       key  = (((u64)pnode->m_size << 32) | (u64)(pnode->m_addr));
            xbst::index_based::insert(m_level[ilvl].m_free_bst, &m_free_bst_config, key, inode);
        }

        void add_to_addr_chain(u32 icurr, nblock_t* pcurr, u32 inode, nblock_t* pnode)
        {
            u32 const inext    = pcurr->m_next_addr;
            nblock_t* pnext    = idx2block(inext);
            pnext->m_prev_addr = inode;
            pcurr->m_next_addr = inode;
        }

        void remove_from_addr_chain(u32 inode, nblock_t* pnode)
        {
            u32 const iprev = pnode->m_prev_addr;
            u32 const inext = pnode->m_next_addr;
            nblock_t* pprev = idx2block(iprev);
            nblock_t* pnext = idx2block(inext);
            if (pprev != nullptr)
            {
                pprev->m_next_addr = inext;
            }
            if (pnext != nullptr)
            {
                pnext->m_prev_addr = iprev;
            }
        }

        void dealloc_node(u32 inode, nblock_t* pnode)
        {
            ASSERT(m_node_alloc->idx2ptr(inode) == pnode);
            m_node_alloc->deallocate(pnode);
        }

        void deallocate_from_level(u32 ilvl, void* ptr)
        {
            level_t* plvl = &m_level[ilvl];
            u64      key  = ptr2addr(ptr);
            u32      inode_curr;
            if (xbst::index_based::find(plvl->m_alloc_bst, &m_alloc_bst_config, key, inode_curr))
            {
                // This node is now 'free', can we coalesce with previous/next ?
                // Coalesce (but do not add the node back to 'free' yet)
                // Is the associated range now free ?, if so release it back
                // If the associated range is not free we can add the node back to 'free'
                nblock_t* pnode_curr = idx2block(inode_curr);
                u32 const inode_prev = pnode_curr->m_prev_addr;
                u32 const inode_next = pnode_curr->m_next_addr;
                nblock_t* pnode_prev = idx2block(inode_prev);
                nblock_t* pnode_next = idx2block(inode_next);

                if ((pnode_prev != nullptr && pnode_prev->is_free()) && (pnode_next != nullptr && pnode_next->is_free()))
                {
                    // prev and next are marked as 'free'.
                    // - remove size of prev from size DB
                    // - increase prev size with current and next size
                    // - remove size of current and next from size DB
                    // - remove from addr DB
                    // - remove current and next addr from physical addr list
                    // - add prev size back to size DB
                    // - deallocate current and next
                    remove_from_size_db(inode_prev, pnode_prev);
                    remove_from_size_db(inode_next, pnode_next);

                    pnode_prev->m_size += pnode_curr->m_size + pnode_next->m_size;
                    add_to_size_db(inode_prev, pnode_prev);
                    remove_from_addr_chain(inode_curr, pnode_curr);
                    remove_from_addr_chain(inode_next, pnode_next);
                    dealloc_node(inode_curr, pnode_curr);
                    dealloc_node(inode_next, pnode_next);
                }
                else if ((pnode_prev == nullptr || !pnode_prev->is_free()) && (pnode_next != nullptr && pnode_next->is_free()))
                {
                    // next is marked as 'free' (prev is 'used')
                    // - remove next from size DB and physical addr list
                    // - deallocate 'next'
                    // - add the size of 'next' to 'current'
                    // - add size to size DB
                    remove_from_size_db(inode_next, pnode_next);
                    pnode_curr->m_size += pnode_next->m_size;
                    add_to_size_db(inode_curr, pnode_curr);
                    remove_from_addr_chain(inode_next, pnode_next);
                    dealloc_node(inode_next, pnode_next);
                }
                else if ((pnode_prev != nullptr && pnode_prev->is_free()) && (pnode_next == nullptr || !pnode_next->is_free()))
                {
                    // prev is marked as 'free'. (next is 'used')
                    // - remove this addr/size node
                    // - rem/add size node of 'prev', adding the size of 'current'
                    // - deallocate 'current'
                    remove_from_size_db(inode_prev, pnode_prev);
                    pnode_prev->m_size += pnode_curr->m_size;
                    add_to_size_db(inode_prev, pnode_prev);
                    remove_from_addr_chain(inode_curr, pnode_curr);
                    dealloc_node(inode_curr, pnode_curr);
                }
                else if ((pnode_prev == nullptr || !pnode_prev->is_free()) && (pnode_next == nullptr || !pnode_next->is_free()))
                {
                    // prev and next are marked as 'used'.
                    // - add current to size DB
                    add_to_size_db(inode_curr, pnode_curr);
                }
            }

            if (m_vrange_manager.is_range_empty(ptr))
            {
                // TODO: Release range
                // There is still a nblock_t in the free BST for this range, we need
                // to remove it. We know the size and addr.
                u32 range_size = (u32)(m_vrange_manager.get_range_size() / m_allocsize_step);
                u32 range_addr = (u32)((u64)m_vrange_manager.get_range_addr(ptr) / m_allocsize_step);
                u64 key        = get_size_addr_key(range_size, range_addr);
                u32 inode;
                xbst::index_based::remove(plvl->m_free_bst, &m_free_bst_config, (void*)key, inode);
                nblock_t* pnode = idx2block(inode);
                remove_from_addr_chain(inode, pnode);
                dealloc_node(inode, pnode);
            }
        }

        xalloc*                   m_main_alloc;
        xfsadexed*                m_node_alloc;
        void*                     m_vmem_base_addr;
        u32                       m_allocsize_min;
        u32                       m_allocsize_max;
        u32                       m_allocsize_step;
        u32                       m_pagesize;
        u32                       m_level_cnt;
        level_t*                  m_level;
        xbst::index_based::tree_t m_free_bst_config;
        xbst::index_based::tree_t m_alloc_bst_config;
        xvmem_range_t             m_vrange_manager;
    };

    void xvmem_allocator_segregated::initialize(xalloc* main_alloc, xfsadexed* node_alloc, void* vmem_address, u64 vmem_range, u64 level_range, u32 allocsize_min, u32 allocsize_max, u32 allocsize_step, u32 pagesize)
    {
        m_main_alloc     = main_alloc;
        m_node_alloc     = node_alloc;
        m_vmem_base_addr = vmem_address;

        m_allocsize_min  = allocsize_min;
        m_allocsize_max  = allocsize_max;
        m_allocsize_step = allocsize_step;
        m_pagesize       = pagesize;
        m_level_cnt      = (m_allocsize_max - m_allocsize_min) / m_allocsize_step;
        m_level          = (level_t*)m_main_alloc->allocate(sizeof(level_t) * m_level_cnt, sizeof(void*));
        for (s32 i = 0; i < m_level_cnt; ++i)
        {
            m_level[i].reset();
        }

        m_free_bst_config.m_get_key_f   = bst_size::get_key_node_f;
        m_free_bst_config.m_compare_f   = bst_size::compare_node_f;
        m_free_bst_config.m_get_color_f = bst_color::get_color_node_f;
        m_free_bst_config.m_set_color_f = bst_color::set_color_node_f;

        m_alloc_bst_config.m_get_key_f   = bst_addr::get_key_node_f;
        m_alloc_bst_config.m_compare_f   = bst_addr::compare_node_f;
        m_alloc_bst_config.m_get_color_f = bst_color::get_color_node_f;
        m_alloc_bst_config.m_set_color_f = bst_color::set_color_node_f;

        // Initialize the vmem address range manager
        m_vrange_manager.init(main_alloc, m_vmem_base_addr, vmem_range, level_range);
    }

    void* xvmem_allocator_segregated::allocate(u32 size, u32 alignment)
    {
        ASSERT(size >= m_allocsize_min && size < m_allocsize_max);
        ASSERT(alignment <= m_pagesize);
        u32 const aligned_size = (size + m_allocsize_step - 1) & (~m_allocsize_step - 1);
        u32 const level_index  = (aligned_size - m_allocsize_min) / m_level_cnt;
        void*     ptr          = allocate_from_level(level_index, aligned_size);
        ASSERT(m_vrange_manager.ptr2lvl(ptr) == level_index);
        m_vrange_manager.register_alloc(ptr);
        return ptr;
    }

    void xvmem_allocator_segregated::deallocate(void* ptr)
    {
        u16 const ilvl = m_vrange_manager.register_dealloc(ptr);
        deallocate_from_level(ilvl, ptr);
    }

    void xvmem_allocator_segregated::release()
    {
        // de-allocate all nblock_t nodes at every level, for both alloc and free bst's
        // for every level decommit every committed memory block of any allocated nblock_t
        // release reserved virtual memory address range

        // release resources
        m_main_alloc->deallocate(m_level);
        m_vrange_manager.release(m_main_alloc);
    }

}; // namespace xcore
