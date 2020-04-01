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
    namespace xsegregatedstrat
    {
        static inline u64   get_size_addr_key(u32 size, u32 addr) { return ((u64)size << 32) | (u64)addr; }
        static inline void* advance_ptr(void* ptr, u64 size) { return (void*)((uptr)ptr + size); }

        static u16 const NIL16 = 0xffff;
        static u32 const NIL32 = 0xffffffff;

        // Node to link memory blocks in a doubly linked list as well as a binary search tree node for
        // managing it either in the ``allocation`` or ``size`` tree.
        struct nnode_t : public xbst::index_based::node_t
        {
            static u32 const FLAG_COLOR_RED = 0x40;
            static u32 const FLAG_FREE      = 0x00;
            static u32 const FLAG_USED      = 0x80;
            static u32 const FLAG_MASK      = 0xC0;

            u32 m_addr;            // addr = base_addr(m_addr * size_step) + flags [Bit 31&30 = Used/Free, Color]
			u16 m_flags;
			u16 m_dummy;
            u16 m_pages_range;     // Number of pages that this block holds
            u16 m_pages_committed; // Number of pages committed
            u32 m_prev_addr;       // previous node in memory, can be free, can be allocated (for coalesce)
            u32 m_next_addr;       // next node in memory, can be free, can be allocated (for coalesce)

            void init()
            {
                clear();
                m_addr            = 0;
				m_flags           = 0;
				m_dummy           = 0;
                m_pages_range     = 0;
                m_pages_committed = 0;
                m_prev_addr       = NIL32;
                m_next_addr       = NIL32;
            }

            inline u32   get_addr() const { return m_addr; }
            inline void* get_full_address(void* baseaddr, u64 page_size) const
            {
                return (void*)((u64)baseaddr + ((u64)m_addr * page_size));
            }
            inline void set_addr_from_full_address(void* baseaddr, u64 page_size, void* addr)
            {
                m_addr = (u32)(((u64)addr - (u64)baseaddr) / page_size);
            }
            inline u16  get_page_cnt() const { return (u16)m_pages_range; }
            inline void set_page_cnt(u16 page_cnt) { m_pages_range = page_cnt; }
            inline u16  get_commited_page_cnt() const { return m_pages_committed; }
            inline void set_commited_page_cnt(u64 size, u32 page_size) { m_pages_committed = (u16)((size + (page_size - 1)) / page_size); }

            inline void set_used(bool used) { m_flags = m_flags | FLAG_USED; }
            inline bool is_used() const { return (m_flags & FLAG_USED) == FLAG_USED; }
            inline bool is_free() const { return (m_flags & FLAG_USED) == FLAG_FREE; }

            inline void set_color_red() { m_flags = m_flags | FLAG_COLOR_RED; }
            inline void set_color_black() { m_flags = m_flags & ~FLAG_COLOR_RED; }
            inline bool is_color_red() const { return (m_flags & FLAG_COLOR_RED) == FLAG_COLOR_RED; }
            inline bool is_color_black() const { return (m_flags & FLAG_COLOR_RED) == 0; }

            inline u64 get_addr_key() const { return m_addr; }
            inline u64 get_size_key() const { return get_size_addr_key(m_pages_range, m_addr); }

            XCORE_CLASS_PLACEMENT_NEW_DELETE
        };

        inline nnode_t* idx2node(xfsadexed* nodes, u32 inode)
        {
			if (inode == NIL32)
				return nullptr;
            return nodes->idx2obj<nnode_t>(inode);
        }
        inline u32 node2idx(xfsadexed* nodes, nnode_t* pnode)
        {
			if (pnode == nullptr)
				return NIL32;
            return nodes->obj2idx<nnode_t>(pnode);
        }

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
                u32 const      addr = (u32)pkey;
                if (addr < n->get_addr())
                    return -1;
                if (addr > n->get_addr())
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
                if (addr < n->get_addr())
                    return -1;
                if (addr > n->get_addr())
                    return 1;
                return 0;
            }

            s32 find_node_f(const u64 pkey, const xbst::index_based::node_t* node)
            {
                u32 const      num_pages = (u32)(pkey >> 32);
                nnode_t const* n         = (nnode_t const*)(node);
                if (num_pages <= n->m_pages_range)
	                return 0;
                return -1;
            }

            static tree_t config;
        } // namespace bst_size

        struct xspace_t;
        struct xspaces_t
        {
            void*     m_mem_address;
            u64       m_space_size;  // The size of a range (e.g. 1 GiB)
            u32       m_space_count; // The full address range is divided into 'n' spaces
            u32       m_page_size;
            u16       m_freelist; // The doubly linked list spaces that are 'empty'
            xspace_t* m_spaces;   // The array of memory spaces that make up the full address range

            void init(xalloc* main_heap, void* mem_address, u64 mem_range, u64 space_size, u32 page_size);
            void release(xalloc* main_heap);

            bool         is_space_empty(void* addr) const;
            bool         is_space_full(void* addr) const;
            inline void* get_base_address() const { return m_mem_address; }
            inline u32   get_page_size() const { return m_page_size; }
            inline u64   get_space_size() const { return m_space_size; }
            void*        get_space_addr(void* addr) const;

            inline u16 ptr2addr(void* p) const
            {
                u16 const idx = (u16)(((u64)p - (u64)get_space_addr(p)) / get_page_size());
                ASSERT(idx < m_space_count);
                return idx;
            }

            void      insert_space_into_list(u16& head, u16 item);
            xspace_t* remove_space_from_list(u16& head, u16 item);
            bool      obtain_space(void*& addr, u16& ispace, xspace_t*& pspace);
            void      release_space(void*& addr);
            void      release_space(u16 ispace, xspace_t* pspace);
            u16       ptr2level(void* ptr) const;
            u16       ptr2space(void* ptr) const;
            void      register_alloc(void* addr);
            u16       register_dealloc(void* addr);

            XCORE_CLASS_PLACEMENT_NEW_DELETE
        };

		// Note: We can also simplify this by not using a BST but just a single 64-bit integer.
		// This means that the size of a space is kinda fixed: ``basesize * 64``
		// Example:
		//    - minimumsize =  2.5 MB
		//    - maximumsize = 32   MB
		//    - stepsize    =  2   MB
		//  Calculate 
		//    -> basesize  =  2.5 MB aligned down with 2 MB = 2 MB
		//    -> spacesize =  64 * 2 MB = 128 MB
		//
		// Also, the maximumsize has to be smaller that the spacesize, in this case we can fit
		// 4 maximumsize allocations in a single space.
		// 

        struct xspace_t
        {
            void reset()
            {
                m_free_bst  = NIL32;
                m_alloc_bst = NIL32;
                m_alloc_cnt = 0;
                m_level_idx = NIL16;
                m_next      = NIL16;
                m_prev      = NIL16;
            }

            bool is_unused() const { return m_alloc_cnt == 0; }
            bool is_empty() const { return m_alloc_cnt == 0; }
            bool is_full() const { return m_free_bst == NIL32; }
			
			void set_link(u16 p, u16 n) { m_prev = p; m_next = n; }
			bool is_linked(u16 i) const { return (m_prev==i) && (m_next==i); }

            void register_alloc() { m_alloc_cnt += 1; }
            void register_dealloc()
            {
                ASSERT(m_alloc_cnt > 0);
                m_alloc_cnt -= 1;
            }

            void init(u16 lvl_idx, xfsadexed* node_alloc, void* vmem_base_addr, u64 allocsize_step, void* range_base_addr, u64 space_size, u32 page_size)
            {
				m_level_idx = lvl_idx;
                // Create a new nnode_t for this range and add it
                nnode_t* pnode = (nnode_t*)node_alloc->allocate();
                u32      inode = node_alloc->ptr2idx(pnode);
                pnode->init();
                pnode->set_addr_from_full_address(vmem_base_addr, allocsize_step, range_base_addr);
                pnode->set_page_cnt((u16)(space_size / page_size));
                add_to_size_db(inode, pnode, node_alloc);
            }

            void add_to_size_db(u32 inode, nnode_t* pnode, xdexer* dexer)
            {
                u64 const key = pnode->get_size_key();
                xbst::index_based::insert(m_free_bst, &bst_size::config, dexer, key, inode);
            }

            void remove_from_size_db(u32 inode, nnode_t* pnode, xdexer* dexer)
            {
                u64 const key = pnode->get_size_key();
                xbst::index_based::insert(m_free_bst, &bst_size::config, dexer, key, inode);
            }

            void add_to_addr_chain(u32 icurr, nnode_t* pcurr, u32 inode, nnode_t* pnode, xdexer* dexer)
            {
                u32 const inext    = pcurr->m_next_addr;
				if (inext != NIL32)
				{
					nnode_t*  pnext    = dexer->idx2obj<nnode_t>(inext);
					pnext->m_prev_addr = inode;
				}
                pcurr->m_next_addr = inode;
				pnode->m_prev_addr = icurr;
				pnode->m_next_addr = inext;
            }

            void remove_from_addr_chain(u32 inode, nnode_t* pnode, xdexer* dexer)
            {
                u32 const iprev = pnode->m_prev_addr;
                u32 const inext = pnode->m_next_addr;

				nnode_t*  pprev = dexer->idx2obj<nnode_t>(iprev);
                if (pprev != nullptr)
                {
                    pprev->m_next_addr = inext;
                }
                nnode_t*  pnext = dexer->idx2obj<nnode_t>(inext);
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
                u64 const key = spaces->ptr2addr(ptr);

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
            m_spaces      = (xspace_t*)main_heap->allocate(sizeof(xspace_t) * m_space_count, sizeof(void*));
            for (u32 i = 0; i < m_space_count; i++)
            {
                m_spaces[i].reset();
            }

			m_spaces[0].set_link(m_space_count-1, 1);
			for (u32 i = 1; i < m_space_count; i++)
            {
				m_spaces[i].set_link(i-1, i+1);
            }
			m_spaces[m_space_count-1].set_link(m_space_count-2, 0);
            m_freelist = 0;
        }

        inline xspace_t* idx2space(xspaces_t* self, u16 idx)
        {
            ASSERT(idx < self->m_space_count);
            return &self->m_spaces[idx];
        }

        void xspaces_t::release(xalloc* main_heap)
        {
            m_mem_address = nullptr;
            m_space_size  = 0;
            m_space_count = 0;
            m_freelist    = NIL16;
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
            if (m_freelist != NIL16)
            {
                ispace = m_freelist;
				pspace = idx2space(this, ispace);
                remove_space_from_list(m_freelist, ispace);
                pspace->m_level_idx = NIL16;
                pspace->m_next      = NIL16;
                pspace->m_prev      = NIL16;
                addr                = (void*)((u64)m_mem_address + (m_space_size * ispace));
                return true;
            }
			else
			{
				ispace = NIL16;
				pspace = nullptr;
				return false;
			}
        }

        void xspaces_t::insert_space_into_list(u16& head, u16 item)
        {
            xspace_t* pitem = idx2space(this, item);
			ASSERT(pitem != nullptr);

			if (head == NIL16)
			{
				head = item;
				pitem->set_link(item, item);
				return;
			}

            xspace_t* phead = idx2space(this, head);
			xspace_t* pprev = idx2space(this, phead->m_prev);
            pitem->m_prev   = phead->m_prev;
            pitem->m_next   = head;
            phead->m_prev   = item;
			pprev->m_next   = item;
            head            = item;
        }

        xspace_t* xspaces_t::remove_space_from_list(u16& head, u16 item)
        {
            xspace_t* pitem = idx2space(this, item);
			if (pitem->is_linked(item))
			{
				ASSERT(head == item);
				head = NIL16;
				return pitem;
			}

            xspace_t* pprev = idx2space(this, pitem->m_prev);
            xspace_t* pnext = idx2space(this, pitem->m_next);
            pprev->m_next = pitem->m_next;
            pnext->m_prev = pitem->m_prev;
			head = pitem->m_next;
			pitem->set_link(NIL16, NIL16);
            return pitem;
        }

        void xspaces_t::release_space(void*& addr)
        {
            // Put this space back into the free-list !
            u32 const ispace = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            xspace_t* pspace = idx2space(this, ispace);
            pspace->m_level_idx = NIL16;
            insert_space_into_list(m_freelist, ispace);
        }

        void xspaces_t::release_space(u16 ispace, xspace_t* pspace)
        {
            // Put this space back into the free-list !
            pspace->m_level_idx = NIL16;
            insert_space_into_list(m_freelist, ispace);
        }

        u16 xspaces_t::ptr2level(void* ptr) const
        {
            u16 const ispace = (u16)(((uptr)ptr - (uptr)m_mem_address) / (uptr)m_space_size);
            ASSERT(m_spaces[ispace].m_level_idx != NIL16);
            return m_spaces[ispace].m_level_idx;
        }

        u16 xspaces_t::ptr2space(void* addr) const
        {
            u16 const ispace = (u16)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            return ispace;
        }

        // We register allocations and de-allocations on every space to know if
        // it is stil used. We are interested to know when a space is free
        // so that we can release it back.
        void xspaces_t::register_alloc(void* addr)
        {
            u16 const ispace = (u16)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            m_spaces[ispace].register_alloc();
        }

        // Returns the level index
        u16 xspaces_t::register_dealloc(void* addr)
        {
            u16 const ispace = (u16)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_space_size);
            m_spaces[ispace].register_dealloc();
            return m_spaces[ispace].m_level_idx;
        }

        struct xlevel_t
        {
            void  reset();
            void* allocate(u32 size_in_pages, u16 lvl_idx, u32 lvl_size_in_pages, xspaces_t* spaces, xfsadexed* node_alloc, void* vmem_base_addr, u32 allocsize_step);
            void  deallocate(void* ptr, xfsadexed* node_alloc, xspaces_t* spaces);
            u16   m_spaces_not_full; // Linked list of spaces that are not full and not empty
        };

        void xlevel_t::reset() { m_spaces_not_full = NIL16; }

        void* xlevel_t::allocate(u32 size_in_pages, u16 lvl_idx, u32 lvl_size_in_pages, xspaces_t* spaces, xfsadexed* node_alloc, void* vmem_base_addr, u32 allocsize_step)
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
                pspace->init(lvl_idx, node_alloc, vmem_base_addr, allocsize_step, space_base_addr, spaces->get_space_size(), spaces->get_page_size());
                spaces->insert_space_into_list(m_spaces_not_full, ispace);
            }
            else
            {
                pspace = idx2space(spaces, ispace);
            }

            // Take a node from the 'free' bst of the space
            u32       inode;
            u64 const key = get_size_addr_key(size_in_pages, 0);
            xbst::index_based::find_specific(pspace->m_free_bst, &bst_size::config, node_alloc, key, inode, bst_size::find_node_f);
            
			nnode_t* pnode = idx2node(node_alloc, inode);

            u64 const size_in_bytes      = size_in_pages * spaces->get_page_size();
            u16 const node_size_in_pages = pnode->get_page_cnt();
            if (node_size_in_pages >= lvl_size_in_pages)
            {
                u32 const inext = pnode->m_next_addr;
                nnode_t*  pnext = idx2node(node_alloc, inext);

                // split the node and add it back into 'free'
                nnode_t* psplit     = node_alloc->construct<nnode_t>();
                u32      isplit     = node2idx(node_alloc, psplit);
                psplit->m_prev_addr = inode;
                psplit->m_next_addr = inext;
				if (pnext != nullptr)
				{
					pnext->m_prev_addr  = isplit;
				}
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

        void xlevel_t::deallocate(void* ptr, xfsadexed* node_alloc, xspaces_t* spaces)
        {
            u16 const  ispace   = spaces->ptr2space(ptr);
            xspace_t*  pspace   = idx2space(spaces, ispace);
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

        struct xinstance_t
        {
            xalloc*    m_main_alloc;
            xfsadexed* m_node_alloc;
            void*      m_vmem_base_addr;
            u32        m_allocsize_min;
            u32        m_allocsize_max;
            u32        m_allocsize_step;
            u32        m_pagesize;
            u32        m_level_cnt;
            xlevel_t*  m_levels;
            xspaces_t* m_spaces;

            XCORE_CLASS_PLACEMENT_NEW_DELETE
        };

        inline u16 lvl2size_in_pages(xinstance_t* self, u16 ilevel) 
		{
            ASSERT(ilevel < self->m_level_cnt);
			return (((ilevel+1) * self->m_allocsize_step) + (self->m_pagesize - 1)) / self->m_pagesize; 
		}

        inline u16 ptr2lvl(xinstance_t* self, void* ptr)
        {
            u16 const ilevel = self->m_spaces->ptr2level(ptr);
            ASSERT(ilevel < self->m_level_cnt);
            return ilevel;
        }

        inline xlevel_t* idx2lvl(xinstance_t* self, u16 ilevel)
        {
            ASSERT(ilevel < self->m_level_cnt);
            return &self->m_levels[ilevel];
        }

        xinstance_t* create(xalloc* main_alloc, xfsadexed* node_alloc, void* vmem_address, u64 vmem_space, u64 space_size, u32 allocsize_min, u32 allocsize_max, u32 allocsize_step, u32 page_size)
        {
			ASSERT(xispo2(allocsize_step));
			ASSERT(xispo2(page_size));

            // Example: min=640KB, max=32MB, step=1MB
            // Levels -> 1MB, 2MB, 3MB ... 30MB 31MB 32MB, in total 32 levels
			const u32     base_size  = xalignDown(allocsize_min, allocsize_step);
            const u32     num_levels = ((allocsize_max + (allocsize_step - 1) - base_size) / allocsize_step);
            const u32     mem_size   = sizeof(xlevel_t) * num_levels + sizeof(xspaces_t) + sizeof(xinstance_t);
            xbyte*        mem_block  = (xbyte*)main_alloc->allocate(mem_size);
            xallocinplace aip(mem_block, mem_size);
            xinstance_t*  instance    = aip.construct<xinstance_t>();
            xspaces_t*    spaces      = aip.construct<xspaces_t>();
            xlevel_t*     level_array = (xlevel_t*)aip.allocate(sizeof(xlevel_t) * num_levels);

            instance->m_main_alloc     = main_alloc;
            instance->m_node_alloc     = node_alloc;
            instance->m_vmem_base_addr = vmem_address;

            instance->m_allocsize_min  = allocsize_min;
            instance->m_allocsize_max  = allocsize_max;
            instance->m_allocsize_step = allocsize_step;
            instance->m_pagesize       = page_size;
            instance->m_level_cnt      = num_levels;
            instance->m_levels         = level_array;
			instance->m_spaces         = spaces;
            for (u32 i = 0; i < instance->m_level_cnt; ++i)
            {
                instance->m_levels[i].reset();
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
            instance->m_spaces->init(main_alloc, instance->m_vmem_base_addr, vmem_space, space_size, page_size);

            return instance;
        }

        void destroy(xinstance_t* self)
        {
            self->m_spaces->release(self->m_main_alloc);
            self->m_spaces = nullptr;
            self->m_levels = nullptr;
            self->m_main_alloc->deallocate(self);
        }

		// Example:
		//    - minimumsize =  2.5 MB
		//    - maximumsize = 33   MB
		//    - stepsize    =  2   MB
		//  Calculate 
		//    -> basesize =  2.5 MB aligned down with 2 MB = 2 MB
		//  Compute Level Count
		//    -> numlevels = ((maximumsize + (stepsize - 1) - basesize) / stepsize);
		//    -> e.g. ((33 MB + (2 MB - 1) - 2 MB) / 2 MB) = 16
		//    4,6,8,10, 12,14,16,18, 20,22,24,26, 28,30,32,34

		//  Compute Level Index 
		//    -> (size - basesize) / stepsize
		//    -> e.g. size = 2.7 MB -> ( 2.7 MB - 2 MB) / 2 MB = 0
		//    -> e.g. size =  33 MB -> (33   MB - 2 MB) / 2 MB = 15

        void* allocate(xinstance_t* self, u32 size, u32 alignment)
        {
            ASSERT(size >= self->m_allocsize_min && size <= self->m_allocsize_max);
            ASSERT(alignment <= self->m_allocsize_step);
			u32 const base_size = xalignDown(self->m_allocsize_min, self->m_allocsize_step);
            u16 const ilevel = ((size - base_size) / self->m_allocsize_step);
			xlevel_t* plevel = idx2lvl(self, ilevel);
            u16 const size_in_pages = (u16)((size + (self->m_pagesize - 1)) / self->m_pagesize);
            void*     ptr    = plevel->allocate(size_in_pages, ilevel, lvl2size_in_pages(self, ilevel), self->m_spaces, self->m_node_alloc, self->m_vmem_base_addr, self->m_allocsize_step);
            ASSERT(ptr2lvl(self, ptr) == ilevel);
            self->m_spaces->register_alloc(ptr);
            return ptr;
        }

        void deallocate(xinstance_t* self, void* ptr)
        {
            u16 const ilevel = self->m_spaces->register_dealloc(ptr);
			ASSERT(ilevel < self->m_level_cnt);
            xlevel_t* plevel = &self->m_levels[ilevel];
            plevel->deallocate(ptr, self->m_node_alloc, self->m_spaces);
        }
    } // namespace xsegregatedstrat
} // namespace xcore