#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"
#include "xbase/x_hibitset.h"

#include "xvmem/private/x_strategy_coalesce.h"

namespace xcore
{
    namespace xcoalescestrat_direct
    {
        /*
        When limiting the number of nodes to 64K we also limit the amount of memory
        this strategy can manage. Based on the minumum size and the maximum amount
        of nodes you can compute the maximum memory range.
        64K * 8KB = 512MB

        The addr-db is dividing the memory range into blocks, every block spans a
        certain range. For example if the range of a block is 256KB then we would
        end up with 512MB / 256KB = 2048 blocks.

        The size-db is dividing the size range using size-step.
        As an example: Let's put the minimum size to 8KB and 640KB as the maximum
        size with a size-step of 1024. This means we would have 640-8 entries in
        the size-db.
        */

        // Node: as an allocated block or as a free block
        // This node should be exactly 16 bytes.
        struct node_t
        {
            static u16 const NIL       = 0xffff;
            static u32 const FLAG_FREE = 0x0;
            static u32 const FLAG_USED = 0x80000000;
            static u32 const FLAG_MASK = 0x80000000;

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

            inline void* get_addr(void* baseaddr, u64 size_step) const { return (void*)((u64)baseaddr + ((u64)m_addr * size_step)); }
            inline void* set_addr(void* baseaddr, u64 size_step, void* addr) { m_addr = (u32)(((u64)addr - (u64)baseaddr) / size_step); }
            inline void  set_size(u32 size) { m_size = (m_size & FLAG_MASK) | size; }
            inline u32   get_size() const { return m_size & FLAG_MASK; }
            inline void  set_used(bool used) { m_size = m_size | FLAG_USED; }
            inline bool  is_used() const { return (m_size & FLAG_MASK) == FLAG_USED; }
        };

        struct block_t
        {
            u32 m_head;
        };

        struct xsize_db
        {
            u32   m_size_count;
            u32*  m_level0_array;
            u32** m_level1_array;
        };

        struct xhb1024_t
        {
            u32  m_level0;
            u32* m_level1; // 32 * sizeof(u32)
        };

        static inline void* addr_add(void* addr, u64 offset) { return (void*)((u64)addr + offset); }

        struct xinstance_t
        {
            xinstance_t();

            // Main API
            void* allocate(u32 size, u32 alignment);
            void  deallocate(void* p);

            // Coalesce related functions
            void initialize(xalloc* main_heap, xfsadexed* node_heap, void* mem_addr, u64 mem_size, u32 size_min, u32 size_max, u32 size_step, u32 list_size);
            void release();
            void remove_from_addr(u32 inode, node_t* pnode);
            void add_to_addr_db(u32 inode, node_t* pnode);
            bool pop_from_addr_db(void* ptr, u32& inode, node_t*& pnode);
            bool can_split_node(u32 size, u32 alignment, u32 inode, node_t* pnode);
            void split_node(u32 size, u32 alignment, u32 inode, node_t* pnode, u32& rinode, node_t*& rpnode);
            bool pop_from_size_db(u32 size, u32& inode, node_t*& pnode);
            void add_to_size_db(u32 inode, node_t* pnode);
            void remove_from_size_db(u32 inode, node_t* pnode);

            inline node_t* alloc_node() { return (node_t*)m_node_heap->allocate(); }
            inline void    dealloc_node(u32 inode, node_t* pnode)
            {
                ASSERT(m_node_heap->idx2ptr(inode) == pnode);
                m_node_heap->deallocate(pnode);
            }

            inline u32 calc_size_slot(u32 size) const
            {
                ASSERT(size >= m_alloc_size_min);
                if (size > m_alloc_size_max)
                    return m_size_nodes_cnt;
                u32 const slot = (size - m_alloc_size_min) / m_alloc_size_step;
                ASSERT(slot < m_size_nodes_cnt);
                return slot;
            }

            inline u32 calc_addr_slot(void* addr) const
            {
                u32 const slot = (u32)(((u64)addr - (u64)m_memory_addr) / m_addr_alignment);
                ASSERT(slot < m_addr_nodes_cnt);
                return slot;
            }

            inline node_t* idx2node(u32 idx)
            {
                node_t* pnode = nullptr;
                if (idx != node_t::NIL)
                    pnode = (node_t*)m_node_heap->idx2ptr(idx);
                return pnode;
            }
            inline u32 node2idx(node_t* n) const
            {
                u32 const idx = (n == nullptr) ? (node_t::NIL) : (m_node_heap->ptr2idx(n));
                return idx;
            }

            // Global variables
            xalloc*    m_main_heap;
            xfsadexed* m_node_heap;
            void*      m_memory_addr;
            u64        m_mspace_size; // 32 MB

            // Local variables ?
            u32      m_alloc_size_min;
            u32      m_alloc_size_max;
            u32      m_alloc_size_step;
            u32*     m_size_nodes;              // (128 * 8) * sizeof(u32)
            u32      m_size_nodes_occupancy[4]; // 128 entries
            u32      m_addr_node_range;
            u32      m_addr_nodes_count;
            block_t* m_addr_nodes_array;
        };

        xcoalescee::xcoalescee()
            : m_main_heap(nullptr)
            , m_node_heap(nullptr)
            , m_memory_addr(nullptr)
            , m_memory_size(0)
            , m_alloc_size_min(0)
            , m_alloc_size_max(0)
            , m_alloc_size_step(0)
            , m_size_nodes(nullptr)
            , m_addr_nodes(nullptr)
            , m_addr_alignment(0)
        {
        }

        void xcoalescee::initialize(xalloc* main_heap, xfsadexed* node_heap, void* mem_addr, u64 mem_size, u32 size_min, u32 size_max, u32 size_step, u32 list_size)
        {
            m_main_heap       = main_heap;
            m_node_heap       = node_heap;
            m_memory_addr     = mem_addr;
            m_memory_size     = mem_size;
            m_alloc_size_min  = size_min;
            m_alloc_size_max  = size_max;
            m_alloc_size_step = size_step;
            m_addr_alignment  = size_min * list_size;

            m_size_nodes_cnt = (m_alloc_size_max - m_alloc_size_min) / m_alloc_size_step;
            m_size_nodes     = (u32*)m_main_heap->allocate((m_size_nodes_cnt + 1) * sizeof(u32), sizeof(void*));
            for (s32 i = 0; i <= m_size_nodes_cnt; i++)
            {
                m_size_nodes[i] = node_t::NIL;
            }

            // Which sizes are available in 'm_size_nodes' is known through
            // this hierarchical set of bits.
            m_size_nodes_occupancy.init(m_main_heap, m_size_nodes_cnt + 1);

            // Please keep the number of addr slots low
            ASSERT((m_memory_size / m_addr_alignment) < (u64)64 * 1024);
            m_addr_nodes_cnt = (s32)(m_memory_size / m_addr_alignment);
            m_addr_nodes     = (u32*)m_main_heap->allocate(m_addr_nodes_cnt * sizeof(u32), sizeof(void*));
            for (s32 i = 0; i < m_addr_nodes_cnt; i++)
            {
                m_addr_nodes[i] = node_t::NIL;
            }

            node_t* head_node = m_node_heap->construct<node_t>();
            node_t* tail_node = m_node_heap->construct<node_t>();
            head_node->init();
            head_node->set_locked();
            tail_node->init();
            head_node->set_locked();
            node_t*   main_node  = m_node_heap->construct<node_t>();
            u32 const main_inode = m_node_heap->ptr2idx(main_node);
            main_node->init();
            main_node->m_addr      = 0;
            main_node->m_size      = (u32)(mem_size / m_alloc_size_min);
            main_node->m_prev_addr = m_node_heap->ptr2idx(head_node);
            main_node->m_next_addr = m_node_heap->ptr2idx(tail_node);
            head_node->m_next_addr = main_inode;
            tail_node->m_prev_addr = main_inode;
            add_to_size_db(main_inode, main_node);
        }

        void xcoalescee::release()
        {
            // TODO: Iterate over the size_db and addr_db releasing all the allocated nodes

            m_main_heap->deallocate(m_size_nodes);
            m_main_heap->deallocate(m_addr_nodes);
            m_size_nodes_occupancy.release(m_main_heap);
        }

        void* xcoalescee::allocate(u32 size, u32 alignment)
        {
            u32     inode = node_t::NIL;
            node_t* pnode = nullptr;
            if (!pop_from_size_db(size, inode, pnode))
            {
                // Could not find a size to fullfill the request
                return nullptr;
            }
            // See if we have a split-off part, if so insert an addr node
            //   after the current that is marked as 'Free' and also add it
            //   to the size DB
            if (can_split_node(size, alignment, inode, pnode))
            {
                u32     rinode;
                node_t* rpnode;
                split_node(size, alignment, inode, pnode, rinode, rpnode);
                add_to_size_db(rinode, rpnode);
            }

            // Add it to the address-db so that we can find it when deallocating
            add_to_addr_db(inode, pnode);

            // Mark the current addr node as 'used'
            pnode->set_used(true);

            // Return the address
            return pnode->get_addr(m_memory_addr, m_alloc_size_min);
        }

        void xcoalescee::deallocate(void* p)
        {
            u32     inode = node_t::NIL;
            node_t* pnode = nullptr;
            if (!pop_from_addr_db(p, inode, pnode))
            {
                // Could not find address in the addr_db
                return;
            }

            // Coalesce:
            //   Determine the 'prev' and 'next' of the current addr node
            //   If 'prev' is marked as 'Free' then coalesce (merge) with it
            //   Remove the size node that belonged to 'prev' from the size DB
            //   If 'next' is marked as 'Free' then coalesce (merge) with it
            //   Remove the size node that belonged to 'next' from the size DB
            //   Build the size node with the correct size and reference the 'merged' addr node
            //   Add the size node to the size DB
            //   Done

            u32     inode_prev = pnode->m_prev_addr;
            u32     inode_next = pnode->m_next_addr;
            node_t* pnode_prev = idx2node(inode_prev);
            node_t* pnode_next = idx2node(inode_next);

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
                remove_from_size_db(inode_prev, pnode_prev);
                remove_from_size_db(inode_next, pnode_next);
                pnode_prev->m_size += pnode->m_size + pnode_next->m_size;
                add_to_size_db(inode_prev, pnode_prev);
                remove_from_addr(inode, pnode);
                remove_from_addr(inode_next, pnode_next);
                dealloc_node(inode, pnode);
                dealloc_node(inode_next, pnode_next);
            }
            else if (!pnode_prev->is_free() && pnode_next->is_free())
            {
                // next is marked as 'free' (prev is 'used')
                // - remove next from size DB and physical addr list
                // - deallocate 'next'
                // - add the size of 'next' to 'current'
                // - add size to size DB
                remove_from_size_db(inode_next, pnode_next);
                pnode->m_size += pnode_next->m_size;
                add_to_size_db(inode, pnode);
                remove_from_addr(inode_next, pnode_next);
                dealloc_node(inode_next, pnode_next);
            }
            else if (pnode_prev->is_free() && !pnode_next->is_free())
            {
                // prev is marked as 'free'. (next is 'used')
                // - remove this addr/size node
                // - rem/add size node of 'prev', adding the size of 'current'
                // - deallocate 'current'
                remove_from_size_db(inode_prev, pnode_prev);
                pnode_prev->m_size += pnode->m_size;
                add_to_size_db(inode_prev, pnode_prev);
                remove_from_addr(inode, pnode);
                dealloc_node(inode, pnode);
            }
            else if (!pnode_prev->is_free() && !pnode_next->is_free())
            {
                // prev and next are marked as 'used'.
                // - add current to size DB
                add_to_size_db(inode, pnode);
            }
        }

        void xcoalescee::remove_from_addr(u32 idx, node_t* pnode)
        {
            node_t* pnode_prev      = idx2node(pnode->m_prev_addr);
            node_t* pnode_next      = idx2node(pnode->m_next_addr);
            pnode_prev->m_next_addr = pnode->m_next_addr;
            pnode_next->m_prev_addr = pnode->m_prev_addr;
        }

        void xcoalescee::add_to_addr_db(u32 inode, node_t* pnode)
        {
            void*     addr  = pnode->get_addr(m_memory_addr, m_alloc_size_min);
            u32 const slot  = calc_addr_slot(addr);
            u32 const ihead = m_addr_nodes[slot];
            if (ihead == node_t::NIL)
            {
                m_addr_nodes[slot] = inode;
                pnode->m_next_db   = inode;
                pnode->m_prev_db   = inode;
            }
            else
            {
                node_t* phead      = idx2node(ihead);
                node_t* pprev      = idx2node(phead->m_prev_db);
                pnode->m_prev_db   = phead->m_prev_db;
                phead->m_prev_db   = inode;
                pprev->m_next_db   = inode;
                pnode->m_next_db   = ihead;
                m_addr_nodes[slot] = inode;
            }
        }

        bool xcoalescee::pop_from_addr_db(void* ptr, u32& inode, node_t*& pnode)
        {
            // Calculate the slot in the addr DB
            // Iterate through the list at that slot until we find 'p'
            // Remove the item from the list
            u32 const addr_slot = calc_addr_slot(ptr);
            u32*      pihead    = &m_addr_nodes[addr_slot];
            pnode               = idx2node(*pihead);
            while (pnode != nullptr)
            {
                if (ptr == pnode->get_addr(m_memory_addr, m_alloc_size_step))
                {
                    // Now we have the addr node pointer and index
                    *pihead = pnode->m_next_db;
                    return true;
                }
                pihead = &pnode->m_next_db;
                pnode  = idx2node(*pihead);
            }
            return false;
        }

        bool xcoalescee::can_split_node(u32 size, u32 alignment, u32 inode, node_t* pnode) { return ((pnode->m_size - size) >= m_alloc_size_step); }

        void xcoalescee::split_node(u32 size, u32 alignment, u32 inode, node_t* pnode, u32& rinode, node_t*& rpnode)
        {
            u32 alloc_size = size;

            // TODO: Compute alloc size including alignment

            // Construct new naddr node and link it into the physical address doubly linked list
            rpnode = m_node_heap->construct<node_t>();
            rinode = m_node_heap->ptr2idx(rpnode);
            rpnode->init();
            rpnode->m_size          = pnode->m_size - alloc_size;
            rpnode->m_flags         = pnode->m_flags;
            rpnode->m_prev_addr     = inode;
            rpnode->m_next_addr     = pnode->m_next_addr;
            pnode->m_next_addr      = rinode;
            node_t* pnode_next      = idx2node(pnode->m_next_addr);
            pnode_next->m_prev_addr = rinode;
            void* node_addr         = pnode->get_addr(m_memory_addr, m_memory_size);
            void* rest_addr         = addr_add(node_addr, alloc_size);
            rpnode->set_addr(m_memory_addr, m_memory_size, rest_addr);
        }

        bool xcoalescee::pop_from_size_db(u32 _size, u32& inode, node_t*& pnode)
        {
            // TODO: Alignment calculation by inflating the size

            // Find 'good-fit' [size, alignment]
            u32 size = _size;
            if (size < m_alloc_size_min)
                size = m_alloc_size_min;
            size     = (size + (m_alloc_size_step - 1)) & ~(m_alloc_size_step - 1);
            u32 slot = calc_size_slot(size);
            if (!m_size_nodes_occupancy.is_set(slot))
            {
                // Also no large blocks left, so we are unable to allocate
                return false;
            }

            inode = m_size_nodes[slot];
            pnode = (node_t*)m_node_heap->idx2ptr(inode);

            m_size_nodes[slot] = pnode->m_next_db;
            if (m_size_nodes[slot] == node_t::NIL)
            {
                m_size_nodes_occupancy.clr(slot);
            }

            // Remove list pointers
            pnode->m_next_db = node_t::NIL;
            pnode->m_prev_db = node_t::NIL;

            return true;
        }

        void xcoalescee::add_to_size_db(u32 inode, node_t* pnode)
        {
            u32 const size     = pnode->m_size;
            u32 const slot     = calc_size_slot(size);
            u32 const ihead    = m_size_nodes[slot];
            node_t*   phead    = idx2node(ihead);
            node_t*   pprev    = idx2node(phead->m_prev_db);
            pnode->m_prev_db   = phead->m_prev_db;
            phead->m_prev_db   = inode;
            pprev->m_next_db   = inode;
            pnode->m_next_db   = ihead;
            bool const first   = m_size_nodes[slot] == node_t::NIL;
            m_size_nodes[slot] = inode;
            if (first)
            {
                m_size_nodes_occupancy.set(slot);
            }
        }

        class xvmem_allocator_coalesce : public xalloc
        {
        public:
            xvmem_allocator_coalesce()
                : m_internal_heap(nullptr)
                , m_node_alloc(nullptr)
            {
            }

            virtual void* allocate(u32 size, u32 alignment);
            virtual void  deallocate(void* p);
            virtual void  release();

            xalloc*    m_internal_heap;
            xfsadexed* m_node_alloc; // For allocating node_t and nsize_t nodes
            xvmem*     m_vmem;
            xcoalescee m_coalescee;
        };

        void* xvmem_allocator_coalesce::allocate(u32 size, u32 alignment) { return nullptr; }

        void xvmem_allocator_coalesce::deallocate(void* p) {}

        void xvmem_allocator_coalesce::release()
        {
            m_coalescee.release();

            // release virtual memory

            m_internal_heap->destruct<>(this);
        }

        xalloc* gCreateVMemCoalesceAllocator(xalloc* internal_heap, xfsadexed* node_alloc, xvmem* vmem, u64 mem_size, u32 alloc_size_min, u32 alloc_size_max, u32 alloc_size_step, u32 alloc_addr_list_size)
        {
            xvmem_allocator_coalesce* allocator = internal_heap->construct<xvmem_allocator_coalesce>();
            allocator->m_internal_heap          = internal_heap;
            allocator->m_node_alloc             = node_alloc;
            allocator->m_vmem                   = vmem;

            void* memory_addr = nullptr;
            u32   page_size;
            u32   attr = 0;
            vmem->reserve(mem_size, page_size, attr, memory_addr);

            allocator->m_coalescee.initialize(internal_heap, node_alloc, memory_addr, mem_size, alloc_size_min, alloc_size_max, alloc_size_step, alloc_addr_list_size);

            return allocator;
        }
    } // namespace xcoalescestrat_direct

}; // namespace xcore
