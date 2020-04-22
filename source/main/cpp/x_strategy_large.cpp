#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_large.h"

namespace xcore
{
    static const u32 INDEX32_NIL = 0xffffffff;

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
            u32 m_next;
            u32 m_prev;
        };

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        xalloc*  m_main_heap;
        void*    m_mem_base;
        u64      m_mem_range;
        u64      m_block_size;
        u32      m_free_list;
        u32      m_alloc_list;
        u32      m_entry_capacity;
        entry_t* m_entry_array;
    };

    static inline xalloc_large::entry_t* helper_idx2entry(xalloc_large* self, u32 const i)
    {
        if (i == INDEX32_NIL)
            return nullptr;
        return &self->m_entry_array[i];
    }

    static inline u32 helper_entry2idx(xalloc_large* self, xalloc_large::entry_t* entry)
    {
        if (entry == nullptr)
            return INDEX32_NIL;
        u64 const idx = (entry - &self->m_entry_array[0]) / sizeof(xalloc_large::entry_t);
        return (u32)idx;
    }

    static inline xalloc_large::entry_t* helper_entry_next(xalloc_large* self, u32 i)
    {
        xalloc_large::entry_t* entry = helper_idx2entry(self, i);
        return helper_idx2entry(self, entry->m_next);
    }

    static inline xalloc_large::entry_t* helper_list_remove(xalloc_large* self, u32& head, u32 i)
    {
        xalloc_large::entry_t* entry = helper_idx2entry(self, i);
        if (i == head)
        {
            head = entry->m_next;
        }

        xalloc_large::entry_t* next = helper_idx2entry(self, entry->m_next);
        xalloc_large::entry_t* prev = helper_idx2entry(self, entry->m_prev);
        if (next != nullptr)
        {
            next->m_prev = entry->m_prev;
        }
        if (prev != nullptr)
        {
            prev->m_next = entry->m_next;
        }

        entry->m_next = INDEX32_NIL;
        entry->m_prev = INDEX32_NIL;
        return entry;
    }

    static inline void helper_list_insert(xalloc_large* self, u32& head, u32 i)
    {
        xalloc_large::entry_t* entry = helper_idx2entry(self, i);
        entry->m_next                = head;
        head                         = i;
    }

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
        instance->m_alloc_list = INDEX32_NIL;
        instance->m_free_list  = 0;

        instance->m_entry_capacity = block_cnt;
        instance->m_entry_array    = (xalloc_large::entry_t*)main_heap->allocate(sizeof(xalloc_large::entry_t) * block_cnt, sizeof(void*));
        for (u32 i = 0; i < block_cnt; ++i)
        {
            instance->m_entry_array[i].m_size  = (u32)block_size;
            instance->m_entry_array[i].m_prev  = i - 1;
            instance->m_entry_array[i].m_next  = i + 1;
            instance->m_entry_array[i].m_index = i;
        }
        instance->m_entry_array[block_cnt - 1].m_next = INDEX32_NIL;
        instance->m_entry_array[0].m_prev             = INDEX32_NIL;

        return instance;
    }

    void xalloc_large::v_release()
    {
        m_main_heap->deallocate(m_entry_array);
        m_main_heap->deallocate(this);
    }

    void* xalloc_large::v_allocate(u32 size, u32 alignment)
    {
        if (m_free_list != INDEX32_NIL)
        {
            const u32 idx = m_free_list;
            helper_list_remove(this, m_free_list, idx);
            helper_list_insert(this, m_alloc_list, idx);
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
        xalloc_large::entry_t* entry = helper_idx2entry(this, idx);
        u32 const              size  = entry->m_size;
        entry->m_size                = (u32)m_block_size;
        helper_list_remove(this, m_alloc_list, idx);
        helper_list_insert(this, m_free_list, idx);
        return size;
    }

} // namespace xcore