#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_large.h"

namespace xcore
{
    namespace xlarge
    {
        struct xinstance_t
        {
            struct entry_t
            {
                entry_t()
                    : m_address(nullptr)
                    , m_size(0)
                {
                }
                void* m_address;
                u64   m_size;
            };

            xalloc*  m_main_heap;
            void*    m_mem_base;
            u64      m_mem_range;
            u32      m_entry_write;
            u32      m_entry_read;
            u32      m_entry_capacity;
            entry_t* m_entry_array;
        };

        static inline u32 helper_inc_index(u32 const index, u32 const cap) { return (index + 1) % cap; }
        static inline u32 helper_dec_index(u32 const index, u32 const cap) { return (index + (cap - 1)) % cap; }

        xinstance_t* create(xalloc* main_heap, void* mem_addr, u64 mem_size, u32 size_min, u32 size_step, u32 max_num_allocs)
        {
            // We can divide the memory range by the number of allocations that can be done, this
            // will leave us with a maximum allocation size that the user can do.
            // The benefit is that we will not have any fragmentation to deal with.
            return nullptr;
        }

        void destroy(xinstance_t* self) { self->m_main_heap->deallocate(self); }

        void* allocate(xinstance_t* self, u32 size, u32 alignment)
        {
            if (self->m_entry_read != helper_inc_index(self->m_entry_write, self->m_entry_capacity))
            {
                xinstance_t::entry_t* entry = &self->m_entry_array[self->m_entry_write];
                self->m_entry_write         = helper_inc_index(self->m_entry_write, self->m_entry_capacity);
            }
            return nullptr;
        }

        u32 deallocate(xinstance_t* self, void* ptr)
        {
            u32 index = self->m_entry_read;
            while (index != self->m_entry_write)
            {
                xinstance_t::entry_t* entry = &self->m_entry_array[index];
                if (entry->m_address == ptr)
                {
                    u64 const size   = entry->m_size;
                    entry->m_address = nullptr;
                    entry->m_size    = 0;
                    if (index == self->m_entry_read)
                    {
                        self->m_entry_read = helper_inc_index(self->m_entry_read, self->m_entry_capacity);
                    }
                    else if (index == self->m_entry_write)
                    {
                        self->m_entry_write = helper_dec_index(self->m_entry_write, self->m_entry_capacity);
                    }
                    return size;
                }
                index = helper_inc_index(index, self->m_entry_capacity);
            }
            return 0;
        }

    } // namespace xlarge

} // namespace xcore