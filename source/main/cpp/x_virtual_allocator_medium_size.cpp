#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_btree.h"

#include "xvmem/x_virtual_allocator.h"
#include "xvmem/x_virtual_pages.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    // Using btree32 for the medium size allocators to track allocations.
    // The key-value implementation would be like this:
    struct naddr_t
    {
        u32 m_addr;      // (m_addr * size step) + base addr
        u32 m_flags;     // Allocated, Free, Locked
        u32 m_addr_prev; // previous node in memory, can be free, can be allocated
        u32 m_addr_next; // next node in memory, can be free, can be allocated
    };

    struct nsize_t
    {
        u32 m_list_prev;  // either in the allocation slot array as a list node
        u32 m_list_next;  // or in the size slot array as a list node
        u32 m_size;       // 
        u32 m_index;      // 
    };

    class xvmem_alloc_kv : public xbtree_kv
    {
        xfsadexed* const m_fsa;

    public:
        inline xvmem_alloc_kv(xfsadexed* fsa)
            : m_fsa(fsa)
        {
        }

        virtual u64 get_key(u32 value) const
        {
            // The key is the address
            node_t* node = (node_t*)m_fsa->idx2ptr(value);
            return node->m_addr;
        }

        virtual void set_key(u32 value, u64 key)
        {
            // Do not need to do anything since the key is already set
        }
    };


}; // namespace xcore
