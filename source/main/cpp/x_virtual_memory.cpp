#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/x_virtual_memory.h"

#if defined TARGET_MAC
#endif
#if defined TARGET_PC
#include "Windows.h"
#endif

namespace xcore
{
    class xvmem_os : public xvmem
    {
    public:
        virtual bool reserve(u64 address_range, u32& page_size, u32 attributes, void*& baseptr);
        virtual bool release(void* baseptr);

        virtual bool commit(void* page_address, u32 page_size, u32 page_count);
        virtual bool decommit(void* page_address, u32 page_size, u32 page_count);
    };

#if defined TARGET_MAC

    bool xvmem_os::reserve(u64 address_range, u32& page_size, u32 reserve_flags, void*& baseptr) 
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

    static xvmem_os sVMem;

#elif defined TARGET_PC

    bool xvmem_os::reserve(u64 address_range, u32& page_size, u32 reserve_flags, void*& baseptr)
    { 
		unsigned int allocation_type = MEM_RESERVE | reserve_flags;
		unsigned int protect = 0;
		baseptr = ::VirtualAlloc(NULL, (SIZE_T)address_range, allocation_type, protect);
        return baseptr != nullptr;
    }

    bool xvmem_os::release(void* baseptr) 
    {
		BOOL b = ::VirtualFree(baseptr, 0, MEM_RELEASE);
        return b; 
    }

    bool xvmem_os::commit(void* page_address, u32 page_size, u32 page_count) 
    {
		unsigned int allocation_type = MEM_COMMIT;
		unsigned int protect = PAGE_READWRITE;
		BOOL success = ::VirtualAlloc(page_address, page_size * page_count, allocation_type, protect) != NULL;
        return success;
    }

    bool xvmem_os::decommit(void* page_address, u32 page_size, u32 page_count) 
    {
		unsigned int allocation_type = MEM_DECOMMIT;
		BOOL b = ::VirtualFree(page_address, page_size * page_count, allocation_type);
        return b; 
    }

	static xvmem_os sVMem;

#else

#error Unknown Platform/Compiler configuration for xvmem

#endif

    xvmem* gGetVirtualMemory()
    {
        return &sVMem;
    }


}; // namespace xcore
