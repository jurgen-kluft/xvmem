#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_strategy_fsa_small.h"

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

	virtual bool initialize(u32 pagesize)
	{
		return true;
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

UNITTEST_SUITE_BEGIN(strategy_fsa_small)
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
			void* mem_base = nullptr;
			u64 const mem_range = (u64)64 * 1024 * 1024;
			u32 const page_attrs = 0;
			u32 page_size;
			vmem.reserve(mem_range, page_size, page_attrs, mem_base);
			xfsastrat::xpages_t* pages = xfsastrat::create(gTestAllocator, mem_base, mem_range, page_size);

			xfsastrat::destroy(pages);
			vmem.release(mem_base);
		}

		UNITTEST_TEST(create_then_alloc_free_one_page_then_release)
		{
			void* mem_base = nullptr;
			u64 const mem_range = (u64)64 * 1024 * 1024;
			u32 const page_attrs = 0;
			u32 page_size;
			vmem.reserve(mem_range, page_size, page_attrs, mem_base);
			xfsastrat::xpages_t* pages = xfsastrat::create(gTestAllocator, mem_base, mem_range, page_size);

			xfsastrat::xlist_t page_list;
			CHECK_EQUAL(0, page_list.m_count);
			xfsastrat::alloc_page(pages, page_list, 32);
			CHECK_EQUAL(1, page_list.m_count);
			xfsastrat::free_one_page(pages, page_list);
			CHECK_EQUAL(0, page_list.m_count);
			
			xfsastrat::destroy(pages);
			vmem.release(mem_base);
		}

		UNITTEST_TEST(create_then_alloc_free_many_pages_then_release)
		{
			void* mem_base = nullptr;
			u64 const mem_range = (u64)64 * 1024 * 1024;
			u32 const page_attrs = 0;
			u32 page_size;
			vmem.reserve(mem_range, page_size, page_attrs, mem_base);
			xfsastrat::xpages_t* pages = xfsastrat::create(gTestAllocator, mem_base, mem_range, page_size);

			xfsastrat::xlist_t page_list;
			for (s32 i=0; i<1024; ++i)
			{
				u32 const size = 32 + ((i/64) * 32);
				CHECK_EQUAL(0, page_list.m_count);
				xfsastrat::alloc_page(pages, page_list, size);
				CHECK_EQUAL(1, page_list.m_count);
				xfsastrat::free_one_page(pages, page_list);
				CHECK_EQUAL(0, page_list.m_count);
			}
			
			xfsastrat::destroy(pages);
			vmem.release(mem_base);
		}

		UNITTEST_TEST(create_then_alloc_free_use_many_pages_then_release)
		{
			void* mem_base = nullptr;
			u64 const mem_range = (u64)64 * 1024 * 1024;
			u32 const page_attrs = 0;
			u32 page_size;
			vmem.reserve(mem_range, page_size, page_attrs, mem_base);
			xfsastrat::xpages_t* pages = xfsastrat::create(gTestAllocator, mem_base, mem_range, page_size);

			xfsastrat::xlist_t page_list;
			for (s32 i=0; i<1024; ++i)
			{
				u32 const size = 32 + ((i/64) * 32);
				xfsastrat::alloc_page(pages, page_list, size);

				const u32 cnt = (64 * 1024) / size;
				void** elements = (void**)gTestAllocator->allocate(cnt * sizeof(void*), sizeof(void*));

				xfsastrat::xlist_t notfull_pages;
				xfsastrat::xlist_t empty_pages;
				{
					elements[0] = xfsastrat::alloc_elem(pages, notfull_pages, size);
					for (u32 j=1; j<cnt; ++j)
					{
						CHECK_EQUAL(1, notfull_pages.m_count);
						elements[j] = xfsastrat::alloc_elem(pages, notfull_pages, size);
					}				
					CHECK_EQUAL(0, notfull_pages.m_count);

					xfsastrat::free_elem(pages, notfull_pages, elements[0], empty_pages);
					for (u32 j=1; j<cnt; ++j)
					{
						CHECK_EQUAL(1, notfull_pages.m_count);
						xfsastrat::free_elem(pages, notfull_pages, elements[j], empty_pages);
						elements[j] = nullptr;
					}				
					CHECK_EQUAL(0, notfull_pages.m_count);
					CHECK_EQUAL(1, empty_pages.m_count);
					xfsastrat::free_one_page(pages, empty_pages);
				}

				gTestAllocator->deallocate(elements);
				xfsastrat::free_one_page(pages, page_list);
			}

			xfsastrat::destroy(pages);
			vmem.release(mem_base);
		}

		UNITTEST_TEST(alloc_elements_then_release)
		{
			void* mem_base = nullptr;
			u64 const mem_range = (u64)64 * 1024 * 1024;
			u32 const page_attrs = 0;
			u32 page_size;
			vmem.reserve(mem_range, page_size, page_attrs, mem_base);
			xfsastrat::xpages_t* pages = xfsastrat::create(gTestAllocator, mem_base, mem_range, page_size);

			const s32 cnt = 1024;
			void* element[cnt];

			xfsastrat::xlist_t used_pages;
			xfsastrat::xlist_t empty_pages;
			u32 i = 0;
			{
				u32 const size = 32 + ((i/64) * 32);
				
				for (s32 j=0; j<cnt; ++j)
				{
					element[j] = xfsastrat::alloc_elem(pages, used_pages, size);
					CHECK_NOT_NULL(element[j]);
				}				
				for (s32 j=0; j<cnt; ++j)
				{
					xfsastrat::free_elem(pages, used_pages, element[j], empty_pages);
					element[j] = nullptr;
				}				
			}
			CHECK_EQUAL(used_pages.m_count, 0);
			CHECK_EQUAL(1, empty_pages.m_count);
			xfsastrat::free_one_page(pages, empty_pages);

			xfsastrat::destroy(pages);
			vmem.release(mem_base);
		}

		UNITTEST_TEST(create_then_alloc_elements_many_times_then_release)
		{
			void* mem_base = nullptr;
			u64 const mem_range = (u64)64 * 1024 * 1024;
			u32 const page_attrs = 0;
			u32 page_size;
			vmem.reserve(mem_range, page_size, page_attrs, mem_base);
			xfsastrat::xpages_t* pages = xfsastrat::create(gTestAllocator, mem_base, mem_range, page_size);

			const s32 cnt = 1024;
			void* element[cnt];

			xfsastrat::xlist_t used_pages;
			xfsastrat::xlist_t empty_pages;

			for (s32 i=0; i<32; ++i)
			{
				u32 const size = 32 + (i * 8);
				
				for (s32 j=0; j<cnt; ++j)
				{
					element[j] = xfsastrat::alloc_elem(pages, used_pages, size);
					CHECK_NOT_NULL(element[j]);
				}				

				for (s32 j=0; j<cnt; ++j)
				{
					xfsastrat::free_elem(pages, used_pages, element[j], empty_pages);
					element[j] = nullptr;
				}				
				CHECK_NOT_EQUAL(0, empty_pages.m_count);
				xfsastrat::free_all_pages(pages, empty_pages);
				CHECK_EQUAL(0, empty_pages.m_count);
			}

			xfsastrat::destroy(pages);
			vmem.release(mem_base);
		}

	}
}
UNITTEST_SUITE_END
