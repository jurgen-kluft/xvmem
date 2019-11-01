#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_hibitset.h"

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

	class xcoalescee : public xalloc
	{
	public:
		xcoalescee();

		void          initialize(xalloc* main_heap, xfsadexed* node_heap, void* mem_addr, u64 mem_size, u32 size_min, u32 size_max, u32 size_step);
		virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);
        xalloc*       m_main_heap;
        xfsadexed*    m_node_heap;
        void*         m_memory_addr;
        u64           m_memory_size;
        u32           m_alloc_size_min;
        u32           m_alloc_size_max;
        u32           m_alloc_size_step;
        u32*          m_size_nodes;
        u32*          m_size_nodes_occupancy_data;
		xhibitset     m_size_nodes_occupancy;
        u32*          m_addr_nodes;
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
        , m_size_nodes_occupancy_data(nullptr)
        , m_addr_nodes(nullptr)
	{
	}

	void  xcoalescee::initialize(xalloc* main_heap, xfsadexed* node_heap, void* mem_addr, u64 mem_size, u32 size_min, u32 size_max, u32 size_step)
	{
		m_main_heap = main_heap;
        m_node_heap = node_heap;
        m_memory_addr = mem_addr;
        m_memory_size = mem_size;
        m_alloc_size_min = size_min;
        m_alloc_size_max = size_max;
        m_alloc_size_step = size_step;

		s32 const num_sizes = (size_max - size_min) / size_step;
        m_size_nodes = (u32*)main_heap->allocate(num_sizes * sizeof(u32), sizeof(void*));
		x_memset(m_size_nodes, 0, num_sizes * sizeof(u32));
        m_size_nodes_occupancy_data = (u32*)main_heap->allocate(((num_sizes + 31) / 32) * sizeof(u32), sizeof(void*));
		x_memset(m_size_nodes_occupancy_data, 0, ((num_sizes + 63) / 64) * sizeof(u64));

		// Please keep the number of addr slots low
		ASSERT((mem_size / (u64)(size_min * 16)) < (u64)64*1024);
		s32 const num_addrs = (s32)(mem_size / (size_min * 16));
        m_addr_nodes = (u32*)main_heap->allocate(num_addrs * sizeof(u32), sizeof(void*));
		x_memset(m_addr_nodes, 0, num_addrs * sizeof(u32));
	}

	void*	xcoalescee::allocate(u32 size, u32 alignment)
	{
		// Find 'good-fit' [size, alignment]
		if (size < m_alloc_size_min)
			size = m_alloc_size_min;
		size = (size + (m_alloc_size_step-1)) & ~(m_alloc_size_step-1);
		u32 size_slot = (size - m_alloc_size_min) / m_alloc_size_step;

		// Determine addr node
		// See if we have a split-off part, if so insert an addr node 
		//   after the current that is marked as 'Free' and also add it 
		//   to the size DB
		// Mark the current addr node as 'Allocated'
		// Add the current addr node in the addr DB
		// Return the address
		
		return nullptr;
	}

	void	xcoalescee::deallocate(void* p)
	{
		// Calculate the slot in the addr DB
		// Iterate through the list at that slot until we find 'p'
		// Remove the item from the list

		// Determine the 'prev' and 'next' of the current addr node
		// If 'prev' is marked as 'Free' then coalesce (merge) with it
		// Remove the size node that belonged to 'prev' from the size DB
		// If 'next' is marked as 'Free' then coalesce (merge) with it
		// Remove the size node that belonged to 'next' from the size DB
		// Build the size node with the correct size and reference the 'merged' addr node
		// Add the size node to the size DB
		// Done
	}
    class xvmem_allocator_coalesce : public xalloc
    {
    public:
        xvmem_allocator_coalesce()
			: m_internal_heap(nullptr)
			, m_node_alloc(nullptr)
			, m_memory_addr(nullptr)
			, m_memory_size(0)
			, m_alloc_size_min(0)
			, m_alloc_size_max(0)
			, m_alloc_size_step(0)
			, m_size_nodes(nullptr)
			, m_size_nodes_occupancy(nullptr)
			, m_addr_nodes(nullptr)
		{
		}

        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);
        virtual void release();

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

	void*	xvmem_allocator_coalesce::allocate(u32 size, u32 alignment)
	{
		return nullptr;
	}

	void	xvmem_allocator_coalesce::deallocate(void* p)
	{
	}

	void	xvmem_allocator_coalesce::release()
	{

	}

	xalloc*		gCreateVMemCoalesceAllocator(xalloc* internal_heap, xfsadexed* node_alloc, xvmem* vmem, u64 mem_size, u32 alloc_size_min, u32 alloc_size_max, u32 alloc_size_step, u32 alloc_addr_list_size)
	{
		return nullptr;
	}

}; // namespace xcore
