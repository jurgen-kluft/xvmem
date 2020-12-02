#ifndef _X_XVMEM_BINMAP_H_
#define _X_XVMEM_BINMAP_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xbase/x_debug.h"

namespace xcore
{

    struct binmap
    {
        struct config
        {
            config(u8 l1_len, u8 l2_len, u32 count)
                : m_l1_len(l1_len)
                , m_l2_len(l2_len)
                , m_count(count)
            {
                ASSERT((m_count <= 32) || (m_count <= (m_l2_len * 16)));
                ASSERT((m_count <= 32) || ((m_l1_len * 16) <= m_l2_len));
            }

            u8 const  m_l1_len;
            u8 const  m_l2_len;
            u16 const m_count;
        };

        binmap(u32 count)
            : m_l0(0)
            , m_l1_offset(0xffffffff)
            , m_l2_offset(0xffffffff)
        {
        }

        inline u16* get_l1() const { return (u16*)this + (sizeof(binmap) / 2); }
        inline u16* get_l2(config const& cfg) const { return (u16*)(this + (sizeof(binmap) / 2) + cfg.m_l1_len); }

        void init(config const& cfg);
        void set(config const& cfg, u32 bin);
        void clr(config const& cfg, u32 bin);
        bool get(config const& cfg, u32 bin) const;
        u32  find(config const& cfg) const;

        u32 m_l0;
        u32 m_l1_offset;
        u32 m_l2_offset;
    };

} // namespace xcore

#endif // _X_XVMEM_BINMAP_H_