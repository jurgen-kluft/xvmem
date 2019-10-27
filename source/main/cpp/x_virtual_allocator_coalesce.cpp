#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/x_virtual_allocator.h"
#include "xvmem/x_virtual_pages.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    struct naddr_t
    {
        u32 m_addr;  // [Allocated, Free, Locked] + (m_addr * size step) + base addr
        u32 m_nsize; // size node index (for coalesce)
        u32 m_prev;  // previous node in memory, can be free, can be allocated
        u32 m_next;  // next node in memory, can be free, can be allocated
    };

    struct nsize_t
    {
        u32 m_size;  // cached size
        u32 m_naddr; // the address node that has this size
        u32 m_prev;  // either in the allocation slot array as a list node
        u32 m_next;  // or in the size slot array as a list node
    };

    class xcoalesce : public xalloc
    {
    public:
        xcoalesce(void* mem_addr, u64 mem_size, xfsadexed* node_alloc);

        void initialize(u32 alloc_size_min, u32 alloc_size_max, u32 alloc_size_step);

        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* addr);

    protected:
        xalloc*    m_internal_heap;
        xfsadexed* m_node_alloc; // For allocating naddr_t and nsize_t nodes
        void*      m_memory_addr;
        u64        m_memory_size;
        u32        m_alloc_size_min;
        u32        m_alloc_size_max;
        u32        m_alloc_size_step;
        u32*       m_size_nodes;
        u32*       m_size_nodes_occupancy;
        u32*       m_addr_nodes;
    };

    class xvmem_allocator_coalesce : public xalloc
    {
    public:
        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);

        virtual void release();
    };

}; // namespace xcore
