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

        // NOTE: This node should be exactly 16 bytes.
        struct node_t
        {
            static u32 const NIL       = 0xffffffff;
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

			XCORE_CLASS_PLACEMENT_NEW_DELETE

            inline u32  get_addr() const { return (u32)(m_addr & 0x7ffffff); }
            inline void set_addr(u32 addr) { m_addr = (m_addr & 0x80000000) | (addr & 0x7fffffff); }
            inline u32  get_addr_index() const { return m_addr / (32 * 1024 * (1024 / 256)); }
            inline void set_size(u32 size, u8 size_index) { m_size = (size_index << 24) | (size >> 10); }
            inline u32  get_size() const { return (m_size & 0x00FFFFFF) << 10; }
            inline u32  get_size_index() const { return ((m_size >> 24) & 0x7F); }
            inline void set_used(bool used) { m_addr = m_addr | FLAG_USED; }
            inline bool is_used() const { return (m_addr & FLAG_MASK) == FLAG_USED; }
            inline bool is_free() const { return (m_addr & FLAG_MASK) == 0; }
        };

        struct xsize_cfg
        {
            u8 size_to_index(u32 size) const
            {
                ASSERT(size > m_alloc_size_min);
                if (size >= m_alloc_size_max)
                    return (u8)127;
                u32 const slot = (size - m_alloc_size_min - m_alloc_size_step) / m_alloc_size_step;
                ASSERT(slot < 128);
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

            u32 m_alloc_size_min;
            u32 m_alloc_size_max;
            u32 m_alloc_size_step;
        };

        // 128 different sizes
        // size db0 = 256 B
        // size db1 = 4 KB
        struct xsize_db
        {
            u64  m_occupancy[2]; // u64[2] (128 bits)
            u16* m_size_db0;     // u16[128]
            u16* m_size_db1;     // u16[128 * 16]

            void reset()
            {
                m_occupancy[0] = 0;
                m_occupancy[1] = 0;
                for (s32 i = 0; i < 128; ++i)
                    m_size_db0[i] = 0;
                for (s32 i = 0; i < (128 * 16); ++i)
                    m_size_db1[i] = 0;
            }

            void remove_size(u32 size_index, u16 addr_index)
            {
                u16 const awi    = addr_index / 16;
                u16 const abi    = addr_index & (16 - 1);
                u32 const ssi    = size_index;
                u32 const sdbi   = (ssi * 16) + awi;
                m_size_db1[sdbi] = m_size_db1[sdbi] & ~(1 << abi);
                if (m_size_db1[sdbi] == 0)
                {
                    m_size_db0[ssi] = m_size_db0[ssi] & ~(1 << awi);
                    if (m_size_db0[ssi] == 0)
                    {
                        // Also clear size occupancy
                        u32 const owi    = ssi / 64;
                        u32 const obi    = ssi & (64 - 1);
                        m_occupancy[owi] = m_occupancy[owi] & ~(1 << obi);
                    }
                }
            }

            void insert_size(u32 size_index, u16 addr_index)
            {
                u16 const awi    = addr_index / 16;
                u16 const abi    = addr_index & (16 - 1);
                u32 const ssi    = size_index;
                u32 const sdbi   = (ssi * 16) + awi;
                u16 const osbi   = (m_size_db1[sdbi] & (1 << abi));
                m_size_db1[sdbi] = m_size_db1[sdbi] | (1 << abi);
                if (osbi == 0)
                {
                    //
                    u16 const osb0  = m_size_db0[ssi];
                    m_size_db0[ssi] = m_size_db0[ssi] | (1 << awi);
                    if (osb0 == 0)
                    {
                        // Also set size occupancy
                        u32 const owi    = ssi / 64;
                        u32 const obi    = ssi & (64 - 1);
                        m_occupancy[owi] = m_occupancy[owi] | ((u64)1 << obi);
                    }
                }
            }

            // Returns the addr node index that has a node with a best-fit 'size'
            u32 find_size(u32& size_index) const
            {
                u32 owi = size_index / 64;
                u64 obm = ~((1 << (size_index & (64 - 1))) - 1);
                u32 ssi = 0;
                if ((m_occupancy[owi] & obm) != 0)
                {
                    ssi = xfindFirstBit(m_occupancy[owi]);
                }
                else if (owi == 0)
                {
                    ssi = 64 + xfindFirstBit(m_occupancy[1]);
                }
                else
                {
                    return node_t::NIL;
                }

                size_index     = ssi;                            // communicate back the size index we need to search for
                u16 const sdb0 = (u16)xfindFirstBit(m_size_db0[ssi]); // get the addr node that has that size
                u16 const sdbi = (ssi * 16) + sdb0;
                u32 const adbi = (u32)((sdb0 * 16) + xfindFirstBit(m_size_db1[sdbi]));
                return adbi;
            }
        };

        // 256 nodes = 1KB
        struct xaddr_db
        {
            void reset()
            {
                for (s32 i = 0; i < 256; ++i)
                    m_nodes[i] = node_t::NIL;
            }

            void split_node(u32 inode, node_t* pnode, u32 size, xfsadexed* node_alloc, xsize_db& size_db, xsize_cfg const& size_cfg, u32& inew, node_t*& pnew)
            {
                u32       node_sidx = pnode->get_size_index();
                u32 const node_aidx = pnode->get_addr_index();
                size_db.remove_size(node_sidx, node_aidx);

                // we need to insert the new node between pnode and it's next node
                u32 const     inext = pnode->m_next_addr;
                node_t* const pnext = (node_t*)node_alloc->idx2ptr(inext);

                // create the new node
                pnew = (node_t*)node_alloc->allocate();
                pnew->init();
                inew = node_alloc->ptr2idx(pnew);

                // Set the new size on node and next
                u32 const node_addr = pnode->get_addr();
                u32 const new_addr  = node_addr + size;
                u32 const node_size = pnode->get_size() - size;
                pnode->set_size(node_size, size_cfg.size_to_index(node_size));
                pnew->set_size(size, size_cfg.size_to_index(size));
                pnew->set_addr(new_addr);

                // Link this new node into the address list
                pnew->m_prev_addr  = inode;
                pnew->m_next_addr  = inext;
                pnode->m_next_addr = inew;
                pnext->m_prev_addr = inew;

                // See if this new node is now part of a address node
                u32 const i = pnew->get_addr_index();
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

                // Check if node_aidx still has other nodes of the same size-index 'node-aidx'
                if (has_node_with_size_index(node_aidx, node_alloc, node_sidx))
                {
                    size_db.insert_size(node_sidx, node_aidx);
                }

                // node has been split and now has a different size, mark it in the size-db
                node_sidx = pnode->get_size_index();
                size_db.insert_size(node_sidx, node_aidx);

                // our new node has to be marked in the size-db
                u32 const new_aidx = pnew->get_addr_index();
                u32 const new_sidx = pnew->get_size_index();
                size_db.insert_size(new_sidx, new_aidx);
            }

            void rescan_size_for(u16 const addr_index, u8 const size_index, xsize_db& size_db, xdexer* dexer)
            {
                if (has_node_with_size_index(addr_index, dexer, size_index))
                {
                    size_db.insert_size(size_index, addr_index);
                }
            }

            void coalesce_node(u32 inode, node_t* pnode, bool merge_prev, bool merge_next, xsize_db& size_db, xsize_cfg const& size_cfg, xdexer* dexer)
            {
                if (merge_next)
                {
                    u32 const inext = pnode->m_next_addr;
                    node_t*   pnext = (node_t*)dexer->idx2ptr(inext);

                    u32 const node_size_index = pnode->get_size_index();
                    u32 const node_addr_index = pnode->get_addr_index();
                    size_db.remove_size(node_size_index, node_addr_index);
                    u32 const next_size_index = pnext->get_size_index();
                    u32 const next_addr_index = pnext->get_addr_index();
                    size_db.remove_size(next_size_index, next_addr_index);

                    u32 const next_size = pnext->get_size();
                    u32 const node_size = pnode->get_size() + next_size;
                    pnode->set_size(node_size, size_cfg.size_to_index(node_size));

                    remove_node(inext, pnext, dexer); // we can safely remove 'next' from the address db

                    rescan_size_for(node_addr_index, node_size_index, size_db, dexer);
                    rescan_size_for(next_addr_index, next_size_index, size_db, dexer);
                }
                if (merge_prev)
                {
                    u32 const iprev = pnode->m_prev_addr;
                    node_t*   pprev = (node_t*)dexer->idx2ptr(iprev);

                    u32 const prev_size_index = pprev->get_size_index();
                    u32 const prev_addr_index = pprev->get_addr_index();
                    size_db.remove_size(prev_size_index, prev_addr_index);
                    u32 const node_size_index = pnode->get_size_index();
                    u32 const node_addr_index = pnode->get_addr_index();
                    size_db.remove_size(node_size_index, node_addr_index);

                    u32 const node_size = pnode->get_size();
                    u32 const prev_size = pprev->get_size() + node_size;
                    pprev->set_size(prev_size, size_cfg.size_to_index(prev_size));

                    remove_node(inode, pnode, dexer); // we can safely remove 'prev' from the address db

                    rescan_size_for(prev_addr_index, prev_size_index, size_db, dexer);
                    rescan_size_for(node_addr_index, node_size_index, size_db, dexer);
                }
            }

            void remove_node(u32 inode, node_t* pnode, xdexer* dexer)
            {
                u32 const i = pnode->get_addr_index();
                if (m_nodes[i] == inode)
                {
                    u32 const inext = pnode->m_next_addr;
                    node_t*   pnext = (node_t*)dexer->idx2ptr(inext);
                    if (pnext->get_addr_index() == i)
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

            node_t* get_node_with_addr(u16 const i, xdexer* dexer, u32 addr)
            {
                u16 const ihead = m_nodes[i];
                u16       inode = ihead;
                do
                {
                    node_t* pnode = (node_t*)dexer->idx2ptr(inode);
                    if (pnode->is_used() && pnode->get_addr() == addr)
                    {
                        return pnode;
                    }
                    inode = pnode->m_next_addr;
                } while (inode != ihead);
                return nullptr;
            }

            node_t* get_node_with_size_index(u16 const i, xdexer* dexer, u32 size_index)
            {
                u16     inode = m_nodes[i];
                node_t* pnode = (node_t*)dexer->idx2ptr(inode);
                do
                {
                    if (!pnode->is_used() && pnode->get_size_index() == size_index)
                    {
                        return pnode;
                    }
                    inode = pnode->m_next_addr;
                    pnode = (node_t*)dexer->idx2ptr(inode);
                } while (pnode->get_addr_index() == i);
                return nullptr;
            }

            bool has_node_with_size_index(u16 const i, xdexer* dexer, u32 size_index) const
            {
                u16     inode = m_nodes[i];
                node_t* pnode = (node_t*)dexer->idx2ptr(inode);
                do
                {
                    if (pnode->is_used() == false && pnode->get_size_index() == size_index)
                    {
                        return true;
                    }
                    inode = pnode->m_next_addr;
                    pnode = (node_t*)dexer->idx2ptr(inode);
                } while (pnode->get_addr_index() == i);
                return false;
            }

            u32* m_nodes;
        };

        struct xinstance_t
        {
            // Main API
            void* allocate(u32 size, u32 alignment);
            u32   deallocate(void* p);

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
            xfsadexed* m_node_heap;

            // Local variables
            xsize_cfg m_size_cfg;
            xsize_db  m_size_db;
            xaddr_db  m_addr_db;
        };

        void* xinstance_t::allocate(u32 size, u32 alignment)
        {
            size = m_size_cfg.align_size(size);

            u32       size_index = m_size_cfg.size_to_index(size);
            u16 const addr_index = m_size_db.find_size(size_index);

            node_t*   pnode = m_addr_db.get_node_with_size_index(addr_index, m_node_heap, size_index);
            u32 const inode = node2idx2(pnode);

            // Should we split the node?
            if (size <= pnode->get_size() && ((pnode->get_size() - size) >= m_size_cfg.m_alloc_size_min))
            {
                u32     inew = node_t::NIL;
                node_t* pnew = nullptr;
                m_addr_db.split_node(inode, pnode, size, m_node_heap, m_size_db, m_size_cfg, inew, pnew);
            }

            // Mark the current addr node as 'used'
            ASSERT(pnode->is_free());
            pnode->set_used(true);

            return (void*)((uptr)pnode->get_addr() - 1024);
        }

        u32 xinstance_t::deallocate(void* p)
        {
            u32 const addr       = (u32)(uptr)p;
            u32 const addr_index = (addr + 1024) / (32 * 1024 * (1024 / 256));

            node_t*   pnode = m_addr_db.get_node_with_addr(addr_index, m_node_heap, addr);
            u32       inode = m_node_heap->ptr2idx(pnode);
            u32 const size  = pnode->get_size();

            // Coalesce
            u32        inode_prev = pnode->m_prev_addr;
            u32        inode_next = pnode->m_next_addr;
            node_t*    pnode_prev = idx2node(inode_prev);
            node_t*    pnode_next = idx2node(inode_next);
            bool const merge_prev = pnode_prev->is_free();
            bool const merge_next = pnode_next->is_free();
            m_addr_db.coalesce_node(inode, pnode, merge_prev, merge_next, m_size_db, m_size_cfg, m_node_heap);

            return size;
        }

        xinstance_t* create(xalloc* main_heap, xfsadexed* node_heap, u32 size_min, u32 size_step)
        {
            xinstance_t* instance = (xinstance_t*)main_heap->allocate(sizeof(xinstance_t));

            instance->m_node_heap = node_heap;

            instance->m_size_cfg.m_alloc_size_min  = size_min;
            instance->m_size_cfg.m_alloc_size_max  = size_min + (256 * size_step);
            instance->m_size_cfg.m_alloc_size_step = size_step;

            instance->m_size_db.m_size_db0 = (u16*)main_heap->allocate(128 * sizeof(u16), sizeof(void*));
            instance->m_size_db.m_size_db1 = (u16*)main_heap->allocate(128 * 16 * sizeof(u16), sizeof(void*));
            instance->m_size_db.reset();

            instance->m_addr_db.m_nodes = (u32*)main_heap->allocate(256 * sizeof(u32), sizeof(void*));
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
            head_node->set_used(true);
            head_node->set_addr(1024);
            head_node->set_size(1024, 0);
            tail_node->set_used(true);
            tail_node->set_addr(32 * 1024 * 1024 + 1024);
            tail_node->set_size(1024, 0);
            main_node->set_addr(1024);
            main_node->set_size(32 * 1024 * 1024, 127);
            main_node->m_prev_addr = head_inode;
            main_node->m_next_addr = tail_inode;
            head_node->m_next_addr = main_inode;
            tail_node->m_prev_addr = main_inode;

			instance->m_size_db.insert_size(main_node->get_size_index(), 0);
            instance->m_addr_db.m_nodes[0] = head_inode;

			return instance;
        }

        void destroy(xinstance_t* instance, xalloc* main_heap)
        {
            u32     inode = instance->m_addr_db.m_nodes[0]; // Should be 'head' node
            node_t* pnode = instance->idx2node(inode);
            while (pnode != nullptr)
            {
                u32 const inext = pnode->m_next_addr;
                instance->dealloc_node(inode, pnode);
                inode = inext;
                pnode = instance->idx2node(inode);
            }

            main_heap->deallocate(instance->m_size_db.m_size_db0);
            main_heap->deallocate(instance->m_size_db.m_size_db1);
            main_heap->deallocate(instance->m_addr_db.m_nodes);
            main_heap->deallocate(instance);
        }

        void* allocate(xinstance_t* instance, u32 size, u32 alignment) { return instance->allocate(size, alignment); }
        u32   deallocate(xinstance_t* instance, void* ptr) { return instance->deallocate(ptr); }

    } // namespace xcoalescestrat_direct

    using namespace xcoalescestrat_direct;

    class xvmem_allocator_coalesce_direct : public xalloc
    {
    public:
        xvmem_allocator_coalesce_direct()
            : m_internal_heap(nullptr)
            , m_node_alloc(nullptr)
        {
        }

        virtual void* v_allocate(u32 size, u32 alignment);
        virtual void  v_deallocate(void* p);
        virtual void  v_release();

		XCORE_CLASS_PLACEMENT_NEW_DELETE

        xalloc*      m_internal_heap;
        xfsadexed*   m_node_alloc; // For allocating node_t and nsize_t nodes
        xvmem*       m_vmem;
        xinstance_t* m_coalescee;
    };

    void* xvmem_allocator_coalesce_direct::v_allocate(u32 size, u32 alignment)
    {
        void* ptr = xcoalescestrat_direct::allocate(m_coalescee, size, alignment);

        return ptr;
    }

    void xvmem_allocator_coalesce_direct::v_deallocate(void* p) { xcoalescestrat_direct::deallocate(m_coalescee, p); }

    void xvmem_allocator_coalesce_direct::v_release()
    {
        destroy(m_coalescee, m_internal_heap);

        // release virtual memory

        m_internal_heap->destruct<>(this);
    }

    xalloc* gCreateVMemCoalesceDirectAllocator(xalloc* internal_heap, xfsadexed* node_alloc, xvmem* vmem, u64 mem_size, u32 alloc_size_min, u32 alloc_size_step)
    {
        xvmem_allocator_coalesce_direct* allocator = internal_heap->construct<xvmem_allocator_coalesce_direct>();
        allocator->m_internal_heap          = internal_heap;
        allocator->m_node_alloc             = node_alloc;
        allocator->m_vmem                   = vmem;

        void* memory_addr = nullptr;
        u32   page_size;
        u32   attr = 0;
        vmem->reserve(mem_size, page_size, attr, memory_addr);

        allocator->m_coalescee = create(internal_heap, node_alloc, alloc_size_min, alloc_size_step);

        return allocator;
    }
}; // namespace xcore
