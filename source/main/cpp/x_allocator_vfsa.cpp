#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/x_allocator_vfsa.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    class xssa : public xalloc
    {
    public:
        u32        m_min_size;
        u32        m_max_size;
        u32        m_size_step;
        xvfsa**    m_fsalloc;
    };

    struct xvelem_t
    {
        xvelem_t*     m_next;
    };

    struct xvpage_t
    {
        inline      xvpage_t() : m_next(nullptr), m_prev(nullptr), m_free(nullptr), m_cur(0), m_max(0) {}
        xvpage_t*   m_next;
        xvpage_t*   m_prev;
        xvelem_t*   m_free;
        u16         m_scur;
        u16         m_smax;
        u16         m_used;
        u16         m_capacity;
        bool        is_full() const { return m_used == m_capacity; }
    };

    class xvpage_allocator
    {
    public:

    };

    /**
        @brief	xvfsa is a fixed size allocator using virtual memory pages over a reserved address range.

        This makes the address of pages predictable and deallocation thus easy to find out which page to manipulate.

        Example:
            FSA address range = 512 MB
            Page Size = 64 KB
            Number of Pages = 8192

        @note	This allocator does not guarantee that two objects allocated sequentially are sequential in memory.
        **/
    class xvfsa
    {
    public:
        xvfsa();
        ~xvfsa() {}

		enum { Null = 0xffffffff };

        ///@name	Should be called when created with default constructor
        //			Parameters are the same as for constructor with parameters
        void init(xalloc* a, xpage_alloc* page_allocator, xvfsa_params const& params);

        void* allocate(u32& size);
        void  deallocate(void* p);

        XCORE_CLASS_PLACEMENT_NEW_DELETE

    protected:
        bool extend(u32& page_index, void*& page_address);



    protected:
        xvfsa_params m_params;
        xalloc*      m_alloc;
        xpage_alloc* m_page_allocator;
        page_t*      m_pages_notfull;
        page_t*      m_pages_full;
        xhibitset    m_pages_noncommitted;

    private:
        // Copy construction and assignment are forbidden
        xvfsa(const xvfsa&);
        xvfsa& operator=(const xvfsa&);
    };

    xvfsa::xvfsa() : m_alloc(nullptr), m_page_allocator(nullptr), m_page_current(nullptr), m_page_current_index(-1) {}

    void xvfsa::init(xalloc* a, xpage_alloc* page_allocator, xvfsa_params const& params)
    {
        m_params             = params;
        m_alloc              = a;
        m_page_allocator     = page_allocator;
        m_page_current       = nullptr;
        m_page_current_index = 0xffffffff;

        u32 const maxnumpages = m_params.get_address_range() / m_params.get_page_size();

        m_pages_not_full.init(xheap(a), maxnumpages, false, true);
    }

    void* xvfsa::allocate(u32& size)
    {
        if (m_page_current == nullptr)
            return nullptr;

		/*
        void* page_address = calc_page_address(m_page_to_alloc_from);
        void* p            = m_pages[m_page_to_alloc_from].allocate(page_address);

        if (m_page_current->full())
        {
            m_pages_not_full.clr(m_page_current_index);
            if (!m_pages_not_full.find(m_page_current_index))
            {
                // Could not find an empty page, so we have to extend
                if (!extend(page_address, m_page_to_alloc_from))
                {
                    // No more decommitted pages available, full!
                    return nullptr;
                }
                m_pages_not_full.clr(m_page_current_index);
            }
        }
        return p;
		*/
		return nullptr;
    }

    void xvfsa::deallocate(void* p)
    {
		/*
        s32   page_index = calc_page_index(p);
        void* page_addr  = calc_page_address(page_index);
        m_pages[page_index].deallocate(p);
        if (m_pages[page_index].empty())
        {
            // The page we deallocated our item at is now empty
            m_pages_not_full.clr(page_index);
            m_pages_empty.set(page_index);
            m_pages_empty_cnt += 1;
            if (page_index == m_page_to_alloc_from)
            {
                // Get a 'better' page to allocate from (biasing)
                m_page_to_alloc_from = m_pages_empty.find();
            }
        }
		*/
    }

    bool xvfsa::extend(u32& page_index, void*& page_address)
    {
        page_address = m_page_allocator->allocate(page_index);
        return page_address != nullptr;
    }

    void xvfsa::release()
    {
        // Return all pages back to the page allocator

        xheap heap(m_alloc);
        heap.destruct(this);
    }

    xfsalloc* gCreateVFsAllocator(xalloc* a, xpage_alloc* page_allocator, xvfsa_params const& params)
    {
        xheap          heap(a);
        xvfsa* allocator = heap.construct<xvfsa>();
        allocator->init(a, page_allocator, params);
        return allocator;
    }

    #if 0
    class vmalloc : public xdexedfxsa
    {
        enum
        {
            Null32 = 0xffffffff,
            Null16 = 0xffff,
        };

        // Note: Keep bit 31-30 free! So only use bit 0-29 (30 bits)
        inline u32 page_index(u32 index) const { return (index >> 13) & 0x1FFFF; }
        inline u32 item_index(u32 index) const { return (index >> 0) & 0x1FFF; }
        inline u32 to_index(u32 page_index, u32 item_index) const { return ((page_index & 0x1FFFF) << 13) | (item_index & 0x1FFF); }

    public:
        virtual void* allocate()
        {
            u32 page_index;
            if (!m_notfull_used_pages.find(page_index))
            {
                if (!alloc_page(page_index))
                {
                    return nullptr;
                }
                m_notfull_used_pages.set(page_index);
            }

            page_t* page = get_page(page_index);

            u32   i;
            void* ptr = page->allocate(i);
            if (page->m_alloc_cnt == m_page_max_alloc_count)
            { // Page is full
                m_notfull_used_pages.clr(page_index);
            }
            return ptr;
        }

        virtual void deallocate(void* p)
        {
            u32     page_index = (u32)(((uptr)p - (uptr)m_base_addr) / m_page_size);
            page_t* page       = get_page(page_index);
            u32     item_index = page->ptr2idx(p);
            page->deallocate(item_index);
            if (page->m_alloc_cnt == 0)
            {
                dealloc_page(page_index);
            }
        }

        virtual void* idx2ptr(u32 i) const
        {
            page_t* page = get_page(page_index(i));
            return page->idx2ptr(item_index(i));
        }

        virtual u32 ptr2idx(void* ptr) const
        {
            u32     page_index = (u32)(((uptr)ptr - (uptr)m_base_addr) / m_page_size);
            page_t* page       = get_page(page_index);
            u32     item_index = page->ptr2idx(ptr);
            return to_index(page_index, item_index);
        }

        // Every used page has it's first 'alloc size' used for book-keeping
        // sizeof == 8 bytes
        struct page_t
        {
            void init(u32 alloc_size)
            {
                m_list_head  = Null16;
                m_alloc_cnt  = 0;
                m_alloc_size = alloc_size;
                m_alloc_ptr  = alloc_size * 1;
                // We do not initialize a free-list, instead we allocate from
                // a 'pointer' which grows until we reach 'max alloc count'.
                // After that allocation happens from m_list_head.
            }

            void* allocate(u32& item_index)
            {
                if (m_list_head != Null16)
                { // Take it from the list
                    item_index  = m_list_head;
                    m_list_head = ((u16*)this)[m_list_head >> 1];
                }
                else
                {
                    item_index = m_alloc_ptr / m_alloc_size;
                    m_alloc_ptr += m_alloc_size;
                }

                m_alloc_cnt += 1;
                return (void*)((uptr)this + (item_index * m_alloc_size));
            }

            void deallocate(u32 item_index)
            {
                ((u16*)this)[item_index >> 1] = m_list_head;
                m_list_head                   = item_index;
                m_alloc_cnt -= 1;
            }

            void* idx2ptr(u32 index) const
            {
                void* ptr = (void*)((uptr)this + (index * m_alloc_size));
                return ptr;
            }

            u32 ptr2idx(void* ptr) const
            {
                u32 item_index = (u32)(((uptr)ptr - (uptr)this) / m_alloc_size);
                return item_index;
            }

            u16 m_list_head;
            u16 m_alloc_cnt;
            u16 m_alloc_size;
            u16 m_alloc_ptr;
        };

        page_t* get_page(u32 page_index) const
        {
            page_t* page = (page_t*)((uptr)m_base_addr + page_index * m_page_size);
            return page;
        }

        bool alloc_page(u32& page_index)
        {
            u32 freepage_index;
            return m_notused_free_pages.find(freepage_index);
        }

        void dealloc_page(u32 page_index)
        {
            page_t* page = get_page(page_index);
            m_vmem->decommit((void*)page, m_page_size, 1);
            m_notused_free_pages.set(page_index);
        }

        void init(xalloc* a, xvirtual_memory* vmem, u32 alloc_size, u64 addr_range, u32 page_size)
        {
            m_alloc                = a;
            m_vmem                 = vmem;
            m_page_size            = page_size;
            m_page_max_count       = (u32)(addr_range / page_size);
            m_page_max_alloc_count = (page_size / alloc_size) - 1;
            xheap heap(a);
            m_notfull_used_pages.init(heap, m_page_max_count, false, true);
            m_notused_free_pages.init(heap, m_page_max_count, true, true);
        }

        virtual void release()
        {
            // deallocate any allocated resources
            // decommit all used pages
            // release virtual memory
            xheap heap(m_alloc);
            m_notfull_used_pages.release(heap);
            m_notused_free_pages.release(heap);
        }

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        xalloc*          m_alloc;
        xvirtual_memory* m_vmem;
        void*            m_base_addr;
        u32              m_page_size;
        s32              m_page_max_count;
        u32              m_page_max_alloc_count;
        xbitlist         m_notfull_used_pages;
        xbitlist         m_notused_free_pages;
    };

    xdexedfxsa* gCreateVMemBasedDexAllocator(xalloc* a, xvirtual_memory* vmem, u32 alloc_size, u64 addr_range, u32 page_size)
    {
        xheap    heap(a);
        vmalloc* allocator = heap.construct<vmalloc>();
        allocator->init(a, vmem, alloc_size, addr_range, page_size);
        return allocator;
    }
#endif

}; // namespace xcore
