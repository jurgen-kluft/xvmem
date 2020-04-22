#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"


namespace xcore
{
    namespace xcoalescestrat_direct
    {
		class xsize_db
		{
		public:
			virtual void initialize(xalloc*) = 0;
			virtual void release(xalloc*) = 0;
			virtual void reset() = 0;
			virtual void remove_size(u32 size_index, u32 addr_index) = 0;
			virtual void insert_size(u32 size_index, u32 addr_index) = 0;
			virtual bool find_size(u32& size_index, u32& addr_index) const = 0;
		};

		class xsize_db_s128_a256 : public xsize_db
		{
		public:
			enum { DB0_WIDTH = 16, DB1_WIDTH = 16, SIZE_CNT = 128, SIZE_OCC = SIZE_CNT / 64, WIDTH_OCC = 64, ADDR_CNT = 256};
        
			virtual void initialize(xalloc*);
			virtual void release(xalloc*);
			virtual void reset();

			virtual void remove_size(u32 size_index, u32 addr_index);
			virtual void insert_size(u32 size_index, u32 addr_index);
			virtual bool find_size(u32& size_index, u32& addr_index) const = 0;

		private:
			u64  m_size_occ[SIZE_OCC]; // u64[2] (128 bits)
			u16* m_size_db0;           // u16[128]
			u16* m_size_db1;           // u16[128 * 16]
		};

		void xsize_db_s128_a256::initialize(xalloc* allocator)
		{
			m_size_db0 = (u16*)allocator->allocate(SIZE_CNT * (DB0_WIDTH/8), sizeof(void*));
			m_size_db1 = (u16*)allocator->allocate(SIZE_CNT * ((ADDR_CNT/DB1_WIDTH) * (DB1_WIDTH/8)), sizeof(void*));
			reset();
		}

		void xsize_db_s128_a256::release(xalloc* allocator)
		{
			allocator->deallocate(m_size_db0);
			allocator->deallocate(m_size_db1);
		}

		void xsize_db_s128_a256::reset()
		{
			for (s32 i = 0; i < SIZE_OCC; ++i)
				m_size_occ[i] = 0;
			for (s32 i = 0; i < SIZE_CNT; ++i)
				m_size_db0[i] = 0;
			for (s32 i = 0; i < (SIZE_CNT * DB0_WIDTH); ++i)
				m_size_db1[i] = 0;
		}

		void xsize_db_s128_a256::remove_size(u32 size_index, u32 addr_index)
		{
			ASSERT(size_index < SIZE_CNT);
			ASSERT(addr_index < ADDR_CNT);
			u32 const awi    = addr_index / DB1_WIDTH;
			u32 const abi    = addr_index & (DB1_WIDTH - 1);
			u32 const ssi    = size_index;
			u32 const sdbi   = (ssi * DB0_WIDTH) + awi;
			m_size_db1[sdbi] = m_size_db1[sdbi] & ~((u16)1 << abi);
			if (m_size_db1[sdbi] == 0)
			{
				m_size_db0[ssi] = m_size_db0[ssi] & ~((u16)1 << awi);
				if (m_size_db0[ssi] == 0)
				{
					// Also clear size occupancy
					u32 const owi    = ssi / WIDTH_OCC;
					u32 const obi    = ssi & (WIDTH_OCC - 1);
					ASSERT(owi < SIZE_OCC);
					m_size_occ[owi] = m_size_occ[owi] & ~((u64)1 << obi);
				}
			}
		}

		void xsize_db_s128_a256::insert_size(u32 size_index, u32 addr_index)
		{
			ASSERT(size_index < SIZE_CNT);
			ASSERT(addr_index < ADDR_CNT);
			u32 const awi    = addr_index / DB1_WIDTH;
			u32 const abi    = addr_index & (DB1_WIDTH - 1);
			u32 const ssi    = size_index;
			u32 const sdbi   = (ssi * DB0_WIDTH) + awi;
			u16 const osbi   = (m_size_db1[sdbi] & ((u16)1 << abi));
			m_size_db1[sdbi] = m_size_db1[sdbi] | (u16)(1 << abi);
			if (osbi == 0)
			{
				u16 const osb0  = m_size_db0[ssi];
				m_size_db0[ssi] = m_size_db0[ssi] | ((u16)1 << awi);
				if (osb0 == 0)
				{
					// Also set size occupancy
					u32 const owi    = ssi / WIDTH_OCC;
					u32 const obi    = ssi & (WIDTH_OCC - 1);
					ASSERT(owi < SIZE_OCC);
					m_size_occ[owi] = m_size_occ[owi] | ((u64)1 << obi);
				}
			}
		}

		// Returns the addr node index that has a node with a best-fit 'size'
		bool xsize_db_s128_a256::find_size(u32& size_index, u32& addr_index) const
		{
			ASSERT(size_index < SIZE_CNT);
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
				} while (owi < SIZE_OCC && m_size_occ[owi] == 0);

				if (owi == SIZE_OCC)
					return false;

				ASSERT(owi < SIZE_OCC);
				ssi = (owi * WIDTH_OCC) + xfindFirstBit(m_size_occ[owi]);
			}

			size_index     = ssi;                                 // communicate back the size index we need to search for
			u32 const sdb0 = (u32)xfindFirstBit(m_size_db0[ssi]); // get the addr node that has that size
			u32 const sdbi = (ssi * DB0_WIDTH) + sdb0;
			u32 const adbi = (u32)((sdb0 * DB1_WIDTH) + xfindFirstBit(m_size_db1[sdbi]));
			addr_index = adbi;
			return true;
		}


		class xsize_db_s256_a2048 : public xsize_db
		{
		public:
			enum { DB0_WIDTH = 64, DB1_WIDTH = 32, SIZE_CNT = 256, SIZE_OCC = SIZE_CNT / 64, WIDTH_OCC = 64, ADDR_CNT = 2048};

			virtual void initialize(xalloc*);
			virtual void release(xalloc*);
			virtual void reset();

			virtual void remove_size(u32 size_index, u32 addr_index);
			virtual void insert_size(u32 size_index, u32 addr_index);
			virtual bool find_size(u32& size_index, u32& addr_index) const;

		private:
			u64  m_size_occ[SIZE_OCC]; // u64[4] (2048 bits = 32 B)
			u64* m_size_db0;           // u64[256] = 16 KB
			u32* m_size_db1;           // u32[256 * 64] = 64 KB
		};


		void xsize_db_s256_a2048::initialize(xalloc* allocator)
		{
			m_size_db0 = (u64*)allocator->allocate(SIZE_CNT * (DB0_WIDTH/8), sizeof(void*));
			m_size_db1 = (u32*)allocator->allocate(SIZE_CNT * (DB0_WIDTH * (DB1_WIDTH/8)), sizeof(void*));
			reset();
		}

		void xsize_db_s256_a2048::release(xalloc* allocator)
		{
			allocator->deallocate(m_size_db0);
			allocator->deallocate(m_size_db1);
		}

		void xsize_db_s256_a2048::reset()
		{
			for (s32 i = 0; i < SIZE_OCC; ++i)
				m_size_occ[i] = 0;
			for (s32 i = 0; i < SIZE_CNT; ++i)
				m_size_db0[i] = 0;
			for (s32 i = 0; i < (SIZE_CNT * DB0_WIDTH); ++i)
				m_size_db1[i] = 0;
		}

		void xsize_db_s256_a2048::remove_size(u32 size_index, u32 addr_index)
		{
			ASSERT(size_index < SIZE_CNT);
			ASSERT(addr_index < ADDR_CNT);
			u32 const awi    = addr_index / DB1_WIDTH;
			u32 const abi    = addr_index & (DB1_WIDTH - 1);
			u32 const ssi    = size_index;
			u32 const sdbi   = (ssi * DB0_WIDTH) + awi;
			m_size_db1[sdbi] = m_size_db1[sdbi] & ~((u32)1 << abi);
			if (m_size_db1[sdbi] == 0)
			{
				m_size_db0[ssi] = m_size_db0[ssi] & ~((u64)1 << awi);
				if (m_size_db0[ssi] == 0)
				{
					// Also clear size occupancy
					u32 const owi    = ssi / WIDTH_OCC;
					u32 const obi    = ssi & (WIDTH_OCC - 1);
					ASSERT(owi < SIZE_OCC);
					m_size_occ[owi] = m_size_occ[owi] & ~((u64)1 << obi);
				}
			}
		}

		void xsize_db_s256_a2048::insert_size(u32 size_index, u32 addr_index)
		{
			ASSERT(size_index < SIZE_CNT);
			ASSERT(addr_index < ADDR_CNT);
			u32 const awi    = addr_index / DB1_WIDTH;
			u32 const abi    = addr_index & (DB1_WIDTH - 1);
			u32 const ssi    = size_index;
			u32 const sdbi   = (ssi * DB0_WIDTH) + awi;
			u32 const osbi   = (m_size_db1[sdbi] & ((u32)1 << abi));
			m_size_db1[sdbi] = m_size_db1[sdbi] | ((u32)1 << abi);
			if (osbi == 0)
			{
				u64 const osb0  = m_size_db0[ssi];
				m_size_db0[ssi] = m_size_db0[ssi] | ((u64)1 << awi);
				if (osb0 == 0)
				{
					// Also set size occupancy
					u32 const owi    = ssi / WIDTH_OCC;
					u32 const obi    = ssi & (WIDTH_OCC - 1);
					ASSERT(owi < SIZE_OCC);
					m_size_occ[owi] = m_size_occ[owi] | ((u64)1 << obi);
				}
			}
		}

		// Returns the addr node index that has a node with a best-fit 'size'
		bool xsize_db_s256_a2048::find_size(u32& size_index, u32& addr_index) const
		{
			ASSERT(size_index < SIZE_CNT);
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
				} while (owi < SIZE_OCC && m_size_occ[owi] == 0);

				if (owi == SIZE_OCC)
					return false;

				ASSERT(owi < SIZE_OCC);
				ssi = (owi * WIDTH_OCC) + xfindFirstBit(m_size_occ[owi]);
			}

			size_index     = ssi;                                 // communicate back the size index we need to search for
			u32 const sdb0 = (u32)xfindFirstBit(m_size_db0[ssi]); // get the addr node that has that size
			u32 const sdbi = (ssi * DB0_WIDTH) + sdb0;
			u32 const adbi = (u32)((sdb0 * DB1_WIDTH) + xfindFirstBit(m_size_db1[sdbi]));
			addr_index = adbi;
			return true;
		}


		class xsize_db_s256_a4096 : public xsize_db
		{
		public:
			enum { DB0_WIDTH = 64, DB1_WIDTH = 64, SIZE_CNT = 256, SIZE_OCC = SIZE_CNT / 64, WIDTH_OCC = 64, ADDR_CNT = 4096};

			virtual void initialize(xalloc*);
			virtual void release(xalloc*);
			virtual void reset();

			virtual void remove_size(u32 size_index, u32 addr_index);
			virtual void insert_size(u32 size_index, u32 addr_index);
			virtual bool find_size(u32& size_index, u32& addr_index) const;

		private:
			u64  m_size_occ[SIZE_OCC]; // u64[4] (2048 bits = 32 B)
			u64* m_size_db0;           // u64[256] = 16 KB
			u64* m_size_db1;           // u64[256 * 64] = 128 KB
		};


		void xsize_db_s256_a4096::initialize(xalloc* allocator)
		{
			m_size_db0 = (u64*)allocator->allocate(SIZE_CNT * (DB0_WIDTH/8), sizeof(void*));
			m_size_db1 = (u64*)allocator->allocate(SIZE_CNT * (DB0_WIDTH * (DB1_WIDTH/8)), sizeof(void*));
			reset();
		}

		void xsize_db_s256_a4096::release(xalloc* allocator)
		{
			allocator->deallocate(m_size_db0);
			allocator->deallocate(m_size_db1);
		}

		void xsize_db_s256_a4096::reset()
		{
			for (s32 i = 0; i < SIZE_OCC; ++i)
				m_size_occ[i] = 0;
			for (s32 i = 0; i < SIZE_CNT; ++i)
				m_size_db0[i] = 0;
			for (s32 i = 0; i < (SIZE_CNT * DB0_WIDTH); ++i)
				m_size_db1[i] = 0;
		}

		void xsize_db_s256_a4096::remove_size(u32 size_index, u32 addr_index)
		{
			ASSERT(size_index < SIZE_CNT);
			ASSERT(addr_index < ADDR_CNT);
			u32 const awi    = addr_index / DB1_WIDTH;
			u32 const abi    = addr_index & (DB1_WIDTH - 1);
			u32 const ssi    = size_index;
			u32 const sdbi   = (ssi * DB0_WIDTH) + awi;
			m_size_db1[sdbi] = m_size_db1[sdbi] & ~((u64)1 << abi);
			if (m_size_db1[sdbi] == 0)
			{
				m_size_db0[ssi] = m_size_db0[ssi] & ~((u64)1 << awi);
				if (m_size_db0[ssi] == 0)
				{
					// Also clear size occupancy
					u32 const owi    = ssi / WIDTH_OCC;
					u32 const obi    = ssi & (WIDTH_OCC - 1);
					ASSERT(owi < SIZE_OCC);
					m_size_occ[owi] = m_size_occ[owi] & ~((u64)1 << obi);
				}
			}
		}

		void xsize_db_s256_a4096::insert_size(u32 size_index, u32 addr_index)
		{
			ASSERT(size_index < SIZE_CNT);
			ASSERT(addr_index < ADDR_CNT);
			u32 const awi    = addr_index / DB1_WIDTH;
			u32 const abi    = addr_index & (DB1_WIDTH - 1);
			u32 const ssi    = size_index;
			u32 const sdbi   = (ssi * DB0_WIDTH) + awi;
			u64 const osbi   = (m_size_db1[sdbi] & ((u64)1 << abi));
			m_size_db1[sdbi] = m_size_db1[sdbi] | ((u64)1 << abi);
			if (osbi == 0)
			{
				u64 const osb0  = m_size_db0[ssi];
				m_size_db0[ssi] = m_size_db0[ssi] | ((u64)1 << awi);
				if (osb0 == 0)
				{
					// Also set size occupancy
					u32 const owi    = ssi / WIDTH_OCC;
					u32 const obi    = ssi & (WIDTH_OCC - 1);
					ASSERT(owi < SIZE_OCC);
					m_size_occ[owi] = m_size_occ[owi] | ((u64)1 << obi);
				}
			}
		}

		// Returns the addr node index that has a node with a best-fit 'size'
		bool xsize_db_s256_a4096::find_size(u32& size_index, u32& addr_index) const
		{
			ASSERT(size_index < SIZE_CNT);
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
				} while (owi < SIZE_OCC && m_size_occ[owi] == 0);

				if (owi == SIZE_OCC)
					return false;

				ASSERT(owi < SIZE_OCC);
				ssi = (owi * WIDTH_OCC) + xfindFirstBit(m_size_occ[owi]);
			}

			size_index     = ssi;                                 // communicate back the size index we need to search for
			u32 const sdb0 = (u32)xfindFirstBit(m_size_db0[ssi]); // get the addr node that has that size
			u32 const sdbi = (ssi * DB0_WIDTH) + sdb0;
			u32 const adbi = (u32)((sdb0 * DB1_WIDTH) + xfindFirstBit(m_size_db1[sdbi]));
			addr_index = adbi;
			return true;
		}
	}
} // namespace xcore