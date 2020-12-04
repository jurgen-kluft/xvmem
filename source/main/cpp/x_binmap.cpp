#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_binmap.h"

namespace xcore
{
    void resetarray(u32 count, u32 len, u16* data)
    {
        for (u32 i = 0; i < len; i++)
            data[i] = 0;
        u32 wi2 = count / 16;
        u32 wd2 = 0xffff << (count & (16 - 1));
        for (; wi2 < len; wi2++)
        {
            data[wi2] = wd2;
            wd2       = 0xffff;
        }
    }
    void binmap_t::init(config_t const& cfg)
    {
        // Set those bits that we never touch to '1' the rest to '0'
        u16 l0len = cfg.m_count;
        if (cfg.m_count > 32)
        {
            u16* const l2 = get_l2(cfg);
            resetarray(cfg.m_count, cfg.m_l2_len, l2);
            u16* const l1 = get_l1();
            resetarray(cfg.m_l2_len, cfg.m_l1_len, l1);
            l0len = cfg.m_l1_len;
        }
        m_l0 = 0xffffffff << (l0len & (32 - 1));
    }

    void binmap_t::set(config_t const& cfg, u32 k)
    {
        if (cfg.m_count <= 32)
        {
            u32 const bi0 = 1 << (k & (32 - 1));
            u32 const wd0 = m_l0 | bi0;
            m_l0          = wd0;
        }
        else
        {
            u16* const l2  = get_l2(cfg);
            u32 const  wi2 = k / 16;
            u16 const  bi2 = (u16)1 << (k & (16 - 1));
            u16 const  wd2 = l2[wi2] | bi2;
            if (wd2 == 0xffff)
            {
                u16* const l1  = get_l1();
                u32 const  wi1 = wi2 / 16;
                u16 const  bi1 = 1 << (wi1 & (16 - 1));
                u16 const  wd1 = l1[wi1] | bi1;
                if (wd1 == 0xffff)
                {
                    u32 const bi0 = 1 << (wi1 & (32 - 1));
                    u32 const wd0 = m_l0 | bi0;
                    m_l0          = wd0;
                }
                l1[wi1] = wd1;
            }
            l2[wi2] = wd2;
        }
    }

    void binmap_t::clr(config_t const& cfg, u32 k)
    {
        if (cfg.m_count <= 32)
        {
            u32 const bi0 = 1 << (k & (32 - 1));
            u32 const wd0 = m_l0 & ~bi0;
            m_l0          = wd0;
        }
        else
        {
            u16* const l2  = get_l2(cfg);
            u32 const  wi2 = k / 16;
            u16 const  bi2 = (u16)1 << (k & (16 - 1));
            u16 const  wd2 = l2[wi2];
            if (wd2 == 0xffff)
            {
                u16* const l1 = get_l1();

                u32 const wi1 = wi2 / 16;
                u16 const bi1 = 1 << (wi1 & (16 - 1));
                u16 const wd1 = l1[wi1];
                if (wd1 == 0xffff)
                {
                    u32 const bi0 = 1 << (wi1 & (32 - 1));
                    u32 const wd0 = m_l0 & ~bi0;
                    m_l0          = wd0;
                }
                l1[wi1] = wd1 & ~bi1;
            }
            l2[wi2] = wd2 & ~bi2;
        }
    }

    bool binmap_t::get(config_t const& cfg, u32 k) const
    {
        if (cfg.m_count <= 32)
        {
            u32 const bi0 = 1 << (k & (32 - 1));
            return (m_l0 & bi0) != 0;
        }
        else
        {
            u16* const l2  = get_l2(cfg);
            u32 const  wi2 = k / 16;
            u16 const  bi2 = (u16)1 << (k & (16 - 1));
            u16 const  wd2 = l2[wi2];
            return (wd2 & bi2) != 0;
        }
    }

    u32 binmap_t::find(config_t const& cfg) const
    {
        u32 const bi0 = (u32)xfindFirstBit(~m_l0);
        if (cfg.m_count <= 32)
        {
            return bi0;
        }
        else
        {
            u32 const  wi1 = bi0 * 16;
            u16* const l1  = get_l1();
            u32 const  bi1 = (u32)xfindFirstBit((u16)~l1[wi1]);
            u32 const  wi2 = bi1 * 16;
            u16* const l2  = get_l2(cfg);
            u32 const  bi2 = (u32)xfindFirstBit((u16)~l2[wi2]);
            return bi2 + wi2 * 16;
        }
    }
} // namespace xcore
