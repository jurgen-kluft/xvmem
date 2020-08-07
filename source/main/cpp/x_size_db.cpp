#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"
#include "xbase/x_hibitset.h"

#include "xvmem/private/x_size_db.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    void xsize_db::initialize(xalloc* allocator, u32 sizecnt, u32 addrcnt)
    {
        m_size_cnt     = sizecnt;
        m_size_db1_cnt = (addrcnt + (DB1_WIDTH - 1)) / DB1_WIDTH;
        m_size_db0_cnt = (m_size_db1_cnt + (DB0_WIDTH - 1)) / DB0_WIDTH;
        m_size_db0     = (u64*)allocator->allocate(m_size_cnt * m_size_db0_cnt * (DB0_WIDTH / 8), sizeof(void*));
        m_size_db1     = (u64*)allocator->allocate(m_size_cnt * m_size_db1_cnt * (DB1_WIDTH / 8), sizeof(void*));
        reset();
    }

    void xsize_db::release(xalloc* allocator)
    {
        allocator->deallocate(m_size_db0);
        allocator->deallocate(m_size_db1);
    }

    void xsize_db::reset()
    {
        for (u32 i = 0; i < 4; ++i)
            m_size_occ[i] = 0;
        for (u16 i = 0; i < (m_size_cnt * m_size_db0_cnt); ++i)
            m_size_db0[i] = 0;
        for (u32 i = 0; i < (m_size_cnt * m_size_db1_cnt); ++i)
            m_size_db1[i] = 0;
    }

    void xsize_db::remove_size(u32 size_index, u32 addr_index)
    {
        ASSERT(size_index < m_size_cnt);
        ASSERT(addr_index < (m_size_db1_cnt * 64));
        u32 const awi    = addr_index / DB1_WIDTH;
        u32 const abi    = addr_index & (DB1_WIDTH - 1);
        u32 const ssi    = size_index;
        u32 const sdbi   = (ssi * m_size_db1_cnt) + awi;
        m_size_db1[sdbi] = m_size_db1[sdbi] & ~((u64)1 << abi);
        if (m_size_db1[sdbi] == 0)
        {
            m_size_db0[ssi] = m_size_db0[ssi] & ~((u64)1 << awi);
            if (m_size_db0[ssi] == 0)
            {
                // Also clear size occupancy
                u32 const owi = ssi / WIDTH_OCC;
                u32 const obi = ssi & (WIDTH_OCC - 1);
                ASSERT(owi < 4);
                m_size_occ[owi] = m_size_occ[owi] & ~((u64)1 << obi);
            }
        }
    }

    void xsize_db::insert_size(u32 size_index, u32 addr_index)
    {
        ASSERT(size_index < m_size_cnt);
        ASSERT(addr_index < (m_size_db1_cnt * 64));
        u32 const awi    = addr_index / DB1_WIDTH;
        u32 const abi    = addr_index & (DB1_WIDTH - 1);
        u32 const ssi    = size_index;
        u32 const sdbi   = (ssi * m_size_db1_cnt) + awi;
        u64 const osbi   = (m_size_db1[sdbi] & ((u64)1 << abi));
        if (osbi == 0)
        {
	        m_size_db1[sdbi] = m_size_db1[sdbi] | ((u64)1 << abi);
            u64 const osb0  = m_size_db0[ssi];
            m_size_db0[ssi] = m_size_db0[ssi] | ((u64)1 << awi);
            if (osb0 == 0)
            {
                // Also set size occupancy
                u32 const owi = ssi / WIDTH_OCC;
                u32 const obi = ssi & (WIDTH_OCC - 1);
                ASSERT(owi < 4);
                m_size_occ[owi] = m_size_occ[owi] | ((u64)1 << obi);
            }
        }
    }

    // Returns the addr node index that has a node with a best-fit 'size'
    bool xsize_db::find_size(u32& size_index, u32& addr_index) const
    {
        ASSERT(size_index < m_size_cnt);
        u32 owi = size_index / WIDTH_OCC;
        u32 obi = size_index & (WIDTH_OCC - 1);
        u64 obm = ~(((u64)1 << obi) - 1);
        u32 ssi = 0;
        if ((m_size_occ[owi] & obm) != 0)
        {
            ssi = (owi * WIDTH_OCC) + xfindFirstBit(m_size_occ[owi] & obm);
        }
        else
        {
            do
            {
                owi += 1;
            } while (owi < 4 && m_size_occ[owi] == 0);

            if (owi == 4)
                return false;

            ASSERT(owi < 4);
            ssi = (owi * WIDTH_OCC) + xfindFirstBit(m_size_occ[owi]);
        }

        size_index     = ssi;                                 // communicate back the size index we need to search for
        u32 const sdb0 = (u32)xfindFirstBit(m_size_db0[ssi]); // get the addr node that has that size
        u32 const sdbi = (ssi * m_size_db1_cnt) + sdb0;
        u32 const adbi = (u32)((sdb0 * DB1_WIDTH) + xfindFirstBit(m_size_db1[sdbi]));
        addr_index     = adbi;
        return true;
    }

} // namespace xcore