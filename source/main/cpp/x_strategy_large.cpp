#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_large.h"

namespace xcore
{
    static const u32 INDEX32_NIL = 0xffffffff;

    namespace xlargestrat
    {
        struct xinstance_t
        {
            struct entry_t
            {
                entry_t()
                    : m_address(nullptr)
                    , m_size(0)
                    , m_next(INDEX32_NIL)
                    , m_prev(INDEX32_NIL)
                {
                }

				XCORE_CLASS_PLACEMENT_NEW_DELETE

				void* m_address;
                u64   m_size;
                u32   m_next;
                u32   m_prev;
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

        static inline xinstance_t::entry_t* helper_idx2entry(xinstance_t* self, u32 const i)
        {
            if (i == INDEX32_NIL)
                return nullptr;
            return &self->m_entry_array[i];
        }

        static inline u32 helper_entry2idx(xinstance_t* self, xinstance_t::entry_t* entry)
        {
            if (entry == nullptr)
                return INDEX32_NIL;
            u64 const idx = (entry - &self->m_entry_array[0]) / sizeof(xinstance_t::entry_t);
            return (u32)idx;
        }

        static inline xinstance_t::entry_t* helper_entry_next(xinstance_t* self, u32 i)
        {
            xinstance_t::entry_t* entry = helper_idx2entry(self, i);
            return helper_idx2entry(self, entry->m_next);
        }

        static inline xinstance_t::entry_t* helper_list_remove(xinstance_t* self, u32& head, u32 i)
        {
            xinstance_t::entry_t* entry = helper_idx2entry(self, i);
            if (i == head)
            {
                head          = entry->m_next;
                entry->m_prev = INDEX32_NIL;
            }
            else
            {
                xinstance_t::entry_t* next = helper_idx2entry(self, entry->m_next);
                xinstance_t::entry_t* prev = helper_idx2entry(self, entry->m_prev);
                if (next != nullptr)
                {
                    next->m_prev = entry->m_prev;
                }
                if (prev != nullptr)
                {
                    prev->m_next = entry->m_next;
                }
            }
            entry->m_next = INDEX32_NIL;
            entry->m_prev = INDEX32_NIL;
			return entry;
        }

        static inline void helper_list_insert(xinstance_t* self, u32& head, u32 i)
        {
            xinstance_t::entry_t* entry = helper_idx2entry(self, i);
            entry->m_next               = head;
            head                        = i;
        }

        xinstance_t* create(xalloc* main_heap, void* mem_addr, u64 mem_size, u32 size_min, u32 max_num_allocs)
        {
            xinstance_t* instance   = main_heap->construct<xinstance_t>();
            u64 const    block_size = mem_size / max_num_allocs;
            u32 const    block_cnt  = max_num_allocs;
            ASSERT(block_size > size_min);

            instance->m_main_heap  = main_heap;
            instance->m_mem_base   = mem_addr;
            instance->m_mem_range  = mem_size;
            instance->m_block_size = block_size;
            instance->m_free_list  = INDEX32_NIL;
            instance->m_alloc_list = INDEX32_NIL;

            instance->m_entry_capacity = block_cnt;
            instance->m_entry_array    = (xinstance_t::entry_t*)main_heap->allocate(sizeof(xinstance_t::entry_t) * block_cnt, sizeof(void*));

            return instance;
        }

        void destroy(xinstance_t* self)
        {
            self->m_main_heap->deallocate(self->m_entry_array);
            self->m_main_heap->deallocate(self);
        }

        void* allocate(xinstance_t* self, u32 size, u32 alignment)
        {
            if (self->m_free_list != INDEX32_NIL)
            {
                xinstance_t::entry_t* entry = helper_list_remove(self, self->m_free_list, self->m_free_list);
				entry->m_size = size;
                return entry->m_address;
            }
            return nullptr;
        }

        u64 deallocate(xinstance_t* self, void* ptr)
        {
            u32 const             idx   = (u32)(((u64)ptr - (u64)self->m_mem_base) / self->m_block_size);
            xinstance_t::entry_t* entry = helper_idx2entry(self, idx);
            u64 const             size  = entry->m_size;
			entry->m_size = self->m_block_size;
            helper_list_remove(self, self->m_alloc_list, idx);
            helper_list_insert(self, self->m_free_list, idx);
            return size;
        }

    } // namespace xlarge

} // namespace xcore