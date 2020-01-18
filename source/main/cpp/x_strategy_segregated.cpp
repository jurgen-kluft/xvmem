#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"
#include "xbase/x_hibitset.h"

#include "xvmem/x_virtual_allocator.h"
#include "xvmem/x_virtual_pages.h"
#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_binarysearch_tree.h"
#include "xvmem/private/x_strategy_segregated.h"

namespace xcore
{
    namespace xsegregated
    {
        static inline u64   get_size_addr_key(u32 size, u32 addr) { return ((u64)size << 32) | (u64)addr; }
        static inline void* advance_ptr(void* ptr, u64 size) { return (void*)((uptr)ptr + size); }

        // Memory Range is divided into Spaces
        //   A Space has an index to the Level that owns that Space
        //   A Level can own multiple spaces and are in a linked-list when not full
        //
        // Space:
        //   Can not be larger than 4 GB!
        //
        // Allocate:
        //   The size-request is transformed into the associated Level
        //   When the linked-list of spaces of that Level is empty we allocate a new space
        //   We take the linked list head node (space) and allocate from that space
        //
        // Deallocate:
        //   From the raw pointer we can compute the index of the space that it is part of.
        //   Knowing the space we can get the level index.
        //   Just call level->deallocate

        static u16 const NIL16 = 0xffff;
        static u32 const NIL32 = 0xffffffff;

        struct nnode_t : public xbst::index_based::node_t
        {
            static u32 const FLAG_COLOR_RED = 0x40000000;
            static u32 const FLAG_FREE      = 0x00000000;
            static u32 const FLAG_USED      = 0x80000000;
            static u32 const FLAG_MASK      = 0xC0000000;

            u32 m_addr;            // addr = base_addr(m_addr * size_step) + flags [Bit 31&30 = Used/Free, Color]
            u16 m_pages_range;     // Number of pages that this block holds
            u16 m_pages_committed; // Number of pages committed
            u32 m_prev_addr;       // previous node in memory, can be free, can be allocated (for coalesce)
            u32 m_next_addr;       // next node in memory, can be free, can be allocated (for coalesce)

            void init()
            {
                clear();
                m_addr            = 0;
                m_pages_range     = 0;
                m_pages_committed = 0;
                m_prev_addr       = NIL32;
                m_next_addr       = NIL32;
            }

            inline u32   get_addr() const { return m_addr & ~FLAG_MASK; }
            inline void* get_full_address(void* baseaddr, u64 page_size) const
            {
                u32 const addr = m_addr & ~FLAG_MASK;
                return (void*)((u64)baseaddr + ((u64)addr * page_size));
            }
            inline void set_addr_from_full_address(void* baseaddr, u64 page_size, void* addr)
            {
                u32 const flags = m_addr & FLAG_MASK;
                m_addr          = (u32)(((u64)addr - (u64)baseaddr) / page_size) | flags;
            }
            inline u32  get_page_cnt() const { return (u32)m_pages_range; }
            inline void set_page_cnt(u64 page_cnt) { m_pages_range = page_cnt; }
            inline u32  get_commited_page_cnt() const { return m_pages_committed; }
            inline void set_commited_page_cnt(u64 size, u32 size_step) { m_pages_committed = (u32)(size / size_step); }

            inline void set_used(bool used) { m_addr = m_addr | FLAG_USED; }
            inline bool is_used() const { return (m_addr & FLAG_USED) == FLAG_USED; }
            inline bool is_free() const { return (m_addr & FLAG_USED) == FLAG_FREE; }

            inline void set_color_red() { m_addr = m_addr | FLAG_COLOR_RED; }
            inline void set_color_black() { m_addr = m_addr & ~FLAG_COLOR_RED; }
            inline bool is_color_red() const { return (m_addr & FLAG_COLOR_RED) == FLAG_COLOR_RED; }
            inline bool is_color_black() const { return (m_addr & FLAG_COLOR_RED) == 0; }

            inline u64 get_addr_key() const { return m_addr & ~FLAG_MASK; }
            inline u64 get_size_key() const { return get_size_addr_key(m_pages_range, (m_addr & ~FLAG_MASK)); }

            XCORE_CLASS_PLACEMENT_NEW_DELETE
        };

        using namespace xbst::index_based;

        namespace bst_color
        {
            s32 get_color_node_f(const xbst::index_based::node_t* lhs)
            {
                nnode_t const* n = (nnode_t const*)(lhs);
                return n->is_color_red() ? xbst::COLOR_RED : xbst::COLOR_BLACK;
            }

            void set_color_node_f(xbst::index_based::node_t* lhs, s32 color)
            {
                nnode_t* n = (nnode_t*)(lhs);
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
                nnode_t const* n = (nnode_t const*)(lhs);
                return (u64)n->get_addr_key();
            }

            s32 compare_node_f(const u64 pkey, const xbst::index_based::node_t* node)
            {
                nnode_t const* n    = (nnode_t const*)(node);
                u32            addr = (u32)pkey;
                if (addr < n->get_addr_key())
                    return -1;
                if (addr > n->get_addr_key())
                    return 1;
                return 0;
            }

            static tree_t config;
        } // namespace bst_addr

        namespace bst_size
        {
            u64 get_key_node_f(const xbst::index_based::node_t* lhs)
            {
                nnode_t const* n   = (nnode_t const*)(lhs);
                u64 const      key = n->get_size_key();
                return key;
            }

            s32 compare_node_f(const u64 pkey, const xbst::index_based::node_t* node)
            {
                u32 const num_pages = (u32)(pkey >> 32);
                u32 const addr      = (u32)(pkey & 0xffffffff);

                // First sort by size
                nnode_t const* n = (nnode_t const*)(node);
                if (num_pages < n->m_pages_range)
                    return -1;
                if (num_pages > n->m_pages_range)
                    return 1;
                // Then sort by address
                if (addr < n->get_addr_key())
                    return -1;
                if (addr > n->get_addr_key())
                    return 1;
                return 0;
            }

            s32 find_node_f(const u64 pkey, const xbst::index_based::node_t* node)
            {
                u32 const      num_pages = (u32)(pkey >> 32);
                nnode_t const* n         = (nnode_t const*)(node);
                if (num_pages < n->m_pages_range)
                    return -1;
                if (num_pages >= n->m_pages_range)
                    return 0;
                return 0;
            }

            static tree_t config;
        } // namespace bst_size

        struct xspace_t
        {
            void reset()
            {
                m_free_bst  = NIL32;
                m_alloc_bst = NIL32;
                m_alloc_cnt = 0;
                m_level_idx = 0xffff;
                m_next      = 0xffff;
                m_prev      = 0xffff;
            }

            bool is_unused() const { return m_alloc_cnt == 0; }
            bool is_empty() const { return m_alloc_cnt == 0; }
            bool is_full() const { return m_free_bst == NIL32; }

            void register_alloc() { m_alloc_cnt += 1; }
            void register_dealloc()
            {
                ASSERT(m_alloc_cnt > 0);
                m_alloc_cnt -= 1;
            }

            void init(xfsadexed* node_alloc, void* vmem_base_addr, u64 allocsize_step, void* range_base_addr, u64 space_size, u32 page_size)
            {
                // Create a new nnode_t for this range and add it
                nnode_t* pnode = (nnode_t*)node_alloc->allocate();
                u32      inode = node_alloc->ptr2idx(pnode);
                pnode->init();
                pnode->set_addr_from_full_address(vmem_base_addr, allocsize_step, range_base_addr);
                pnode->set_page_cnt(space_size / page_size);
                u64 const key = get_size_addr_key(pnode->m_pages_range, pnode->get_addr());
                xbst::index_based::insert(m_free_bst, &bst_size::config, node_alloc, key, inode);
            }

            void add_to_size_db(u32 inode, nnode_t* pnode, xdexer* dexer)
            {
                u64 key = (((u64)pnode->m_pages_range << 32) | (u64)(pnode->get_addr()));
                xbst::index_based::insert(m_free_bst, &bst_size::config, dexer, key, inode);
            }

            void remove_from_size_db(u32 inode, nnode_t* pnode, xdexer* dexer)
            {
                u64 key = (((u64)pnode->m_pages_range << 32) | (u64)(pnode->get_addr()));
                xbst::index_based::insert(m_free_bst, &bst_size::config, dexer, key, inode);
            }

            void add_to_addr_chain(u32 icurr, nnode_t* pcurr, u32 inode, nnode_t* pnode, xdexer* dexer)
            {
                u32 const inext    = pcurr->m_next_addr;
                nnode_t*  pnext    = dexer->idx2obj<nnode_t>(inext);
                pnext->m_prev_addr = inode;
                pcurr->m_next_addr = inode;
            }

            void remove_from_addr_chain(u32 inode, nnode_t* pnode, xdexer* dexer)
            {
                u32 const iprev = pnode->m_prev_addr;
                u32 const inext = pnode->m_next_addr;
                nnode_t*  pprev = dexer->idx2obj<nnode_t>(iprev);
                nnode_t*  pnext = dexer->idx2obj<nnode_t>(inext);
                if (pprev != nullptr)
                {
                    pprev->m_next_addr = inext;
                }
                if (pnext != nullptr)
                {
                    pnext->m_prev_addr = iprev;
                }
            }

            void dealloc_node(u32 inode, nnode_t* pnode, xfsadexed* nodes)
            {
                ASSERT(nodes->idx2ptr(inode) == pnode);
                nodes->deallocate(pnode);
            }

            void deallocate(void* ptr, xfsadexed* nodes, xspaces_t* spaces)
            {
                u32 key = spaces->ptr2addr(ptr);

                u32 inode_curr;
                if (xbst::index_based::find(m_alloc_bst, &bst_addr::config, nodes, key, inode_curr))
                {
                    // This node is now 'free', can we coalesce with previous/next ?
                    // Coalesce (but do not add the node back to 'free' yet)
                    // Is the associated range now free ?, if so release it back
                    // If the associated range is not free we can add the node back to 'free'
                    nnode_t*  pnode_curr = nodes->idx2obj<nnode_t>(inode_curr);
                    u32 const inode_prev = pnode_curr->m_prev_addr;
                    u32 const inode_next = pnode_curr->m_next_addr;
                    nnode_t*  pnode_prev = nodes->idx2obj<nnode_t>(inode_prev);
                    nnode_t*  pnode_next = nodes->idx2obj<nnode_t>(inode_next);

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
                        remove_from_size_db(inode_prev, pnode_prev, nodes);
                        remove_from_size_db(inode_next, pnode_next, nodes);

                        pnode_prev->m_pages_range += pnode_curr->m_pages_range + pnode_next->m_pages_range;
                        add_to_size_db(inode_prev, pnode_prev, nodes);
                        remove_from_addr_chain(inode_curr, pnode_curr, nodes);
                        remove_from_addr_chain(inode_next, pnode_next, nodes);
                        dealloc_node(inode_curr, pnode_curr, nodes);
                        dealloc_node(inode_next, pnode_next, nodes);
                    }
                    else if ((pnode_prev == nullptr || !pnode_prev->is_free()) && (pnode_next != nullptr && pnode_next->is_free()))
                    {
                        // next is marked as 'free' (prev is 'used')
                        // - remove next from size DB and physical addr list
                        // - deallocate 'next'
                        // - add the size of 'next' to 'current'
                        // - add size to size DB
                        remove_from_size_db(inode_next, pnode_next, nodes);
                        pnode_curr->m_pages_range += pnode_next->m_pages_range;
                        add_to_size_db(inode_curr, pnode_curr, nodes);
                        remove_from_addr_chain(inode_next, pnode_next, nodes);
                        dealloc_node(inode_next, pnode_next, nodes);
                    }
                    else if ((pnode_prev != nullptr && pnode_prev->is_free()) && (pnode_next == nullptr || !pnode_next->is_free()))
                    {
                        // prev is marked as 'free'. (next is 'used')
                        // - remove this addr/size node
                        // - rem/add size node of 'prev', adding the size of 'current'
                        // - deallocate 'current'
                        remove_from_size_db(inode_prev, pnode_prev, nodes);
                        pnode_prev->m_pages_range += pnode_curr->m_pages_range;
                        add_to_size_db(inode_prev, pnode_prev, nodes);
                        remove_from_addr_chain(inode_curr, pnode_curr, nodes);
                        dealloc_node(inode_curr, pnode_curr, nodes);
                    }
                    else if ((pnode_prev == nullptr || !pnode_prev->is_free()) && (pnode_next == nullptr || !pnode_next->is_free()))
                    {
                        // prev and next are marked as 'used'.
                        // - add current to size DB
                        add_to_size_db(inode_curr, pnode_curr, nodes);
                    }
                }
            }

            u32 m_free_bst;  // The binary search tree that tracks free space
            u32 m_alloc_bst; // The binary search tree that tracks allocations
            u16 m_alloc_cnt; // The number of allocations that are active
            u16 m_level_idx; // Which level is using us
            u16 m_next;      // Used by level to keep track of spaces that are not full
            u16 m_prev;
        };

        void xspaces_t::init(xalloc* main_heap, void* mem_address, u64 mem_range, u64 space_size, u32 page_size)
        {
            m_mem_address = mem_address;
            m_page_size   = page_size;
            m_space_size  = space_size;
            m_space_count = (u32)(mem_range / space_size);
            m_spaces      = (xspace_t*)main_heap->allocate(sizeof(u16) * m_space_count, sizeof(void*));
            for (u32 i = 0; i < m_space_count; i++)
            {
                m_spaces[i].reset();
            }
            for (u32 i = 1; i < m_space_count; i++)
            {
                u16       icurr = i - 1;
                u16       inext = i;
                xspace_t* pcurr = &m_spaces[icurr];
                xspace_t* pnext = &m_spaces[inext];
                pcurr->m_next   = inext;
                pnext->m_prev   = icurr;
            }
            m_freelist    = 0;
            m_space_dexer = xdexed_array(m_spaces, sizeof(xspace_t), m_space_count);
        }

        void xspaces_t::release(xalloc* main_heap)
        {
            m_mem_address = nullptr;
            m_space_size  = 0;
            m_space_count = 0;
            m_freelist    = 0xffff;
            main_heap->deallocate(m_spaces);
        }

        bool xspaces_t::is_space_empty(void* addr) const
        {
            u32 const idx = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            return m_spaces[idx].is_unused();
        }

        bool xspaces_t::is_space_full(void* addr) const
        {
            u32 const idx = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            return m_spaces[idx].is_full();
        }

        void* xspaces_t::get_space_addr(void* addr) const
        {
            u64 const space_idx = (u64)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            ASSERT(space_idx < m_space_count);
            return advance_ptr(m_mem_address, (space_idx * m_space_size));
        }

        bool xspaces_t::obtain_space(void*& addr, u16& ispace, xspace_t*& pspace)
        {
            // Do we still have some free spaces ?
            if (m_freelist != 0xffff)
            {
                u16 const item  = m_freelist;
                xspace_t* pcurr = &m_spaces[item];
                remove_space_from_list(m_freelist, m_freelist);
                m_freelist         = pcurr->m_next;
                pcurr->m_level_idx = 0xffff;
                pcurr->m_next      = 0xffff;
                pcurr->m_prev      = 0xffff;
                addr               = (void*)((u64)m_mem_address + (m_space_size * item));
                return true;
            }
            return false;
        }

        void xspaces_t::insert_space_into_list(u16& head, u16 item)
        {
            xspace_t* pcurr = m_space_dexer.idx2obj<xspace_t>(head);
            pcurr->m_prev   = item;
            xspace_t* pitem = m_space_dexer.idx2obj<xspace_t>(item);
            pitem->m_prev   = 0xffff;
            pitem->m_next   = head;
            head            = item;
        }

        xspace_t* xspaces_t::remove_space_from_list(u16& head, u16 item)
        {
            xspace_t* pitem = m_space_dexer.idx2obj<xspace_t>(item);
            xspace_t* pprev = m_space_dexer.idx2obj<xspace_t>(pitem->m_prev);
            xspace_t* pnext = m_space_dexer.idx2obj<xspace_t>(pitem->m_next);
            if (pprev != nullptr)
                pprev->m_next = pitem->m_next;
            if (pnext != nullptr)
                pnext->m_prev = pitem->m_prev;
            if (head == item)
                head = pitem->m_next;
            return pitem;
        }

        void xspaces_t::release_space(void*& addr)
        {
            // Put this space back into the free-list !
            u32 const iitem    = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            xspace_t* pitem    = &m_spaces[iitem];
            pitem->m_level_idx = 0xffff;
            insert_space_into_list(m_freelist, iitem);
        }

        void xspaces_t::release_space(u16 ispace, xspace_t* pspace)
        {
            // Put this space back into the free-list !
            pspace->m_level_idx = 0xffff;
            insert_space_into_list(m_freelist, ispace);
        }

        u16 xspaces_t::ptr2level(void* ptr) const
        {
            u16 const space_idx = (u16)(((uptr)ptr - (uptr)m_mem_address) / (uptr)m_space_size);
            ASSERT(m_spaces[space_idx].m_level_idx != 0xffff);
            return m_spaces[space_idx].m_level_idx;
        }

        u16 xspaces_t::ptr2space(void* addr) const
        {
            u16 const space_idx = (u16)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            return space_idx;
        }

        // We register allocations and de-allocations on every space to know if
        // it is stil used. We are interested to know when a space is free
        // so that we can release it back.
        void xspaces_t::register_alloc(void* addr)
        {
            u16 const space_idx = (u16)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            m_spaces[space_idx].register_alloc();
        }

        // Returns the level index
        u16 xspaces_t::register_dealloc(void* addr)
        {
            u16 const space_idx = (u16)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            m_spaces[space_idx].register_dealloc();
            return m_spaces[space_idx].m_level_idx;
        }

        void level_t::reset() { m_spaces_not_full = NIL16; }

        void* level_t::allocate(u32 size_in_pages, u32 lvl_size_in_pages, xspaces_t* spaces, xfsadexed* node_alloc, void* vmem_base_addr, u32 allocsize_step)
        {
            u16       ispace = m_spaces_not_full;
            xspace_t* pspace = nullptr;
            if (ispace == NIL16)
            {
                void* space_base_addr;
                if (!spaces->obtain_space(space_base_addr, ispace, pspace))
                {
                    return nullptr;
                }

                // Initialize the newly obtained space
                pspace->init(node_alloc, vmem_base_addr, allocsize_step, space_base_addr, spaces->get_space_size(), spaces->get_page_size());
                spaces->insert_space_into_list(m_spaces_not_full, ispace);
            }
            else
            {
                pspace = spaces->idx2space(ispace);
            }

            // Take a node from the 'free' bst of the space
            u32       inode;
            u64 const key = get_size_addr_key(size_in_pages, 0);
            xbst::index_based::find(pspace->m_free_bst, &bst_size::config, node_alloc, key, inode);
            nnode_t* pnode = (nnode_t*)node_alloc->idx2ptr(inode);

            u64 const size_in_bytes = size_in_pages * spaces->get_page_size();

            u64 const node_size_in_pages = pnode->get_page_cnt();
            if (node_size_in_pages >= lvl_size_in_pages)
            {
                u32 const inext = pnode->m_next_addr;
                nnode_t*  pnext = (nnode_t*)node_alloc->idx2ptr(inext);

                // split the node and add it back into 'free'
                nnode_t* psplit     = (nnode_t*)node_alloc->allocate();
                u32      isplit     = node_alloc->ptr2idx(psplit);
                psplit->m_prev_addr = inode;
                psplit->m_next_addr = inext;
                pnext->m_prev_addr  = isplit;
                pnode->m_next_addr  = isplit;
                void* addr          = pnode->get_full_address(vmem_base_addr, allocsize_step);
                addr                = advance_ptr(addr, size_in_bytes);
                psplit->set_addr_from_full_address(vmem_base_addr, allocsize_step, addr);
                psplit->set_page_cnt(node_size_in_pages - size_in_pages);
                pnode->set_page_cnt(size_in_pages);

                u64 const key = psplit->get_size_key();
                xbst::index_based::insert(pspace->m_free_bst, &bst_size::config, node_alloc, key, isplit);
            }

            // Add this allocation into the 'alloc' bst for this level
            xbst::index_based::insert(pspace->m_alloc_bst, &bst_addr::config, node_alloc, pnode->get_addr_key(), inode);

            // If this space is full then remove it from the linked list
            if (pspace->is_full())
            {
                spaces->remove_space_from_list(m_spaces_not_full, ispace);
            }

            void* ptr = pnode->get_full_address(vmem_base_addr, allocsize_step);
            return ptr;
        }

        void deallocate(void* ptr, xfsadexed* node_alloc, xspaces_t* spaces)
        {
            u16 const  ispace   = spaces->ptr2space(ptr);
            xspace_t*  pspace   = spaces->idx2space(ispace);
            bool const was_full = pspace->is_full();
            pspace->deallocate(ptr, node_alloc, spaces);
            bool const is_empty = pspace->is_empty();

            if (was_full)
            {
                spaces->insert_space_into_list(m_spaces_not_full, ispace);
            }
            else if (is_empty)
            {
                spaces->remove_space_from_list(m_spaces_not_full, ispace);
                spaces->release_space(ispace, pspace);
            }
        }

        struct xlevel_t
        {
            void  reset();
            void* allocate(u32 size_in_pages, u32 lvl_size_in_pages, xspaces_t* spaces, xfsadexed* node_alloc, void* vmem_base_addr, u32 allocsize_step);
            void  deallocate(void* ptr, xfsadexed* node_alloc, xspaces_t* spaces);
            u16   m_spaces_not_full; // Linked list of spaces that are not full and not empty
        };

        inline u64 lvl2size_in_pages(xlevels_t* self, u16 ilvl) { return (self->m_allocsize_min + (ilvl * self->m_allocsize_step) + (self->m_pagesize - 1)) / self->m_pagesize; }
        inline u16 ptr2lvl(xlevels_t* self, void* ptr)
        {
            u16 const level_index = self->m_spaces.ptr2level(ptr);
            ASSERT(level_index < self->m_level_cnt);
            return level_index;
        }

        void xlevels_t::initialize(xalloc* main_alloc, xfsadexed* node_alloc, void* vmem_address, u64 vmem_space, u64 space_size, u32 allocsize_min, u32 allocsize_max, u32 allocsize_step, u32 page_size)
        {
            m_main_alloc     = main_alloc;
            m_node_alloc     = node_alloc;
            m_vmem_base_addr = vmem_address;

            m_allocsize_min  = allocsize_min;
            m_allocsize_max  = allocsize_max;
            m_allocsize_step = allocsize_step;
            m_pagesize       = page_size;
            m_level_cnt      = (m_allocsize_max - m_allocsize_min) / m_allocsize_step;
            m_levels         = (level_t*)m_main_alloc->allocate(sizeof(level_t) * m_level_cnt, sizeof(void*));
            for (u32 i = 0; i < m_level_cnt; ++i)
            {
                m_levels[i].reset();
            }

            bst_size::config.m_get_key_f   = bst_size::get_key_node_f;
            bst_size::config.m_compare_f   = bst_size::compare_node_f;
            bst_size::config.m_get_color_f = bst_color::get_color_node_f;
            bst_size::config.m_set_color_f = bst_color::set_color_node_f;

            bst_addr::config.m_get_key_f   = bst_addr::get_key_node_f;
            bst_addr::config.m_compare_f   = bst_addr::compare_node_f;
            bst_addr::config.m_get_color_f = bst_color::get_color_node_f;
            bst_addr::config.m_set_color_f = bst_color::set_color_node_f;

            // Initialize the spaces manager
            m_spaces.init(main_alloc, m_vmem_base_addr, vmem_space, space_size, page_size);
        }

        void xlevels_t::release()
        {
            m_spaces.release(m_main_alloc);
            m_main_alloc->deallocate(m_levels);
            m_levels = nullptr;
        }

        void* xlevels_t::allocate(u32 size, u32 alignment)
        {
            ASSERT(size >= m_allocsize_min && size <= m_allocsize_max);
            ASSERT(alignment <= m_pagesize);
            u32 const aligned_size  = (size + (m_allocsize_step - 1)) & (~m_allocsize_step - 1);
            u32 const size_in_pages = (aligned_size + (m_pagesize - 1)) / m_pagesize;
            // TODO: I do not think the level index computation is correct
            u16 const ilevel = (aligned_size - m_allocsize_min) / m_level_cnt;
            level_t*  plevel = &m_levels[ilevel];
            void*     ptr    = plevel->allocate(size_in_pages, lvl2size_in_pages(ilevel), &m_spaces, m_node_alloc, m_vmem_base_addr, m_allocsize_step);
            ASSERT(ptr2lvl(ptr) == ilevel);
            m_spaces.register_alloc(ptr);
            return ptr;
        }

        void xlevels_t::deallocate(void* ptr)
        {
            u16 const ilevel = m_spaces.register_dealloc(ptr);
            level_t*  plevel = &m_levels[ilevel];
            plevel->deallocate(ptr, m_node_alloc, &m_spaces);
        }
    } // namespace xsegregated
} // namespace xcore