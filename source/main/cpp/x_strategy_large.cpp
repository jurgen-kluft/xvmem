#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_doubly_linked_list.h"
#include "xvmem/private/x_strategy_large.h"

namespace xcore
{
    class xalloc_large : public xalloc
    {
    public:
        virtual void* v_allocate(u32 size, u32 alignment) X_FINAL;
        virtual u32   v_deallocate(void* ptr) X_FINAL;
        virtual void  v_release();

        struct entry_t
        {
            XCORE_CLASS_PLACEMENT_NEW_DELETE
            u32 m_index;
            u32 m_size;
        };

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        xalloc*           m_main_heap;
        void*             m_mem_base;
        u64               m_mem_range;
        u64               m_block_size;
        u32               m_entry_max;
        xalist_t          m_alloc_list;
        xalist_t          m_free_list;
        xalist_t::node_t* m_entry_nodes;
        entry_t*          m_entry_array;
    };

    static inline void* advance_ptr(void* ptr, u64 size) { return (void*)((uptr)ptr + size); }

    xalloc* create_alloc_large(xalloc* main_heap, void* mem_addr, u64 mem_size, u32 max_num_allocs)
    {
        xalloc_large* instance   = main_heap->construct<xalloc_large>();
        u64 const     block_size = mem_size / max_num_allocs;
        u32 const     block_cnt  = max_num_allocs;

        instance->m_main_heap  = main_heap;
        instance->m_mem_base   = mem_addr;
        instance->m_mem_range  = mem_size;
        instance->m_block_size = block_size;

        instance->m_entry_max   = block_cnt;
        instance->m_entry_nodes = (xalist_t::node_t*)main_heap->allocate(sizeof(xalist_t::node_t) * block_cnt, sizeof(void*));
        instance->m_entry_array = (xalloc_large::entry_t*)main_heap->allocate(sizeof(xalloc_large::entry_t) * block_cnt, sizeof(void*));
        instance->m_alloc_list  = xalist_t();
        instance->m_free_list.initialize(instance->m_entry_nodes, block_cnt);
        for (u32 i = 0; i < block_cnt; ++i)
        {
            instance->m_entry_array[i].m_size  = (u32)block_size;
            instance->m_entry_array[i].m_index = i;
        }
        return instance;
    }

    void xalloc_large::v_release()
    {
        m_main_heap->deallocate(m_entry_nodes);
        m_main_heap->deallocate(m_entry_array);
        m_main_heap->deallocate(this);
    }

    void* xalloc_large::v_allocate(u32 size, u32 alignment)
    {
        if (!m_free_list.is_empty())
        {
            const u32 idx = m_free_list.remove_headi(m_entry_nodes);
            m_alloc_list.insert(m_entry_nodes, idx);
            xalloc_large::entry_t* entry = &m_entry_array[idx];
            entry->m_size                = size;
            ASSERT(entry->m_index == idx);
            return advance_ptr(m_mem_base, entry->m_index * m_block_size);
        }
        return nullptr;
    }

    u32 xalloc_large::v_deallocate(void* ptr)
    {
        u32 const              idx   = (u32)(((u64)ptr - (u64)m_mem_base) / m_block_size);
        xalloc_large::entry_t* entry = &m_entry_array[idx];
        u32 const              size  = entry->m_size;
        entry->m_size                = (u32)m_block_size;
        ASSERT(entry->m_index == idx);
        m_alloc_list.remove_item(m_entry_nodes, idx);
        m_free_list.insert(m_entry_nodes, idx);
        return size;
    }

} // namespace xcore