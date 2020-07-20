#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_fsa_page.h"

#include "xunittest/xunittest.h"

using namespace xcore;

extern xalloc* gTestAllocator;

UNITTEST_SUITE_BEGIN(strategy_fsa_page)
{
    UNITTEST_FIXTURE(main)
    {
		u64 const sOnePageSize = 64 * 1024;

		void* AllocatePage()
		{
			return gTestAllocator->allocate(sOnePageSize, sizeof(void*));
		}
		void DeallocatePage(void* p)
		{
			gTestAllocator->deallocate(p);
		}

        UNITTEST_FIXTURE_SETUP()
        {
        }

        UNITTEST_FIXTURE_TEARDOWN()
        {
        }

        UNITTEST_TEST(init)
        {
			xpage_t p;
			p.init(sOnePageSize, 16);
			CHECK_EQUAL(true, p.is_empty());
			CHECK_EQUAL(false, p.is_full());
        }

        UNITTEST_TEST(is_full)
        {
			xpage_t p;
			p.init(sOnePageSize, 16);
			CHECK_EQUAL(false, p.is_full());
			p.m_elem_used = p.m_elem_total;
			CHECK_EQUAL(true, p.is_full());
			p.m_elem_used = 0;
			CHECK_EQUAL(false, p.is_full());
        }

        UNITTEST_TEST(allocate_deallocate_1)
        {
			xpage_t p;
			p.init(sOnePageSize, 16);

			void* page_data = AllocatePage();

			void* p1 = p.allocate(page_data);
			CHECK_EQUAL(x_advance_ptr(page_data, 0), p1);
			p.deallocate(page_data, p1);

			DeallocatePage(page_data);
        }

        UNITTEST_TEST(allocate_deallocate_16_max)
        {
			const u32 allocsize = 16;
			const u32 alloccount = 64*1024 / allocsize;

			xpage_t p;
			p.init(sOnePageSize, allocsize);

			void* page_data = AllocatePage();
			for (s32 i=0; i<alloccount; i++)
			{
				p.allocate(page_data);
			}
			CHECK_EQUAL(true, p.is_full());
			for (s32 i=0; i<alloccount; i++)
			{
				void* p1 = x_advance_ptr(page_data, i * allocsize);
				p.deallocate(page_data, p1);
			}
			CHECK_EQUAL(true, p.is_empty());

			DeallocatePage(page_data);
        }

		UNITTEST_TEST(allocate_deallocate_x_max)
        {
			void* page_data = AllocatePage();
			for (u32 i=16; i< 1024; i+=16)
			{
				u32 allocsize = i;
				u32 alloccount = 64*1024 / allocsize;

				xpage_t p;
				p.init(sOnePageSize, allocsize);

				for (u32 i=0; i<alloccount; i++)
				{
					void* p1 = p.allocate(page_data);
					void* p2 = x_advance_ptr(page_data, i * allocsize);
					CHECK_EQUAL(p2, p1);
				}
				CHECK_EQUAL(true, p.is_full());
				for (u32 i=0; i<alloccount; i++)
				{
					void* p1 = x_advance_ptr(page_data, i * allocsize);
					p.deallocate(page_data, p1);
				}
				CHECK_EQUAL(true, p.is_empty());
			}
			DeallocatePage(page_data);
        }

    }
}
UNITTEST_SUITE_END
