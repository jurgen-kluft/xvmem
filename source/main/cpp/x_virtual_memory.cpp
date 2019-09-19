#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    class xvmem_os : public xvirtual_memory
    {
    public:
        virtual bool reserve(u64 address_range, u32 page_size, u32 attributes, void*& baseptr);
        virtual bool release(void* baseptr);

        virtual bool commit(void* page_address, u32 page_size, u32 page_count);
        virtual bool decommit(void* page_address, u32 page_size, u32 page_count);
    };

#if defined TARGET_MAC

    bool xvmem_os::reserve(u64 address_range, u32 page_size, u32 reserve_flags, void*& baseptr) 
    {
		baseptr = mmap(NULL, address_range, PROT_NONE, MAP_PRIVATE | MAP_ANON | reserve_flags, -1, 0);
		if (baseptr == MAP_FAILED)
			baseptr = NULL;

		msync(baseptr, address_range, (MS_SYNC | MS_INVALIDATE));
        return baseptr!=nullptr; 
    }

    bool xvmem_os::release(void* baseptr, u64 address_range)
    {
		msync(baseptr, address_range, MS_SYNC);
		s32 ret = munmap(baseptr, address_range);
		ASSERT(ret == 0, "munmap failed");
        return ret == 0;
    }

    bool xvmem_os::commit(void* page_address, u32 page_size, u32 page_count) 
    {
		mmap(page_address, page_size*page_count, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANON | commit_flags, -1, 0);
		msync(page_address, page_size*page_count, MS_SYNC | MS_INVALIDATE);
        return true;
    }

    bool xvmem_os::decommit(void* page_address, u32 page_size, u32 page_count) 
    {
		mmap (page_address, page_size*page_count, PROT_NONE, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0);
		msync(page_address, page_size*page_count, MS_SYNC | MS_INVALIDATE);
        return true;
    }

#elif defined TARGET_PC

    bool xvmem_os::reserve(u64 address_range, u32 page_size, u32 reserve_flags, void*& baseptr)
    { 
		unsigned int flags = MEM_RESERVE | reserve_flags;
		baseptr = ::VirtualAlloc(NULL, address_range, flags, PAGE_NOACCESS);
        return baseptr != nullptr;
    }

    bool xvmem_os::release(void* baseptr) 
    {
		BOOL b = ::VirtualFree(baseptr, 0, MEM_RELEASE);
        return b; 
    }

    bool xvmem_os::commit(void* page_address, u32 page_size, u32 page_count) 
    {
		u32 va_flags = MEM_COMMIT;
		BOOL success = ::VirtualAlloc(page_address, page_size * page_count, va_flags, PAGE_READWRITE) != NULL;
        return success;
    }

    bool xvmem_os::decommit(void* page_address, u32 page_size, u32 page_count) 
    {
		BOOL b = ::VirtualFree(page_address, page_size, MEM_DECOMMIT);
        return b; 
    }

#else

#error Unknown Platform/Compiler configuration for xvmem

#endif

    xvirtual_memory* gGetVirtualMemory()
    {
        static xvmem_os sVMem;
        return &sVMem;
    }

    // ----------------------------------------------------------------------------
    // ----------------------------------------------------------------------------
    // Virtual Memory Page Allocator
    //
    // This allocator is capable of allocating pages from a virtual address range.
    // When a page is needed it is committed to physical memory and tracked.
    //
    // Since we are just a page allocator we cannot touch any memory in the page
    // for book-keeping, we have to do all the book-keeping separate.
    //
    // Characteristics in terms of memory consumption for book-keeping is:
    //
    // - Range = 4 GB, Page-Size = 64 KB -> Number-of-Pages = 65536 pages
    //   Book-keeping data = 3 * (8KB + 256)+ sizeof(xvpage_alloc)
    //   Book-keeping data = less than 32 KB

    class xvpage_alloc : public xpage_alloc
    {
    public:
        virtual void* allocate()
        {
            void* ptr = nullptr;
            if (m_pages_empty_cnt == 0)
            {
                if (m_pages_free_cnt > 0)
                {
                    u32 page_index;
                    if (m_pages_free.find(page_index))
                    {
                        m_pages_used_cnt += 1;
                        m_pages_used.set(page_index);
                        m_pages_free_cnt -= 1;
                        m_pages_free.clr(page_index);
                        ptr  = calc_page_addr(page_index);
                    }
                }
            }
            else
            {
                u32 page_index;
                if (m_pages_empty.find(page_index))
                {
                    m_pages_used_cnt += 1;
                    m_pages_used.set(page_index);
                    m_pages_empty_cnt -= 1;
                    m_pages_empty.clr(page_index);
                    ptr = calc_page_addr(page_index);
                    m_vmem->commit(ptr, m_page_size, 1);
                }
            }
            return ptr;
        }

        virtual void deallocate(void* ptr)
        {
            s32 const pindex = calc_page_index(ptr);
            m_pages_used_cnt -= 1;
            m_pages_used.clr(pindex);
            m_pages_empty_cnt += 1;
            m_pages_empty.set(pindex);
            if (m_pages_empty_cnt > m_pages_comm_max)
            {
                // Decommit pages
                // @TODO: Decide what to do here.
                // What is the policy for decommitting pages, should
                // we keep a couple committed and anything beyond that
                // we decommit?
            }
        }

        virtual bool info(void* ptr, void*& page_addr, u32& page_size, u32& page_index)
        {
            if (ptr >= m_addr_base && ptr < (void*)((uptr)m_addr_base + m_addr_range))
            {
                u32 const pindex = calc_page_index(ptr);
                page_addr        = calc_page_addr(pindex);
                page_size        = m_page_size;
                page_index       = pindex;
                return true;
            }
            return false;
        }

        virtual void release()
        {
            ASSERT(m_pages_used_cnt == 0); // Any pages still being used ?

            // decommit
            u32 page_index;
            while (m_pages_empty.find(page_index))
            {
                void* page_addr = calc_page_addr(page_index);
                m_vmem->decommit(page_addr, m_page_size, 1);
            }
            m_vmem->release(m_addr_base);

            m_vmem        = nullptr;
            m_addr_base   = nullptr;
            m_addr_range  = 0;
            m_page_size   = 0;
            m_page_battrs = 0;
            m_page_pattrs = 0;

            m_pages_comm_max  = 0;
            m_pages_used_cnt  = 0;
            m_pages_empty_cnt = 0;
            m_pages_free_cnt  = 0;

            m_pages_used.release(m_alloc);
            m_pages_empty.release(m_alloc);
            m_pages_free.release(m_alloc);

            m_alloc = nullptr;
        }

        void* calc_page_addr(u32 index) const { return (void*)((uptr)m_addr_base + index * m_page_size); }

        s32 calc_page_index(void* ptr) const
        {
            ASSERT(ptr >= m_addr_base && ptr < (void*)((uptr)m_addr_base + m_addr_range));
            s32 const page_index = (s32)(((uptr)ptr - (uptr)m_addr_base) / m_page_size);
            return page_index;
        }

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        xalloc*          m_alloc;
        xvirtual_memory* m_vmem;
        void*            m_addr_base;       // The virtual memory base address
        u64              m_addr_range;      // The size of the virtual memory
        u32              m_page_size;       // The size of one page
        u32              m_page_attrs;      // Page base attributes
        u32              m_page_pattrs;     // Page protection attributes
        u32              m_pages_comm_max;  //
        u32              m_pages_used_cnt;  //
        xhibitset        m_pages_used;      // Pages that are committed and used
        u32              m_pages_empty_cnt; //
        xhibitset        m_pages_empty;     // Pages that are committed but free
        u32              m_pages_free_cnt;  //
        xhibitset        m_pages_free;      // Pages that are not committed and free

        void init(xalloc* allocator, u64 address_range, u32 page_size, u32 page_attrs, u32 protection_attrs)
        {
            m_addr_range       = address_range;
            m_page_size        = page_size;
            m_page_attrs       = page_attrs;
            m_protection_attrs = protection_attrs;
            m_vmem->reserve(m_addr_range, m_page_size, m_page_attrs, m_addr_base);

            u32 numpages = (u32)(m_addr_range / m_page_size);

            m_pages_comm_max  = 2;
            m_pages_used_cnt  = 0;
            m_pages_empty_cnt = 0;
            m_pages_free_cnt  = 0;

            m_pages_used.init(allocator, numpages, false, true);
            m_pages_empty.init(allocator, numpages, false, true);
            m_pages_free.init(allocator, numpages, true, true);
        }
    };

    xpage_alloc* gCreateVMemPageAllocator(xalloc* allocator, u64 address_range, u32 page_size, u32 page_attrs, u32 protection_attrs, xvirtual_memory* vmem)
    {
        xvpage_alloc* vpage_allocator = allocator->construct<xvpage_alloc>();
        vpage_allocator->m_alloc      = allocator;
        vpage_allocator->m_vmem       = vmem;
        vpage_allocator->init(allocator, address_range, page_size, page_attrs, protection_attrs);
        return vpage_allocator;
    }

}; // namespace xcore
