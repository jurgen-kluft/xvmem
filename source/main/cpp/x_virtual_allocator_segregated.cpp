#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/x_virtual_allocator.h"
#include "xvmem/x_virtual_pages.h"
#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_bst.h"

namespace xcore
{
    struct nblock_t : public xbst::index_based::node_t
    {
        static u32 const NIL            = 0xffffffff;
        static u32 const FLAG_COLOR_RED = 0x10000000;
        static u32 const FLAG_FREE      = 0x20000000;
        static u32 const FLAG_USED      = 0x40000000;
        static u32 const FLAG_LOCKED    = 0x80000000;
        static u32 const FLAG_MASK      = 0xF0000000;

        u32 m_addr;      // addr = base_addr(m_addr * size_step)
        u32 m_flags;     // [Allocated, Free, Locked, Color]
        u32 m_page_cnt;  // Number of pages committed

        u32 m_prev_addr; // previous node in memory, can be free, can be allocated
        u32 m_next_addr; // next node in memory, can be free, can be allocated

        void init()
        {
            clear();
            m_addr      = 0;
            m_flags     = 0;
            m_page_cnt  = 0;
            m_prev_addr = NIL;
            m_next_addr = NIL;
        }

        inline void* get_addr(void* baseaddr, u64 size_step) const { return (void*)((u64)baseaddr + ((u64)m_addr * size_step)); }
        inline void  set_addr(void* baseaddr, u64 size_step, void* addr) { m_addr = (u32)(((u64)addr - (u64)baseaddr) / size_step); }
        inline u64   get_page_cnt(u64 page_size) const { return (u64)m_page_cnt * page_size; }
        inline void  set_page_cnt(u64 size, u64 page_size) { m_page_cnt = (u32)(size / page_size); }

        inline void set_locked() { m_flags = m_flags | FLAG_LOCKED; }
        inline void set_used(bool used) { m_flags = m_flags | FLAG_USED; }
        inline bool is_free() const { return (m_flags & FLAG_MASK) == FLAG_FREE; }
        inline bool is_locked() const { return (m_flags & FLAG_MASK) == FLAG_LOCKED; }

        inline void set_color_red() { m_flags = m_flags | FLAG_COLOR_RED; }
        inline void set_color_black() { m_flags = m_flags & ~FLAG_COLOR_RED; }
        inline bool is_color_red() const { return (m_flags & FLAG_COLOR_RED) == FLAG_COLOR_RED; }
        inline bool is_color_black() const { return (m_flags & FLAG_COLOR_RED) == 0; }

        XCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    // Segregated Allocator uses BST's for free blocks and allocated blocks.
	// Every level starts of with one free block that covers the memory range of that level.
	// So we can split nodes and we also coalesce.

    class xvmem_allocator_segregated : public xalloc
    {
    public:
        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);
        virtual void  release();

        struct vmem_t
        {
            struct range_t
            {
                void reset() { m_num_allocs = 0; m_next = 0xffff; m_prev = 0xffff; }
                bool is_unused() const { return m_num_allocs == 0; }
                void register_alloc() { m_num_allocs += 1; }
                void register_dealloc() { m_num_allocs -= 1; }

                u16 m_num_allocs;
                u16 m_next;
                u16 m_prev;
            };
            void*    m_mem_address;
            u64      m_sub_range;
            u32      m_sub_max;
            u16      m_sub_freelist;
            range_t* m_sub_ranges;

            void    init(xalloc* main_heap, void* mem_address, u64 mem_range, u64 sub_range)
            {
                m_mem_address = mem_address;
                m_sub_range = sub_range;
                m_sub_max = mem_range / sub_range;
                m_sub_ranges = (range_t*)main_heap->allocate(sizeof(u16) * m_sub_max, sizeof(void*));
                for (s32 i=0; i<m_sub_max; i++)
                {
                    m_sub_ranges[i].reset();
                }
                for (s32 i=1; i<m_sub_max; i++)
                {
                    u16 icurr = i-1;
                    u16 inext = i;
                    range_t* pcurr = &m_sub_ranges[icurr];
                    range_t* pnext = &m_sub_ranges[inext];
                    pcurr->m_next = inext;
                    pnext->m_prev = icurr;
                }
            }
            
            void    release(xalloc* main_heap)
            {
                m_mem_address = nullptr;
                m_sub_range = 0;
                m_sub_max = 0;
                main_heap->deallocate(m_sub_ranges);
            }
            
            bool    is_range_empty(void* addr) const
            {
                u32 const idx = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_sub_range);
                return m_sub_ranges[idx].is_unused();
            }

            bool    obtain_range(void*& addr)
            {
                if (m_sub_freelist != 0xffff)
                {
                    u32 item = m_sub_freelist;
                    range_t* pcurr = &m_sub_ranges[m_sub_freelist];
                    m_sub_freelist = pcurr->m_next;
                    pcurr->m_next = 0xffff;
                    pcurr->m_prev = 0xffff;
                    addr = (void*)((u64)m_mem_address + (m_sub_range * item));
                    return true;
                }
                return false;
            }
            void    release_range(void*& addr)
            {
                u32 const idx = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_sub_range);
                range_t* pcurr = &m_sub_ranges[m_sub_freelist];
                pcurr->m_prev = idx;
                range_t* pitem = &m_sub_ranges[idx];
                pitem->m_next = m_sub_freelist;
                m_sub_freelist = idx;
            }
            void    register_alloc(void* addr)
            {
                u32 const idx = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_sub_range);
                m_sub_ranges[idx].register_alloc();
            }
            void    register_dealloc(void* addr)
            {
                u32 const idx = (u32)(((uptr)addr - (uptr)m_mem_address) / (uptr)m_sub_range);
                m_sub_ranges[idx].register_dealloc();
            }
        };

        struct level_t
        {
            inline  level_t() : m_mem_address(nullptr), m_mem_range(0), m_free(nblock_t::NIL), m_allocated(nblock_t::NIL) {}
            void*   m_mem_address;
			u64     m_mem_range;
			u32     m_free;
			u32     m_allocated;
			void*   allocate(u32 size, u32 alignment, xfsa* node_alloc,);
			void    deallocate(void* p);
        };

        xalloc*   m_main_alloc;
        xfsa*     m_node_alloc;
        xvmem*    m_vmem;
        u32       m_allocsize_min;
        u32       m_allocsize_max;
        u32       m_allocsize_step;
        u32       m_pagesize;
        u32       m_level_cnt;
        level_t*  m_level;
    };

    void  xvmem_allocator_segregated::initialize(xalloc* main_heap, xfsa* node_alloc, xvmem* vmem, u64 vmem_range, u64 level_range, u32 allocsize_min, u32 allocsize_max, u32 allocsize_step, u32 pagesize)
    {
        m_main_alloc = main_alloc;
        m_node_alloc = node_alloc;
        m_vmem = vmem;

        m_allocsize_min = allocsize_min;
        m_allocsize_max = allocsize_max;
        m_allocsize_step = allocsize_step;
        m_pagesize = pagesize;
        m_level_cnt = (m_allocsize_max - m_allocsize_min) / m_allocsize_step;
        m_level = (level_t*)m_main_alloc->allocate(sizeof(level_t) * m_level_cnt, sizeof(void*));
        for (s32 i=0; i<m_level_cnt; ++i)
        {
            m_level = level_t();
        }
    }

    void* xvmem_allocator_segregated::allocate(u32 size, u32 alignment)
    {
        ASSERT(size>=m_allocsize_min && size<m_allocsize_max);
        ASSERT(alignment<=m_page_size);
        u32 aligned_size = (size + m_allocsize_step - 1) & (~m_allocsize_step - 1);
        u32 index = (aligned_size - m_allocsize_min) / m_level_cnt;
    }

    void  xvmem_allocator_segregated::deallocate(void* p)
    {

    }

    void  xvmem_allocator_segregated::release()
    {

    }

}; // namespace xcore
