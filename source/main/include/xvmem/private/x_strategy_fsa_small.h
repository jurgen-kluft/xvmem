#ifndef _X_ALLOCATOR_FSA_STRATEGY_H_
#define _X_ALLOCATOR_FSA_STRATEGY_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
	namespace xfsastrat
	{
		struct xpages_t;
		struct xlist_t
		{
			xlist_t()
				: m_count(0)
				, m_list(0xffff)
			{
			}
			u16 m_count;
			u16 m_list;
		};

		xpages_t*    create(xalloc* main_allocator, void* base_address, u64 memory_range, u32 page_size);
		void         destroy(xpages_t* pages);
		void*        alloc_page(xpages_t* pages, xlist_t& page_list, u32 const elem_size);
		void*        free_one_page(xpages_t* pages, xlist_t& page_list);
		void         free_all_pages(xpages_t* pages, xlist_t& page_list);
		void*        alloc_elem(xpages_t* pages, xlist_t& page_list, u32 const elem_size);
		u32          sizeof_elem(xpages_t* pages, void* const ptr);
		u32          idx_of_elem(xpages_t* pages, void* const ptr);
		void*        ptr_of_elem(xpages_t* pages, u32 const index);
		void         free_elem(xpages_t* pages, xlist_t& page_list, void* const ptr, xlist_t& pages_empty_list);
	}

    // Usage:
    //   Create an instance of ``xfsastrat::xpages_t`` by calling ``xfsastrat::create()``
    //   When you are done and want to release the instance, call ``xfsastrat::destroy()``

    // ``xfsastrat::alloc_elem()``:
    //   When this function returns NULL the next thing to do is to call ``alloc_page``.
    //   You will receive a ``void*`` that can be used to do an actual virtual memory commit.
    //
    // ``xfsastrat::free_elem()``:
    //   Freeing an element can result in a page becoming ``empty``, this will be returned in
    //   ``pages_empty_list``. You can see how many pages are in the list and if you want to
    //   remove one (or more) you can repeatedly call ``free_one_page``.
    //   If you want to free every item of the page list you can call ``free_all_pages``.
    //

} // namespace xcore

#endif // _X_ALLOCATOR_FSA_STRATEGY_H_