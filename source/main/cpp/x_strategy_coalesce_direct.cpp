#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"
#include "xbase/x_hibitset.h"

#include "xvmem/private/x_strategy_coalesce.h"
#include "xvmem/private/x_size_db.h"
#include "xvmem/private/x_addr_db.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    namespace xcoalescestrat_direct
    {
        class xalloc_coalesce_direct : public xalloc
        {
        public:
            virtual void* v_allocate(u32 size, u32 alignment);
            virtual u32   v_deallocate(void* p);
            virtual void  v_release();

            inline void dealloc_node(u32 inode, node_t* pnode)
            {
                ASSERT(m_node_heap->idx2ptr(inode) == pnode);
                m_node_heap->deallocate(pnode);
            }

            inline node_t* idx2node(u32 idx)
            {
                node_t* pnode = nullptr;
                if (idx != node_t::NIL)
                    pnode = (node_t*)m_node_heap->idx2ptr(idx);
                return pnode;
            }

            inline u32 node2idx2(node_t* n)
            {
                if (n == nullptr)
                    return node_t::NIL;
                return m_node_heap->ptr2idx(n);
            }

            XCORE_CLASS_PLACEMENT_NEW_DELETE

            // Global variables
            xalloc*    m_main_heap;
            xfsadexed* m_node_heap;

            // Local variables
            void*     m_mem_base;
            xsize_cfg m_size_cfg;
            xsize_db* m_size_db;
            xaddr_db  m_addr_db;
        };

        void* xalloc_coalesce_direct::v_allocate(u32 size, u32 alignment)
        {
            size = m_size_cfg.align_size(size);

            u32 size_index = m_size_cfg.size_to_index(size);
            u32 addr_index;
            if (!m_size_db->find_size(size_index, addr_index))
                return nullptr;

            node_t*   pnode = m_addr_db.get_node_with_size_index(addr_index, m_node_heap, size_index);
            u32 const inode = node2idx2(pnode);

            // Did we find a node with 'free' space?
            if (pnode == nullptr)
                return nullptr;

            // Should we split the node?
            if (size <= pnode->get_size() && ((pnode->get_size() - size) >= m_size_cfg.m_alloc_size_min))
            {
                m_addr_db.alloc_by_split(inode, pnode, size, m_node_heap, m_size_db, m_size_cfg);
            }
            else
            {
                m_addr_db.alloc(inode, pnode, m_node_heap, m_size_db, m_size_cfg);
            }
            ASSERT(pnode->is_used());

            return (void*)((uptr)pnode->get_addr() + (uptr)m_mem_base);
        }

        u32 xalloc_coalesce_direct::v_deallocate(void* p)
        {
            u32 const addr       = (u32)((uptr)p - (uptr)m_mem_base);
            u32 const addr_index = (addr / m_addr_db.m_addr_range);

            node_t* pnode = m_addr_db.get_node_with_addr(addr_index, m_node_heap, addr);
            u32     inode = m_node_heap->ptr2idx(pnode);
            if (pnode != nullptr)
            {
                pnode->set_free();

                u32 const  size       = pnode->get_size();
                u32        inode_prev = pnode->m_prev_addr;
                u32        inode_next = pnode->m_next_addr;
                node_t*    pnode_prev = idx2node(inode_prev);
                node_t*    pnode_next = idx2node(inode_next);
                bool const merge_prev = pnode_prev->is_free();
                bool const merge_next = pnode_next->is_free();
                m_addr_db.dealloc(inode, pnode, merge_prev, merge_next, m_size_db, m_size_cfg, m_node_heap);
                return size;
            }
            else
            {
                return 0;
            }
        }

        static void initialize(xalloc_coalesce_direct* instance, xalloc* main_heap, xfsadexed* node_heap, u32 size_min, u32 size_max, u32 size_step, void* memory_base, u64 memory_range, u32 addr_count)
        {
            instance->m_main_heap = main_heap;
            instance->m_node_heap = node_heap;

            u32 const size_index_count = ((size_max - size_min) / size_step) + 1; // The +1 is for the size index entry that keeps track of the nodes > max_size
            ASSERT(size_index_count < 256);                                       // There is only one byte reserved to keep track of the size index

            instance->m_mem_base                    = memory_base;
            instance->m_size_cfg.m_size_index_count = size_index_count;
            instance->m_size_cfg.m_alloc_size_min   = size_min;
            instance->m_size_cfg.m_alloc_size_max   = size_max;
            instance->m_size_cfg.m_alloc_size_step  = size_step;

            ASSERT(xispo2(addr_count)); // The address node count should be a power-of-2
            instance->m_addr_db.m_addr_count = addr_count;
            instance->m_addr_db.m_addr_range = (u32)(memory_range / addr_count);
            instance->m_addr_db.m_nodes      = (u32*)main_heap->allocate(addr_count * sizeof(u32), sizeof(void*));
            instance->m_addr_db.reset();

            node_t*   head_node  = instance->m_node_heap->construct<node_t>();
            u32 const head_inode = instance->m_node_heap->ptr2idx(head_node);
            node_t*   tail_node  = instance->m_node_heap->construct<node_t>();
            u32 const tail_inode = instance->m_node_heap->ptr2idx(tail_node);
            node_t*   main_node  = instance->m_node_heap->construct<node_t>();
            u32 const main_inode = instance->m_node_heap->ptr2idx(main_node);
            head_node->init();
            tail_node->init();
            main_node->init();
            head_node->set_used();
			head_node->set_locked();
            head_node->set_addr(0x0);
            head_node->set_size(0, 0);
            tail_node->set_used();
			tail_node->set_locked();
            tail_node->set_addr((u32)memory_range);
            tail_node->set_size(0, 0);
            main_node->set_addr(0);
            main_node->set_size((u32)memory_range, size_index_count - 1);

            // head <-> main <-> tail
            main_node->m_prev_addr = head_inode;
            main_node->m_next_addr = tail_inode;
            head_node->m_next_addr = main_inode;
            tail_node->m_prev_addr = main_inode;

            if (addr_count >= 64 && addr_count <= 4096)
            {
                instance->m_size_db = main_heap->construct<xsize_db>();
                instance->m_size_db->initialize(main_heap, size_index_count, addr_count);
            }
            else
            {
                ASSERT(false); // Not implemented
            }

            instance->m_size_db->insert_size(main_node->get_size_index(), 0);
            instance->m_addr_db.m_nodes[0] = head_inode;
        }

        void xalloc_coalesce_direct::v_release()
        {
            u32     inode = m_addr_db.m_nodes[0]; // 'head' node
            node_t* pnode = idx2node(inode);
            while (pnode != nullptr)
            {
                u32 const inext = pnode->m_next_addr;
                dealloc_node(inode, pnode);
                inode = inext;
                pnode = idx2node(inode);
            }

            m_size_db->release(m_main_heap);
            m_main_heap->deallocate(m_size_db);
            m_main_heap->deallocate(m_addr_db.m_nodes);
            m_main_heap->deallocate(this);
        }

    } // namespace xcoalescestrat_direct

    using namespace xcoalescestrat_direct;

    xalloc* create_alloc_coalesce_direct(xalloc* main_heap, xfsadexed* node_heap, void* mem_base, u32 mem_range, u32 alloc_size_min, u32 alloc_size_max, u32 alloc_size_step, u32 addr_cnt)
    {
        xalloc_coalesce_direct* allocator = main_heap->construct<xalloc_coalesce_direct>();
        initialize(allocator, main_heap, node_heap, alloc_size_min, alloc_size_max, alloc_size_step, mem_base, mem_range, addr_cnt);
        return allocator;
    }

}; // namespace xcore
