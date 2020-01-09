#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_binarysearch_tree.h"
#include "xvmem/private/x_strategy_coalesce.h"

namespace xcore
{
    using namespace xbst::index_based;

    struct naddr_t : public node_t
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

	    static inline u64 get_size_addr_key(u32 size, u32 addr) { return ((u64)size << 32) | (u64)addr; }
        inline u64 get_key() const { return get_size_addr_key(m_size, m_addr); }

        XCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    namespace bst_color
    {
        s32 get_color_node_f(const xbst::index_based::node_t* lhs)
        {
            naddr_t const* n = (naddr_t const*)(lhs);
            return n->is_color_red() ? xbst::COLOR_RED : xbst::COLOR_BLACK;
        }

        void set_color_node_f(xbst::index_based::node_t* lhs, s32 color)
        {
            naddr_t* n = (naddr_t*)(lhs);
            if (color == xbst::COLOR_BLACK)
                n->set_color_black();
            else
                n->set_color_red();
        }
    } // namespace bst_color

    namespace bst_addr
    {
        u64 get_key_node_f(const node_t* lhs)
        {
            naddr_t const* n = (naddr_t const*)(lhs);
            return (u64)n->m_addr;
        }

        s32 compare_node_f(const u64 pkey, const node_t* node)
        {
            naddr_t const* n    = (naddr_t const*)(node);
            u32            addr = (u32)pkey;
            if (addr < n->m_addr)
                return -1;
            if (addr > n->m_addr)
                return 1;
            return 0;
        }

        static tree_t    config;

    } // namespace bst_addr

    namespace bst_size
    {
        u64 get_key_node_f(const xbst::index_based::node_t* lhs)
        {
            naddr_t const* n = (naddr_t const*)(lhs);
            return (u64)n->m_size;
        }

        s32 compare_node_f(const u64 pkey, const xbst::index_based::node_t* node)
        {
            naddr_t const* n    = (naddr_t const*)(node);
            u32 const      size = (u32)(pkey >> 32);
			u32 const      addr = (u32)(pkey & 0xffffffff);
            if (size < n->m_size)
                return -1;
            if (size > n->m_size)
                return 1;
            if (addr < n->m_addr)
                return -1;
            if (addr > n->m_addr)
                return 1;
            return 0;
        }

		static tree_t    config;

    } // namespace bst_size

    static inline void* advance_ptr(void* ptr, u64 size) { return (void*)((uptr)ptr + size); }
    static inline void* align_ptr(void* ptr, u32 alignment) { return (void*)(((uptr)ptr + (alignment - 1)) & ~((uptr)alignment - 1)); }
    static uptr         diff_ptr(void* ptr, void* next_ptr) { return (size_t)((uptr)next_ptr - (uptr)ptr); }

    static u64 adjust_size_for_alignment(u64 requested_size, u32 requested_alignment, u32 default_alignment)
    {
        // Compute the 'size' that taking into account the alignment. This is done by taking
        // the 'external mem ptr' of the current node and the 'external mem ptr'
        // of the next node and substracting them.
        ASSERT(xispo2(default_alignment));
        ASSERT(xispo2(requested_alignment));

        u64 out_size = 0;

        // Verify the alignment
        if (requested_alignment > default_alignment)
        {
            u64 align_mask  = requested_alignment - 1;
            u64 align_shift = (default_alignment & align_mask);

            // How many bytes do we have to add to reach the requested size & alignment?
            align_shift = requested_alignment - align_shift;
            out_size    = requested_size + align_shift;
        }
        else
        {
            // The default alignment is already enough to satisfy the requested alignment
            out_size = requested_size;
        }
        return out_size;
    }

    void remove_from_addr_chain(xcoalescee& self, u32 inode, naddr_t* pnode);
    void add_to_addr_db(xcoalescee& self, u32 inode, naddr_t* pnode);
    bool pop_from_addr_db(xcoalescee& self, void* ptr, u32& inode, naddr_t*& pnode);

    void split_node(xcoalescee& self, u32 inode, naddr_t* pnode, u64 size);
    bool find_bestfit(xcoalescee& self, u64 size, u32 alignment, naddr_t*& out_pnode, u32& out_inode, u64& out_nodeSize);

    void add_to_size_db(xcoalescee& self, u32 inode, naddr_t* pnode);
    void remove_from_size_db(xcoalescee& self, u32 inode, naddr_t* pnode);

    inline void alloc_node(xcoalescee& self, u32& inode, naddr_t*& node)
    {
        node  = (naddr_t*)self.m_node_heap->allocate();
        inode = self.m_node_heap->ptr2idx(node);
    }

    inline naddr_t* idx2naddr(xcoalescee& self, u32 idx)
    {
        naddr_t* pnode = nullptr;
        if (idx != naddr_t::NIL)
            pnode = (naddr_t*)self.m_node_heap->idx2ptr(idx);
        return pnode;
    }

	inline void dealloc_node(xcoalescee& self, u32 inode, naddr_t* pnode)
    {
        ASSERT(self.m_node_heap->idx2ptr(inode) == pnode);
        self.m_node_heap->deallocate(pnode);
    }

    inline u32 calc_size_slot(xcoalescee& self, u64 size)
    {
        if (size > self.m_alloc_size_max)
            return self.m_size_db_cnt;
        ASSERT(size >= self.m_alloc_size_min);
        u32 const slot = (u32)((u64)(size - self.m_alloc_size_min) / self.m_alloc_size_step);
        ASSERT(slot < self.m_size_db_cnt);
        return slot;
    }

    xcoalescee::xcoalescee()
        : m_main_heap(nullptr)
        , m_node_heap(nullptr)
        , m_memory_addr(nullptr)
        , m_memory_size(0)
        , m_alloc_size_min(0)
        , m_alloc_size_max(0)
        , m_alloc_size_step(0)
        , m_size_db_cnt(0)
        , m_size_db(nullptr)
        , m_addr_db(0)
    {
    }

    void xcoalescee::initialize(xalloc* main_heap, xfsadexed* node_heap, void* mem_addr, u64 mem_size, u32 size_min, u32 size_max, u32 size_step)
    {
        m_main_heap       = main_heap;
        m_node_heap       = node_heap;
        m_memory_addr     = mem_addr;
        m_memory_size     = mem_size;
        m_alloc_size_min  = size_min;
        m_alloc_size_max  = size_max;
        m_alloc_size_step = size_step;

        m_size_db_cnt = 1 + ((m_alloc_size_max - m_alloc_size_min) / m_alloc_size_step);
        m_size_db     = (u32*)m_main_heap->allocate(m_size_db_cnt * sizeof(u32), sizeof(void*));
        for (u32 i = 0; i < m_size_db_cnt; i++)
        {
            m_size_db[i] = naddr_t::NIL;
        }
        // Which sizes are available in 'm_size_db' is known through this hierarchical set of bits.
        m_size_db_occupancy.init(m_main_heap, m_size_db_cnt);
        // The last db contains all sizes larger than m_alloc_size_max
        m_size_db_cnt -= 1;

        bst_size::config.m_get_key_f   = bst_size::get_key_node_f;
        bst_size::config.m_compare_f   = bst_size::compare_node_f;
        bst_size::config.m_get_color_f = bst_color::get_color_node_f;
        bst_size::config.m_set_color_f = bst_color::set_color_node_f;

        bst_addr::config.m_get_key_f   = bst_addr::get_key_node_f;
        bst_addr::config.m_compare_f   = bst_addr::compare_node_f;
        bst_addr::config.m_get_color_f = bst_color::get_color_node_f;
        bst_addr::config.m_set_color_f = bst_color::set_color_node_f;

        naddr_t* head_node = m_node_heap->construct<naddr_t>();
        naddr_t* tail_node = m_node_heap->construct<naddr_t>();
        head_node->init();
        head_node->set_used(true);
        head_node->set_locked();
        tail_node->init();
        tail_node->set_used(true);
        tail_node->set_locked();
        naddr_t*  main_node  = m_node_heap->construct<naddr_t>();
        u32 const main_inode = m_node_heap->ptr2idx(main_node);
        main_node->init();
        main_node->m_addr      = 0;
        main_node->m_size      = 0;
        main_node->m_prev_addr = m_node_heap->ptr2idx(head_node);
        main_node->m_next_addr = m_node_heap->ptr2idx(tail_node);
        head_node->m_next_addr = main_inode;
        tail_node->m_prev_addr = main_inode;
		main_node->set_addr(m_memory_addr, m_alloc_size_step, mem_addr);
		main_node->set_size(mem_size, m_alloc_size_step);
        add_to_size_db(*this, main_inode, main_node);
    }

    void xcoalescee::release()
    {
        u32 inode = 0;
        while (clear(m_addr_db, &bst_addr::config, m_node_heap, inode))
        {
            naddr_t* pnode = idx2naddr(*this, inode);
            dealloc_node(*this, inode, pnode);
        }
        for (u32 i = 0; i <= m_size_db_cnt; i++)
        {
            while (clear(m_size_db[i], &bst_size::config, m_node_heap, inode))
            {
                naddr_t* pnode = idx2naddr(*this, inode);
                dealloc_node(*this, inode, pnode);
            }
        }
		m_size_db_occupancy.release(m_main_heap);
    }

    void* xcoalescee::allocate(u32 _size, u32 _alignment)
    {
        // Align the size up with 'm_alloc_size_step'
        // Align the alignment up with 'm_alloc_size_step'
        u32 size      = xalignUp(_size, m_alloc_size_step);
        u32 alignment = xmax(_alignment, m_alloc_size_step);

        // Find the node in the size db that has the same or larger size
        naddr_t* pnode;
        u32      inode;
        u64      nodeSize;
        if (find_bestfit(*this, size, alignment, pnode, inode, nodeSize) == false)
            return nullptr;
        ASSERT(pnode != nullptr);

        // Remove 'node' from the size tree since it is not available/free anymore
        remove_from_size_db(*this, inode, pnode);

        void* ptr = pnode->get_addr(m_memory_addr, m_alloc_size_step);
        pnode->set_addr(m_memory_addr, m_alloc_size_step, align_ptr(ptr, alignment));

        split_node(*this, inode, pnode, size);

        // Mark our node as used
        pnode->set_used(true);

        // Insert our alloc node into the address tree so that we can find it when
        // deallocate is called.
        pnode->clear();
        insert(m_addr_db, &bst_addr::config, m_node_heap, pnode->get_key(), inode);

        // Done...
        return pnode->get_addr(m_memory_addr, m_alloc_size_step);
    }

    void xcoalescee::deallocate(void* p)
    {
        u32      inode_curr = naddr_t::NIL;
        naddr_t* pnode_curr = nullptr;
        if (!pop_from_addr_db(*this, p, inode_curr, pnode_curr))
        {
            // Could not find address in the addr_db
            return;
        }

        // Coalesce:
        //   Determine the 'prev' and 'next' of the current addr node
        //   If 'prev' is marked as 'Free' then coalesce (merge) with it
        //     and remove the size node that belonged to 'prev' from the size DB
        //   If 'next' is marked as 'Free' then coalesce (merge) with it
        //     and remove the size node that belonged to 'next' from the size DB
        //   Build the size node with the correct size and reference the 'merged' addr node
        //   Add the size node to the size DB
        //   Done

        u32      inode_prev = pnode_curr->m_prev_addr;
        u32      inode_next = pnode_curr->m_next_addr;
        naddr_t* pnode_prev = idx2naddr(*this, inode_prev);
        naddr_t* pnode_next = idx2naddr(*this, inode_next);

        if (pnode_prev->is_free() && pnode_next->is_free())
        {
            // prev and next are marked as 'free'.
            // - remove size of prev from size DB
            // - increase prev size with current and next size
            // - remove size of current and next from size DB
            // - remove from addr DB
            // - remove current and next addr from physical addr list
            // - add prev size back to size DB
            // - deallocate current and next
            remove_from_size_db(*this, inode_prev, pnode_prev);
            remove_from_size_db(*this, inode_next, pnode_next);

            pnode_prev->m_size += pnode_curr->m_size + pnode_next->m_size;
            add_to_size_db(*this, inode_prev, pnode_prev);
            remove_from_addr_chain(*this, inode_curr, pnode_curr);
            remove_from_addr_chain(*this, inode_next, pnode_next);
            dealloc_node(*this, inode_curr, pnode_curr);
            dealloc_node(*this, inode_next, pnode_next);
        }
        else if (!pnode_prev->is_free() && pnode_next->is_free())
        {
            // next is marked as 'free' (prev is 'used')
            // - remove next from size DB and physical addr list
            // - deallocate 'next'
            // - add the size of 'next' to 'current'
            // - add size to size DB
            remove_from_size_db(*this, inode_next, pnode_next);
            pnode_curr->m_size += pnode_next->m_size;
            add_to_size_db(*this, inode_curr, pnode_curr);
            remove_from_addr_chain(*this, inode_next, pnode_next);
            dealloc_node(*this, inode_next, pnode_next);
        }
        else if (pnode_prev->is_free() && !pnode_next->is_free())
        {
            // prev is marked as 'free'. (next is 'used')
            // - remove this addr/size node
            // - rem/add size node of 'prev', adding the size of 'current'
            // - deallocate 'current'
            remove_from_size_db(*this, inode_prev, pnode_prev);
            pnode_prev->m_size += pnode_curr->m_size;
            add_to_size_db(*this, inode_prev, pnode_prev);
            remove_from_addr_chain(*this, inode_curr, pnode_curr);
            dealloc_node(*this, inode_curr, pnode_curr);
        }
        else if (!pnode_prev->is_free() && !pnode_next->is_free())
        {
            // prev and next are marked as 'used'.
            // - add current to size DB
            add_to_size_db(*this, inode_curr, pnode_curr);
        }
    }

    void remove_from_addr_chain(xcoalescee& self, u32 idx, naddr_t* pnode)
    {
        naddr_t* pnode_prev     = idx2naddr(self, pnode->m_prev_addr);
        naddr_t* pnode_next     = idx2naddr(self, pnode->m_next_addr);
        pnode_prev->m_next_addr = pnode->m_next_addr;
        pnode_next->m_prev_addr = pnode->m_prev_addr;
        pnode->m_prev_addr      = naddr_t::NIL;
        pnode->m_next_addr      = naddr_t::NIL;
    }

    void add_to_addr_db(xcoalescee& self, u32 inode, naddr_t* pnode)
    {
        u64 const key = pnode->m_addr;
        insert(self.m_addr_db, &bst_addr::config, self.m_node_heap, key, inode);
    }

    bool pop_from_addr_db(xcoalescee& self, void* ptr, u32& inode, naddr_t*& pnode)
    {
        u64 key = (((u64)ptr - (u64)self.m_memory_addr) / self.m_alloc_size_step);
        if (find(self.m_addr_db, &bst_addr::config, self.m_node_heap, key, inode))
        {
            pnode = idx2naddr(self, inode);
            remove(self.m_addr_db, &bst_addr::config, self.m_node_heap,  inode);
            return true;
        }
        return false;
    }

    void add_to_size_db(xcoalescee& self, u32 inode, naddr_t* pnode)
    {
        u64 const key          = pnode->m_addr;
        u64 const size         = pnode->get_size(self.m_alloc_size_step);
        u32 const size_db_slot = calc_size_slot(self, size);
        insert(self.m_size_db[size_db_slot], &bst_size::config, self.m_node_heap, key, inode);
        self.m_size_db_occupancy.set(size_db_slot);
    }

    void remove_from_size_db(xcoalescee& self, u32 inode, naddr_t* pnode)
    {
        u64 const key          = pnode->m_addr;
        u64 const size         = pnode->get_size(self.m_alloc_size_step);
        u32 const size_db_slot = calc_size_slot(self, size);
        if (remove(self.m_size_db[size_db_slot], &bst_size::config, self.m_node_heap, inode))
        {
            if (self.m_size_db[size_db_slot] == naddr_t::NIL)
            {
                self.m_size_db_occupancy.clr(size_db_slot);
            }
        }
    }

    void split_node(xcoalescee& self, u32 inode_curr, naddr_t* pnode_curr, u64 size)
    {
        if ((pnode_curr->get_size(self.m_alloc_size_step) - size) > self.m_alloc_size_step)
        {
            // Construct new naddr node and link it into the physical address doubly linked list
            naddr_t* pnode_after = self.m_node_heap->construct<naddr_t>();
            u32      inode_after = self.m_node_heap->ptr2idx(pnode_after);

            pnode_after->init();
            pnode_after->m_size      = pnode_curr->m_size - (u32)(size / self.m_alloc_size_step);
            pnode_curr->m_size       = (u32)(size / self.m_alloc_size_step);
            pnode_after->m_prev_addr = inode_curr;
            pnode_after->m_next_addr = pnode_curr->m_next_addr;
            pnode_curr->m_next_addr  = inode_after;
            naddr_t* pnode_next      = idx2naddr(self, pnode_curr->m_next_addr);
            pnode_next->m_prev_addr  = inode_after;
            void* node_addr          = pnode_curr->get_addr(self.m_memory_addr, self.m_memory_size);
            void* rest_addr          = advance_ptr(node_addr, size);
            pnode_after->set_addr(self.m_memory_addr, self.m_memory_size, rest_addr);
        }
    }

    bool find_bestfit(xcoalescee& self, u64 size, u32 alignment, naddr_t*& out_pnode, u32& out_inode, u64& out_nodeSize)
    {
        if (self.m_size_db == 0)
            return false;

        // Adjust the size and compute size slot
        // - If the requested alignment is less-or-equal to m_alloc_size_step then we can align_up the size to m_alloc_size_step
        // - If the requested alignment is greater than m_alloc_size_step then we need to calculate the necessary size needed to
        //   include the alignment request
        u64 size_to_alloc = adjust_size_for_alignment(size, alignment, self.m_alloc_size_step);
        u32 size_db_slot  = calc_size_slot(self, size_to_alloc);
        u32 size_db_root  = self.m_size_db[size_db_slot];
        if (size_db_root == naddr_t::NIL)
        {
            // We need to find a size greater since current slot is empty
            u32 ilarger;
            if (self.m_size_db_occupancy.upper(size_db_slot, ilarger))
            {
                size_db_root = self.m_size_db[ilarger];
				ASSERT(size_db_root != naddr_t::NIL);
            }
        }
        if (size_db_root != naddr_t::NIL)
        {
            if (get_min(size_db_root, &bst_size::config, self.m_node_heap, out_inode))
            {
                out_pnode    = idx2naddr(self, out_inode);
                out_nodeSize = out_pnode->get_size(self.m_alloc_size_step);
                return true;
            }
        }
        return false;
    }

}; // namespace xcore
