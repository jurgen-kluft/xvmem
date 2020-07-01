#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"
#include "xbase/x_hibitset.h"

#include "xvmem/private/x_strategy_coalesce.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    namespace xcoalescestrat_direct
    {
        static inline void* advance_ptr(void* ptr, u64 size) { return (void*)((uptr)ptr + size); }

		class xsize_db
		{
		public:
			enum { DB0_WIDTH = 64, DB1_WIDTH = 64, WIDTH_OCC = 64 };

			void initialize(xalloc*, u32 sizecnt, u32 addrcnt);
			void release(xalloc*);
			void reset();

			void remove_size(u32 size_index, u32 addr_index);
			void insert_size(u32 size_index, u32 addr_index);
			bool find_size(u32& size_index, u32& addr_index) const;

			XCORE_CLASS_PLACEMENT_NEW_DELETE

		private:
			u64  m_size_occ[4];       // u64[4] (maximum of <= 256 unique sizes)
			u16  m_size_cnt;          // number of unique sizes that we are handling
			u16  m_size_db0_cnt;      // db0 = first bit layer into db1
			u32  m_size_db1_cnt;      // db1 = full bit layout of addrcnt for each size
			u64* m_size_db0;          // u64[m_size_db0_cnt] = 16 KB
			u64* m_size_db1;          // u64[m_size_db0_cnt * m_size_db1_cnt] = 128 KB
		};

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
            head_node->set_addr(0);
            head_node->set_size(0, 0);
            tail_node->set_used();
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

        void xaddr_db::reset()
        {
            for (u32 i = 0; i < m_addr_count; ++i)
                m_nodes[i] = node_t::NIL;
        }

        void xaddr_db::alloc(u32 inode, node_t* pnode, xfsadexed* node_alloc, xsize_db* size_db, xsize_cfg const& size_cfg)
        {
            // Mark this node as 'used' and thus remove it from the size-db
            pnode->set_used();
            u32       node_sidx = pnode->get_size_index();
            u32 const node_aidx = pnode->get_addr_index(m_addr_range);

            // If node 'node_aidx' still has nodes with the same size-index 'node-aidx' then we do not need to
            // remove this from the size-db.
            if (!has_size_index(node_aidx, node_alloc, node_sidx))
            {
                size_db->remove_size(node_sidx, node_aidx);
            }
        }

        void xaddr_db::alloc_by_split(u32 inode, node_t* pnode, u32 size, xfsadexed* node_alloc, xsize_db* size_db, xsize_cfg const& size_cfg)
        {
            u32 const node_sidx = pnode->get_size_index();
            u32 const node_aidx = pnode->get_addr_index(m_addr_range);

            // we need to insert the new node between pnode and it's next node
            u32 const     inext = pnode->m_next_addr;
            node_t* const pnext = (node_t*)node_alloc->idx2ptr(inext);

            // create the new node
            node_t*   pnew = (node_t*)node_alloc->allocate();
            u32 const inew = node_alloc->ptr2idx(pnew);
            pnew->init();

            // Set the new size on node and next
            u32 const node_addr = pnode->get_addr();
            u32 const new_addr  = node_addr + size;
            u32 const new_size  = pnode->get_size() - size;
            pnode->set_used();
            pnode->set_size(size, size_cfg.size_to_index(size));
            pnew->set_size(new_size, size_cfg.size_to_index(new_size));
            pnew->set_addr(new_addr);

            // Link this new node into the address list
            // node <-> new <-> next
            pnew->m_prev_addr  = inode;
            pnew->m_next_addr  = inext;
            pnode->m_next_addr = inew;
            pnext->m_prev_addr = inew;

            // See if this new node is now part of a address node
            u32 const i = pnew->get_addr_index(m_addr_range);
            if (m_nodes[i] == node_t::NIL)
            {
                m_nodes[i] = inew;
            }
            else
            {
                // There already is a list at that address node, check to see if we are the new head
                u32 const     ihead = m_nodes[i];
                node_t* const phead = (node_t*)node_alloc->idx2ptr(ihead);
                if (pnew->get_addr() < phead->get_addr())
                {
                    m_nodes[i] = inew;
                }
            }

            // our new node has to be marked in the size-db
            u32 const new_aidx = pnew->get_addr_index(m_addr_range);
            u32 const new_sidx = pnew->get_size_index();
            size_db->insert_size(new_sidx, new_aidx);

            // Check if node_aidx still has other nodes of the same size-index 'node-aidx'
            if (!has_size_index(node_aidx, node_alloc, node_sidx) && node_sidx != new_sidx)
            {
                size_db->remove_size(node_sidx, node_aidx);
            }
        }

        void xaddr_db::rescan_for_size_index(u32 const addr_index, u8 const size_index, xsize_db* size_db, xdexer* dexer)
        {
            if (has_size_index(addr_index, dexer, size_index))
            {
                size_db->insert_size(size_index, addr_index);
            }
        }

        void xaddr_db::dealloc(u32 inode, node_t* pnode, bool merge_prev, bool merge_next, xsize_db* size_db, xsize_cfg const& size_cfg, xfsadexed* node_heap)
        {
			if (!merge_next && !merge_prev)
			{
                u32 const node_size_index = pnode->get_size_index();
                u32 const node_addr_index = pnode->get_addr_index(m_addr_range);
                size_db->remove_size(node_size_index, node_addr_index);

                remove_node(inode, pnode, node_heap); // we can safely remove 'node' from the address db
                node_heap->deallocate(pnode);

                rescan_for_size_index(node_addr_index, node_size_index, size_db, node_heap);
			}
			else
			{
				if (merge_next)
				{
					u32 const inext = pnode->m_next_addr;
					node_t*   pnext = (node_t*)node_heap->idx2ptr(inext);

					u32 const node_size_index = pnode->get_size_index();
					u32 const node_addr_index = pnode->get_addr_index(m_addr_range);
					size_db->remove_size(node_size_index, node_addr_index);
					u32 const next_size_index = pnext->get_size_index();
					u32 const next_addr_index = pnext->get_addr_index(m_addr_range);
					size_db->remove_size(next_size_index, next_addr_index);

					u32 const next_size = pnext->get_size();
					u32 const node_size = pnode->get_size() + next_size;
					pnode->set_size(node_size, size_cfg.size_to_index(node_size));

					remove_node(inext, pnext, node_heap); // we can safely remove 'next' from the address db
					node_heap->deallocate(pnext);

					rescan_for_size_index(node_addr_index, node_size_index, size_db, node_heap);
					rescan_for_size_index(next_addr_index, next_size_index, size_db, node_heap);
					rescan_for_size_index(pnode->get_addr_index(m_addr_range), pnode->get_size_index(), size_db, node_heap);
				}
				if (merge_prev)
				{
					u32 const iprev = pnode->m_prev_addr;
					node_t*   pprev = (node_t*)node_heap->idx2ptr(iprev);

					u32 const prev_size_index = pprev->get_size_index();
					u32 const prev_addr_index = pprev->get_addr_index(m_addr_range);
					size_db->remove_size(prev_size_index, prev_addr_index);
					u32 const node_size_index = pnode->get_size_index();
					u32 const node_addr_index = pnode->get_addr_index(m_addr_range);
					size_db->remove_size(node_size_index, node_addr_index);

					u32 const node_size = pnode->get_size();
					u32 const prev_size = pprev->get_size() + node_size;
					pprev->set_size(prev_size, size_cfg.size_to_index(prev_size));

					remove_node(inode, pnode, node_heap); // we can safely remove 'prev' from the address db
					node_heap->deallocate(pnode);

					rescan_for_size_index(prev_addr_index, prev_size_index, size_db, node_heap);
					rescan_for_size_index(node_addr_index, node_size_index, size_db, node_heap);
					rescan_for_size_index(pprev->get_addr_index(m_addr_range), pprev->get_size_index(), size_db, node_heap);
				}
			}
        }

        void xaddr_db::remove_node(u32 inode, node_t* pnode, xdexer* dexer)
        {
            u32 const i = pnode->get_addr_index(m_addr_range);
            if (m_nodes[i] == inode)
            {
                u32 const inext = pnode->m_next_addr;
                node_t*   pnext = (node_t*)dexer->idx2ptr(inext);
                if (pnext->get_addr_index(m_addr_range) == i)
                {
                    m_nodes[i] = inext;
                }
                else
                {
                    m_nodes[i] = node_t::NIL;
                }
            }

            // Remove node from the address list
            u32 const iprev    = pnode->m_prev_addr;
            u32 const inext    = pnode->m_next_addr;
            node_t*   pprev    = (node_t*)dexer->idx2ptr(iprev);
            node_t*   pnext    = (node_t*)dexer->idx2ptr(inext);
            pprev->m_next_addr = inext;
            pnext->m_prev_addr = iprev;
            pnode->m_prev_addr = node_t::NIL;
            pnode->m_next_addr = node_t::NIL;
        }

        node_t* xaddr_db::get_node_with_addr(u32 const i, xdexer* dexer, u32 addr)
        {
            u32 inode = m_nodes[i];
            if (inode != node_t::NIL)
            {
                node_t* pnode = (node_t*)dexer->idx2ptr(inode);
                do
                {
                    if (pnode->is_used() && pnode->get_addr() == addr)
                    {
                        return pnode;
                    }
                    inode = pnode->m_next_addr;
                    pnode = (node_t*)dexer->idx2ptr(inode);
                } while (pnode->get_addr_index(m_addr_range) == i);
            }
            return nullptr;
        }

        node_t* xaddr_db::get_node_with_size_index(u32 const i, xdexer* dexer, u32 size_index)
        {
            u32 inode = m_nodes[i];
            if (inode != node_t::NIL)
            {
                node_t* pnode = (node_t*)dexer->idx2ptr(inode);
                do
                {
                    if (!pnode->is_used() && pnode->get_size_index() == size_index)
                    {
                        return pnode;
                    }
                    inode = pnode->m_next_addr;
                    pnode = (node_t*)dexer->idx2ptr(inode);
                } while (pnode->get_addr_index(m_addr_range) == i);
            }
            return nullptr;
        }

        bool xaddr_db::has_size_index(u32 const i, xdexer* dexer, u32 size_index) const
        {
            u32 inode = m_nodes[i];
            if (inode != node_t::NIL)
            {
                node_t* pnode = (node_t*)dexer->idx2ptr(inode);
                do
                {
                    if (pnode->is_used() == false && pnode->get_size_index() == size_index)
                    {
                        return true;
                    }
                    inode = pnode->m_next_addr;
                    pnode = (node_t*)dexer->idx2ptr(inode);
                } while (pnode->get_addr_index(m_addr_range) == i);
            }
            return false;
        }

        u32 xaddr_db::count_size_index(u32 const i, xdexer* dexer, u32 size_index) const
        {
            u32 count = 0;
            u32 inode = m_nodes[i];
            if (inode != node_t::NIL)
            {
                node_t* pnode = (node_t*)dexer->idx2ptr(inode);
                do
                {
                    if (pnode->is_used() == false && pnode->get_size_index() == size_index)
                    {
                        count += 1;
                    }
                    inode = pnode->m_next_addr;
                    pnode = (node_t*)dexer->idx2ptr(inode);
                } while (pnode->get_addr_index(m_addr_range) == i);
            }
            return count;
        }


		void xsize_db::initialize(xalloc* allocator, u32 sizecnt, u32 addrcnt)
		{
			m_size_cnt = sizecnt;
			m_size_db1_cnt = (addrcnt + (DB1_WIDTH-1)) / DB1_WIDTH;
			m_size_db0_cnt = (m_size_db1_cnt + (DB0_WIDTH-1)) / DB0_WIDTH;
			m_size_db0 = (u64*)allocator->allocate(m_size_cnt * m_size_db0_cnt * (DB0_WIDTH/8), sizeof(void*));
			m_size_db1 = (u64*)allocator->allocate(m_size_cnt * m_size_db1_cnt * (DB1_WIDTH/8), sizeof(void*));
			reset();
		}

		void xsize_db::release(xalloc* allocator)
		{
			allocator->deallocate(m_size_db0);
			allocator->deallocate(m_size_db1);
		}

		void xsize_db::reset()
		{
			for (u32 i = 0; i < 4; ++i)
				m_size_occ[i] = 0;
			for (u16 i = 0; i < (m_size_cnt * m_size_db0_cnt); ++i)
				m_size_db0[i] = 0;
			for (u32 i = 0; i < (m_size_cnt * m_size_db1_cnt); ++i)
				m_size_db1[i] = 0;
		}

		void xsize_db::remove_size(u32 size_index, u32 addr_index)
		{
			ASSERT(size_index < m_size_cnt);
			ASSERT(addr_index < (m_size_db1_cnt * 64));
			u32 const awi    = addr_index / DB1_WIDTH;
			u32 const abi    = addr_index & (DB1_WIDTH - 1);
			u32 const ssi    = size_index;
			u32 const sdbi   = (ssi * m_size_db1_cnt) + awi;
			m_size_db1[sdbi] = m_size_db1[sdbi] & ~((u64)1 << abi);
			if (m_size_db1[sdbi] == 0)
			{
				m_size_db0[ssi] = m_size_db0[ssi] & ~((u64)1 << awi);
				if (m_size_db0[ssi] == 0)
				{
					// Also clear size occupancy
					u32 const owi    = ssi / WIDTH_OCC;
					u32 const obi    = ssi & (WIDTH_OCC - 1);
					ASSERT(owi < 4);
					m_size_occ[owi] = m_size_occ[owi] & ~((u64)1 << obi);
				}
			}
		}

		void xsize_db::insert_size(u32 size_index, u32 addr_index)
		{
			ASSERT(size_index < m_size_cnt);
			ASSERT(addr_index < (m_size_db1_cnt * 64));
			u32 const awi    = addr_index / DB1_WIDTH;
			u32 const abi    = addr_index & (DB1_WIDTH - 1);
			u32 const ssi    = size_index;
			u32 const sdbi   = (ssi * m_size_db1_cnt) + awi;
			u64 const osbi   = (m_size_db1[sdbi] & ((u64)1 << abi));
			m_size_db1[sdbi] = m_size_db1[sdbi] | ((u64)1 << abi);
			if (osbi == 0)
			{
				u64 const osb0  = m_size_db0[ssi];
				m_size_db0[ssi] = m_size_db0[ssi] | ((u64)1 << awi);
				if (osb0 == 0)
				{
					// Also set size occupancy
					u32 const owi    = ssi / WIDTH_OCC;
					u32 const obi    = ssi & (WIDTH_OCC - 1);
					ASSERT(owi < 4);
					m_size_occ[owi] = m_size_occ[owi] | ((u64)1 << obi);
				}
			}
		}

		// Returns the addr node index that has a node with a best-fit 'size'
		bool xsize_db::find_size(u32& size_index, u32& addr_index) const
		{
			ASSERT(size_index < m_size_cnt);
			u32 owi = size_index / WIDTH_OCC;
			u32 obi = size_index & (WIDTH_OCC - 1);
			u64 obm = ~(((u64)1 << obi) - 1);
			u32 ssi = 0;
			if ((m_size_occ[owi] & obm) != 0)
			{
				ssi = (owi * WIDTH_OCC) + xfindFirstBit(m_size_occ[owi] & obm);
			}
			else
			{
				do
				{
					owi += 1;
				} while (owi < 4 && m_size_occ[owi] == 0);

				if (owi == 4)
					return false;

				ASSERT(owi < 4);
				ssi = (owi * WIDTH_OCC) + xfindFirstBit(m_size_occ[owi]);
			}

			size_index     = ssi;                                 // communicate back the size index we need to search for
			u32 const sdb0 = (u32)xfindFirstBit(m_size_db0[ssi]); // get the addr node that has that size
			u32 const sdbi = (ssi * m_size_db1_cnt) + sdb0;
			u32 const adbi = (u32)((sdb0 * DB1_WIDTH) + xfindFirstBit(m_size_db1[sdbi]));
			addr_index = adbi;
			return true;
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
