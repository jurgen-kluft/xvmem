#ifndef _X_XVMEM_SIZE_DB_H_
#define _X_XVMEM_SIZE_DB_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xbase/x_allocator.h"

namespace xcore
{
    class xsize_db
    {
    public:
        enum
        {
            DB0_WIDTH = 64,
            DB1_WIDTH = 64,
            WIDTH_OCC = 64
        };

        void initialize(alloc_t*, u32 sizecnt, u32 addrcnt);
        void release(alloc_t*);
        void reset();

        void remove_size(u32 size_index, u32 addr_index);
        void insert_size(u32 size_index, u32 addr_index);
        bool find_size(u32& size_index, u32& addr_index) const;

        XCORE_CLASS_PLACEMENT_NEW_DELETE

    private:
        u64  m_size_occ[4];  // u64[4] (maximum of <= 256 unique sizes)
        u16  m_size_cnt;     // number of unique sizes that we are handling
        u16  m_size_db0_cnt; // db0 = first bit layer into db1
        u32  m_size_db1_cnt; // db1 = full bit layout of addrcnt for each size
        u64* m_size_db0;     // u64[m_size_db0_cnt] = 16 KB
        u64* m_size_db1;     // u64[m_size_db0_cnt * m_size_db1_cnt] = 128 KB
    };

} // namespace xcore

#endif // _X_XVMEM_SIZE_DB_H_