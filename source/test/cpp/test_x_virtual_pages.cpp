#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/x_virtual_pages.h"
#include "xvmem/x_virtual_memory.h"

#include "xunittest/xunittest.h"

using namespace xcore;

extern xalloc* gTestAllocator;

class xvmem_test : public xvmem
{
public:
	xalloc* m_main_allocator;
	void*	m_mem_address;
	u64     m_mem_range;
	u32     m_page_size;
	u32     m_num_pages;

	void init(xalloc* main_allocator, u64 mem_range, u32 page_size)
	{
		m_main_allocator = main_allocator;
		m_page_size = page_size;
		m_num_pages = (u32)(mem_range / m_page_size);
		xbyte* mem = (xbyte*)main_allocator->allocate(m_page_size * m_num_pages, sizeof(void*));
		m_mem_address = mem;
	}

	void exit()
	{
		m_main_allocator->deallocate(m_mem_address);
	}

    virtual bool reserve(u64 address_range, u32& page_size, u32 attributes, void*& baseptr)
	{
		page_size = m_page_size;
		baseptr = m_mem_address;
		return true;
	}
    
	virtual bool release(void* baseptr)
	{
		return true;
	}

    virtual bool commit(void* address, u32 page_size, u32 page_count)
	{
		return true;
	}
    
	virtual bool decommit(void* address, u32 page_size, u32 page_count)
	{
		return true;
	}

};

UNITTEST_SUITE_BEGIN(virtual_pages)
{
    UNITTEST_FIXTURE(main)
    {
		xvmem_test	vmem;

        UNITTEST_FIXTURE_SETUP()
		{
			vmem.init(gTestAllocator, (u64)64 * 1024 * 1024, 64 * 1024);
		}

        UNITTEST_FIXTURE_TEARDOWN()
		{
			vmem.exit();
		}

		UNITTEST_TEST(create_then_release)
		{
			xvpages_t* pages = gCreateVPages(gTestAllocator, &vmem, (u64)64 * 1024 * 1024, 10);
			pages->release();
		}

		UNITTEST_TEST(create_then_alloc_free_one_page_then_release)
		{
			xvpages_t* pages = gCreateVPages(gTestAllocator, &vmem, (u64)64 * 1024 * 1024, 10);

			xvpage_t* page = pages->alloc_page(32);
			pages->free_page(page);

			pages->release();
		}

		UNITTEST_TEST(create_then_alloc_free_many_pages_then_release)
		{
			xvpages_t* pages = gCreateVPages(gTestAllocator, &vmem, (u64)64 * 1024 * 1024, 10);

			for (s32 i=0; i<1024; ++i)
			{
				u32 const size = 32 + ((i/64) * 32);
				xvpage_t* page = pages->alloc_page(size);
				pages->free_page(page);
			}

			pages->release();
		}

		UNITTEST_TEST(create_then_alloc_free_use_many_pages_then_release)
		{
			xvpages_t* pages = gCreateVPages(gTestAllocator, &vmem, (u64)64 * 1024 * 1024, 10);

			for (s32 i=0; i<1024; ++i)
			{
				u32 const size = 32 + ((i/64) * 32);
				xvpage_t* page = pages->alloc_page(size);

				pages->free_page(page);
			}

			pages->release();
		}

		UNITTEST_TEST(alloc_elements_then_release)
		{
			xvpages_t* pages = gCreateVPages(gTestAllocator, &vmem, (u64)64 * 1024 * 1024, 10);

			const s32 cnt = 1024;
			void* element[cnt];

			u32 used_pages = xvpage_t::INDEX_NIL;
			u32 i = 0;
			{
				u32 const size = 32 + ((i/64) * 32);
				
				for (s32 j=0; j<cnt; ++j)
				{
					element[j] = pages->allocate(used_pages, size);
					CHECK_NOT_NULL(element[j]);
				}				
				for (s32 j=0; j<cnt; ++j)
				{
					pages->deallocate(used_pages, element[j]);
					element[j] = nullptr;
				}				
			}
			CHECK_EQUAL(used_pages, xvpage_t::INDEX_NIL);

			pages->release();
		}

		UNITTEST_TEST(create_then_alloc_elements_many_times_then_release)
		{
			xvpages_t* pages = gCreateVPages(gTestAllocator, &vmem, (u64)64 * 1024 * 1024, 10);

			const s32 cnt = 1024;
			void* element[cnt];

			u32 used_pages = xvpage_t::INDEX_NIL;
			for (s32 i=0; i<=12; ++i)
			{
				u32 const size = 32 + (i * 8);
				
				for (s32 j=0; j<cnt; ++j)
				{
					element[j] = pages->allocate(used_pages, size);
					CHECK_NOT_NULL(element[j]);
				}				
				for (s32 j=0; j<cnt; ++j)
				{
					pages->deallocate(used_pages, element[j]);
					element[j] = nullptr;
				}				
			}
			CHECK_EQUAL(used_pages, xvpage_t::INDEX_NIL);

			pages->release();
		}

	}
}
UNITTEST_SUITE_END
