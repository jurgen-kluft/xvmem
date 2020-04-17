#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"
#include "xbase/x_hibitset.h"

#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_strategy_coalesce_direct.h"

namespace xcore
{

    class xvmem_allocator_coalesce_direct : public xalloc
    {
    public:
        xvmem_allocator_coalesce_direct()
            : m_vmem(nullptr)
        {
        }

        virtual void* v_allocate(u32 size, u32 alignment);
        virtual void  v_deallocate(void* p);
        virtual void  v_release();

        xvmem* m_vmem;
        void*  m_memory_addr;
		u64 m_memory_range;
        xalloc* m_main_heap;
        void*                               m_s2m_mem_base;      // The memory base address, reserved
        u64                                 m_s2m_mem_range;     // 32 MB * 8 = 256 MB
		u32 m_s2m_min_size;
		u32 m_s2m_max_size;
        xcoalescestrat_direct::xinstance_t* m_s2m_allocators[8]; // Small/Medium
        void*                               m_m2l_mem_base;      // The memory base address, reserved
        u64                                 m_m2l_mem_range;     // 64 MB * 4 = 256 MB
		u32 m_m2l_min_size;
		u32 m_m2l_max_size;
        xcoalescestrat_direct::xinstance_t* m_m2l_allocators[4]; // Medium/Large


        XCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    void* xvmem_allocator_coalesce_direct::v_allocate(u32 size, u32 alignment)
	{
		if (size > m_s2m_min_size && size < m_s2m_max_size)
		{
			// Which one still has a free size for this request ?

		}				
		else if (size > m_m2l_min_size && size < m_m2l_max_size)
		{
		}
		else
		{
			return nullptr;
		}
	}

    void  xvmem_allocator_coalesce_direct::v_deallocate(void* p) { xcoalescestrat_direct::deallocate(m_coalescee, p); }

    void xvmem_allocator_coalesce_direct::v_release()
    {
        m_vmem->release(m_memory_addr);
        xcoalescestrat_direct::destroy(m_coalescee, m_main_heap);
    }

    static inline void* advance_ptr(void* ptr, u64 size) { return (void*)((uptr)ptr + size); }

    xalloc* gCreateVMemCoalesceBasedAllocator(xalloc* main_heap, xfsadexed* node_heap, xvmem* vmem, u64 mem_size, u32 alloc_size_min, u32 alloc_size_max, u32 alloc_size_step)
    {
        xvmem_allocator_coalesce_direct* allocator = main_heap->construct<xvmem_allocator_coalesce_direct>();

        void* memory_addr = nullptr;
        u32   page_size;
        u32   attr = 0;
        vmem->reserve(mem_size, page_size, attr, memory_addr);

        allocator->m_vmem         = vmem;
        allocator->m_memory_addr  = memory_addr;
		allocator->m_memory_range = mem_size;

		allocator->m_s2m_mem_base   = allocator->m_memory_addr;
		allocator->m_s2m_mem_range  = (u64)8 * 32 * 1024 * 1024;
		for (s32 i=0; i<8; ++i)
		{
			allocator->m_s2m_allocators[i] = xcoalescestrat_direct::create_4KB_64KB_256B_32MB(main_heap, node_heap);
		}
		allocator->m_m2l_mem_base  = advance_ptr(allocator->m_s2m_mem_base, allocator->m_s2m_mem_range);
		allocator->m_m2l_mem_range = (u64)4 * 64 * 1024 * 1024;
		for (s32 i=0; i<4; ++i)
		{
			allocator->m_m2l_allocators[i] = xcoalescestrat_direct::create_64KB_512KB_2KB_64MB(main_heap, node_heap);
		}

        allocator->m_coalescee   = xcoalescestrat_direct::create(main_heap, memory_addr, mem_size, alloc_size_min, alloc_size_max, alloc_size_step);

        return allocator;
    }

}; // namespace xcore
