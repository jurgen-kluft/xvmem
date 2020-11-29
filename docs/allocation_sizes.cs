using System;
using System.Collections.Generic;

namespace SuperAlloc
{
	public class Program
	{
		public static int AllocSizeToBin(UInt64 size)
		{
			UInt64 f = FloorPo2(size);
			int r = CountTrailingZeros(f >> 4) * 4;
			int t = CountTrailingZeros(AlignTo32(f) >> 2);
			int i = (int)((size - (f & ~((UInt64)32 - 1))) >> t);
			i += r;
			return i - 1;
		}

		public static UInt64 AlignAllocSize(UInt64 size)
		{
			size = AlignTo8(size);
			UInt64 f = FloorPo2(size);
			f = AlignTo8(f);
			size = f + ((size - f) + ((f >> 2) - 1) & ~((f >> 2) - 1));
			return size;
		}

		public struct BinMapConfig
		{
			public uint Count { get; set; }
			public uint L1Len { get; set; }
			public uint L2Len { get; set; }
		}

		public static UInt64 CalcAddressRange(UInt64 size)
		{
			UInt64 addressrange;
			if (size <= 256) addressrange = 256 * 1024 * 1024;
			else if (size <= 2048) addressrange = 256 * 1024 * 1024;
			else if (size <= 24576) addressrange = 1 * 1024 * 1024 * 1024;
			else if (size < 512 * 1024) addressrange = (UInt64)8 * 1024 * 1024 * 1024;
			else if (size < 16 * 1024 * 1024) addressrange = (UInt64)16 * 1024 * 1024 * 1024;
			else addressrange = (UInt64)16 * 1024 * 1024 * 1024;
			return addressrange;
		}

		public static UInt64 CalcChunkSize(UInt64 s)
		{
			UInt64 size = s;
			UInt64 chunksize;
			if (size <= 256) chunksize = 64 * 1024;
			else if (size <= 2048) chunksize = 512 * 1024;
			else if (size <= 24576) chunksize = 4 * 1024 * 1024;
			else if (size < 512 * 1024) chunksize = 32 * 1024 * 1024;
			else if (size < 16 * 1024 * 1024) chunksize = 64 * 1024 * 1024;
			else chunksize = (UInt64)1 * 1024 * 1024 * 1024;
			return chunksize;
		}

		public static int CalcPageTrackingGranularity(UInt64 s)
		{
			if (s < (2 * 65536)) return 1;
			else return (int)(s / 65536);
		}

		public static int CalcPageTrackingNodeBits(UInt64 s)
		{
			if (s <= 256) return 16;
			else if (s < 65536) return 8;
			else if (s < (512 * 1024)) return 8;
			return 16;
		}

		public static string CalcPageTrackingMode(UInt64 s)
		{
			if (s <= 256) return "Ref";
			else if (s < 65536) return "Ref";
			else if (s < (512 * 1024)) return "Ref";
			return "Count";
		}

		public static UInt64 CalcMemoryFootPrint()
		{
			// Need: how many allocations of each size


			return 0;
		}

		public static BinMapConfig CalcBinMap(UInt64 s)
		{
			UInt64 size = s;
			UInt64 chunksize = CalcChunkSize(s);

			UInt64 l2_len = ((chunksize / size / (UInt64)16) + (UInt64)3) & ~((UInt64)3);
			if ((chunksize / size / 16) > 16)
			{
				l2_len = (((l2_len / (UInt64)4) * (UInt64)4) + (UInt64)3) & ~((UInt64)3);
			}
			else
			{
				l2_len = ((chunksize / size / 16) + (UInt64)1) & ~((UInt64)1);
			}
			UInt64 l1_len = (l2_len + 15) / (UInt64)16;
			if (l1_len < 2) l1_len = 2;

			BinMapConfig bm = new BinMapConfig();
			bm.Count = (uint)(chunksize / size);
			if (bm.Count <= 32)
			{
				bm.L1Len = 0;
				bm.L2Len = 0;
			}
			else
			{
				bm.L1Len = (uint)l1_len;
				bm.L2Len = (uint)l2_len;
			}
			return bm;
		}

		public static void Main()
		{
			UInt64 page = 64 * 1024;
			foreach (AllocSize size in Sizes)
			{
				BinMapConfig bm = CalcBinMap(size.Size);
				UInt64 chunkSize = CalcChunkSize(size.Size);
				UInt64 c = (chunkSize / size.Size);
				UInt64 pages;
				if (chunkSize > 65536)
				{
					do
					{
						if (((c * size.Size) & (page - 1)) == 0)
							break;
						c--;
					} while (true);
					pages = (c * size.Size) / page;
				}
				else
				{
					pages = 1;
				}

				Console.WriteLine("{0}: Alloc(Size:{1}, Count:{4}), Pages:{2}, Chunk:{3}, BinMapSize(4,{5},{6}):{7}, Tracking(M:{8},P:{9},W:{10})", AllocSizeToBin(size.Size), size.Size, pages, CalcChunkSize(size.Size).ToByteSize(), c, bm.L1Len, bm.L2Len, 4 + 2 * (bm.L1Len + bm.L2Len), CalcPageTrackingMode(size.Size), CalcPageTrackingGranularity(size.Size), CalcPageTrackingNodeBits(size.Size));
			}
		}

		public class PageTracker
		{
			public int PageGranularity { get; set; } = 0;
			public int NodeBits { get; set; } = 0;
			public string Mode { get; set; } = "Ref";
		}

		static PageTracker[] PageTrackers = new PageTracker[] {
			new PageTracker() { PageGranularity=1, NodeBits=16, Mode= "Ref" },
			new PageTracker { PageGranularity= 1, NodeBits=  8, Mode= "Ref" },
			new PageTracker { PageGranularity= 1, NodeBits=  8, Mode= "Ref" },
			new PageTracker { PageGranularity= 1, NodeBits=  8, Mode= "Ref" },
			new PageTracker { PageGranularity= 1, NodeBits= 16, Mode= "Count" },
			new PageTracker { PageGranularity= 1, NodeBits= 16, Mode= "Count" },
		};

		public class Allocator
		{
			public int Index { get; set; } = 0;
			public UInt64 AddressRange { get; set; } = 0;
			public UInt64 ChunkSize { get; set; } = 0;
			public int ChunkCount { get; set; } = 0;
			public List<AllocSize> AllocSizes { get; set; } = new List<AllocSize>();
			public PageTracker PageTracker { get; set; }
			public UInt64 MemoryFootPrint { get; set; } = 0;
		};

		static Allocator[] Allocators = new Allocator[] {
			new Allocator { Index=0, AddressRange= MB(512), ChunkSize= KB(64) },  // Nodes=8K (u16)
			new Allocator { Index=1, AddressRange= GB(1), ChunkSize= KB(512) },   // Nodes=16K (u8)
			new Allocator { Index=2, AddressRange= GB(1), ChunkSize= MB(4) },     // Nodes=16K (u8)
			new Allocator { Index=3, AddressRange= GB(1), ChunkSize= MB(32) },    // Nodes=16K (u8)
			new Allocator { Index=4, AddressRange= GB(4), ChunkSize= MB(64) },    // Nodes=8K (u16)
			new Allocator { Index=5, AddressRange= GB(16), ChunkSize= GB(1) },   // Nodes=1K (u16)
		};

		public struct AllocSize
		{
			public UInt64 Size { get; set; }
			public Allocator Allocator { get; set; }
			public PageTracker PageTracker { get; set; }
		};

		static AllocSize[] Sizes = new AllocSize[] {
			// The page-tracking needs to track the ref-count of a page
			new AllocSize { Size=8, Allocator= Allocators[0], PageTracker= PageTrackers[0] },
			new AllocSize { Size=16, Allocator= Allocators[0], PageTracker= PageTrackers[0] },
			new AllocSize { Size=24, Allocator= Allocators[0], PageTracker= PageTrackers[0] },
			new AllocSize { Size=32, Allocator= Allocators[0], PageTracker= PageTrackers[0] },
			new AllocSize { Size=40, Allocator= Allocators[0], PageTracker= PageTrackers[0] },
			new AllocSize { Size=48, Allocator= Allocators[0], PageTracker= PageTrackers[0] },
			new AllocSize { Size=56, Allocator= Allocators[0], PageTracker= PageTrackers[0] },
			new AllocSize { Size=64, Allocator= Allocators[0], PageTracker= PageTrackers[0] },
			new AllocSize { Size=80, Allocator= Allocators[0], PageTracker= PageTrackers[0] },
			new AllocSize { Size=96, Allocator= Allocators[0], PageTracker= PageTrackers[0] },
			new AllocSize { Size=112, Allocator= Allocators[0], PageTracker= PageTrackers[0] },
			new AllocSize { Size=128, Allocator= Allocators[0], PageTracker= PageTrackers[0] },
			new AllocSize { Size=160, Allocator= Allocators[0], PageTracker= PageTrackers[0] },
			new AllocSize { Size=192, Allocator= Allocators[0], PageTracker= PageTrackers[0] },
			new AllocSize { Size=224, Allocator= Allocators[0], PageTracker= PageTrackers[0] },
			new AllocSize { Size=256, Allocator= Allocators[0], PageTracker= PageTrackers[0] },

			new AllocSize { Size= 320, Allocator= Allocators[1], PageTracker= PageTrackers[1] },
			new AllocSize { Size= 384, Allocator= Allocators[1], PageTracker= PageTrackers[1] },
			new AllocSize { Size= 448, Allocator= Allocators[1], PageTracker= PageTrackers[1] },
			new AllocSize { Size= 512, Allocator= Allocators[1], PageTracker= PageTrackers[1] },
			new AllocSize { Size= 640, Allocator= Allocators[1], PageTracker= PageTrackers[1] },
			new AllocSize { Size= 768, Allocator= Allocators[1], PageTracker= PageTrackers[1] },
			new AllocSize { Size= 896, Allocator= Allocators[1], PageTracker= PageTrackers[1] },
			new AllocSize { Size= 1024, Allocator= Allocators[1], PageTracker= PageTrackers[1] },
			new AllocSize { Size= 1280, Allocator= Allocators[1], PageTracker= PageTrackers[1] },
			new AllocSize { Size= 1536, Allocator= Allocators[1], PageTracker= PageTrackers[1] },
			new AllocSize { Size= 1792, Allocator= Allocators[1], PageTracker= PageTrackers[1] },
			new AllocSize { Size= 2048, Allocator= Allocators[1], PageTracker= PageTrackers[1] },

			new AllocSize { Size= 2560, Allocator= Allocators[2], PageTracker= PageTrackers[2] },
			new AllocSize { Size= 3072, Allocator= Allocators[2], PageTracker= PageTrackers[2] },
			new AllocSize { Size= 3584, Allocator= Allocators[2], PageTracker= PageTrackers[2] },
			new AllocSize { Size= KB(4), Allocator= Allocators[2], PageTracker= PageTrackers[2] },
			new AllocSize { Size= KB(5), Allocator= Allocators[2], PageTracker= PageTrackers[2] },
			new AllocSize { Size= KB(6), Allocator= Allocators[2], PageTracker= PageTrackers[2] },
			new AllocSize { Size= KB(7), Allocator= Allocators[2], PageTracker= PageTrackers[2] },
			new AllocSize { Size= KB(8), Allocator= Allocators[2], PageTracker= PageTrackers[2] },
			new AllocSize { Size= KB(10), Allocator= Allocators[2], PageTracker= PageTrackers[2] },
			new AllocSize { Size= KB(12), Allocator= Allocators[2], PageTracker= PageTrackers[2] },
			new AllocSize { Size= KB(14), Allocator= Allocators[2], PageTracker= PageTrackers[2] },
			new AllocSize { Size= KB(16), Allocator= Allocators[2], PageTracker= PageTrackers[2] },
			new AllocSize { Size= KB(20), Allocator= Allocators[2], PageTracker= PageTrackers[2] },
			new AllocSize { Size= KB(24), Allocator= Allocators[2], PageTracker= PageTrackers[2] },

			new AllocSize { Size=(UInt64)KB(28), Allocator= Allocators[3], PageTracker= PageTrackers[3] },
			new AllocSize { Size=(UInt64)KB(32), Allocator= Allocators[3], PageTracker= PageTrackers[3] },
			new AllocSize { Size=(UInt64)KB(36), Allocator= Allocators[3], PageTracker= PageTrackers[3] },
			new AllocSize { Size=(UInt64)KB(40), Allocator= Allocators[3], PageTracker= PageTrackers[3] },
			new AllocSize { Size=(UInt64)KB(48), Allocator= Allocators[3], PageTracker= PageTrackers[3] },
			new AllocSize { Size=(UInt64)KB(56), Allocator= Allocators[3], PageTracker= PageTrackers[3] },
			new AllocSize { Size=(UInt64)KB(64), Allocator= Allocators[3], PageTracker= PageTrackers[3] },
			new AllocSize { Size=(UInt64)KB(80), Allocator= Allocators[3], PageTracker= PageTrackers[3] },
			new AllocSize { Size=(UInt64)KB(96), Allocator= Allocators[3], PageTracker= PageTrackers[3] },
			new AllocSize { Size=(UInt64)KB(112), Allocator= Allocators[3], PageTracker= PageTrackers[3] },
			new AllocSize { Size=(UInt64)KB(128), Allocator= Allocators[3], PageTracker= PageTrackers[3] },
			new AllocSize { Size=(UInt64)KB(160), Allocator= Allocators[3], PageTracker= PageTrackers[3] },
			new AllocSize { Size=(UInt64)KB(192), Allocator= Allocators[3], PageTracker= PageTrackers[3] },
			new AllocSize { Size=(UInt64)KB(224), Allocator= Allocators[3], PageTracker= PageTrackers[3] },

			// From here the page-tracking needs to store the actual pages committed (and not the ref-count)
			// However the granularity for tracking is different
			new AllocSize { Size=(UInt64)KB(256), Allocator= Allocators[3], PageTracker= PageTrackers[3] },
			new AllocSize { Size=(UInt64)KB(320), Allocator= Allocators[3], PageTracker= PageTrackers[3] },
			new AllocSize { Size=(UInt64)KB(384), Allocator= Allocators[3], PageTracker= PageTrackers[3] },
			new AllocSize { Size=(UInt64)KB(448), Allocator= Allocators[3], PageTracker= PageTrackers[3] },

			new AllocSize { Size=(UInt64)KB(512), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)KB(640), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)KB(768), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)KB(896), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)KB(1024), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)KB(1280), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)KB(1536), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)KB(1792), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)KB(2048), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)KB(2560), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)KB(3072), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)KB(3584), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)MB(4), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)MB(5), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)MB(6), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)MB(7), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)MB(8), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)MB(10), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)MB(12), Allocator= Allocators[4], PageTracker= PageTrackers[4] },
			new AllocSize { Size=(UInt64)MB(14), Allocator= Allocators[4], PageTracker= PageTrackers[4] },

			new AllocSize { Size = (UInt64)MB(16), Allocator= Allocators[5], PageTracker= PageTrackers[5] },
			new AllocSize { Size = (UInt64)MB(20), Allocator= Allocators[5], PageTracker= PageTrackers[5] },
			new AllocSize { Size = (UInt64)MB(24), Allocator= Allocators[5], PageTracker= PageTrackers[5] },
			new AllocSize { Size = (UInt64)MB(28), Allocator= Allocators[5], PageTracker= PageTrackers[5] },
			new AllocSize { Size = (UInt64)MB(32), Allocator= Allocators[5], PageTracker= PageTrackers[5] },
			new AllocSize { Size = (UInt64)MB(40), Allocator= Allocators[5], PageTracker= PageTrackers[5] },
			new AllocSize { Size = (UInt64)MB(48), Allocator= Allocators[5], PageTracker= PageTrackers[5] },
			new AllocSize { Size = (UInt64)MB(56), Allocator= Allocators[5], PageTracker= PageTrackers[5] },
			new AllocSize { Size = (UInt64)MB(64), Allocator= Allocators[5], PageTracker= PageTrackers[5] },
			new AllocSize { Size = (UInt64)MB(80), Allocator= Allocators[5], PageTracker= PageTrackers[5] },
			new AllocSize { Size = (UInt64)MB(96), Allocator= Allocators[5], PageTracker= PageTrackers[5] },
			new AllocSize { Size = (UInt64)MB(112), Allocator= Allocators[5], PageTracker= PageTrackers[5] },
			new AllocSize { Size = (UInt64)MB(128), Allocator= Allocators[5], PageTracker= PageTrackers[5] },
			new AllocSize { Size = (UInt64)MB(160), Allocator= Allocators[5], PageTracker= PageTrackers[5] },
			new AllocSize { Size = (UInt64)MB(192), Allocator= Allocators[5], PageTracker= PageTrackers[5] },
			new AllocSize { Size = (UInt64)MB(224), Allocator= Allocators[5], PageTracker= PageTrackers[5] },
			new AllocSize { Size = (UInt64)MB(256), Allocator= Allocators[5], PageTracker= PageTrackers[5] },
		};

		static UInt64 KB(int mb) { return (UInt64)mb * 1024; }
		static UInt64 MB(int mb) { return (UInt64)mb * 1024 * 1024; }
		static UInt64 GB(int mb) { return (UInt64)mb * 1024 * 1024 * 1024; }


		public static UInt64 CeilPo2(UInt64 v)
		{
			int w = CountLeadingZeros(v);
			UInt64 l = (UInt64)0x8000000000000000 >> w;
			if (l == v) return v;
			return l << 1;
		}

		public static UInt64 FloorPo2(UInt64 v)
		{
			int w = CountLeadingZeros(v);
			UInt64 l = (UInt64)0x8000000000000000 >> w;
			if (l == v) return v;
			return l;
		}

		public static int CountLeadingZeros(UInt64 integer)
		{
			if (integer == 0)
				return 64;

			int count = 0;
			if ((integer & 0xFFFFFFFF00000000UL) == 0)
			{
				count += 32;
				integer = integer << 32;
			}
			if ((integer & 0xFFFF000000000000UL) == 0)
			{
				count += 16;
				integer = integer << 16;
			}
			if ((integer & 0xFF00000000000000UL) == 0)
			{
				count += 8;
				integer = integer << 8;
			}
			if ((integer & 0xF000000000000000UL) == 0)
			{
				count += 4;
				integer = integer << 4;
			}
			if ((integer & 0xC000000000000000UL) == 0)
			{
				count += 2;
				integer = integer << 2;
			}
			if ((integer & 0x8000000000000000UL) == 0)
			{
				count += 1;
				integer = integer << 1;
			}
			if ((integer & 0x8000000000000000UL) == 0)
			{
				count += 1;
			}
			return count;
		}

		public static int CountTrailingZeros(UInt64 integer)
		{
			int count = 0;
			if ((integer & 0xFFFFFFFF) == 0)
			{
				count += 32;
				integer = integer >> 32;
			}
			if ((integer & 0x0000FFFF) == 0)
			{
				count += 16;
				integer = integer >> 16;
			}
			if ((integer & 0x000000FF) == 0)
			{
				count += 8;
				integer = integer >> 8;
			}
			if ((integer & 0x0000000F) == 0)
			{
				count += 4;
				integer = integer >> 4;
			}
			if ((integer & 0x00000003) == 0)
			{
				count += 2;
				integer = integer >> 2;
			}
			if ((integer & 0x00000001) == 0)
			{
				count += 1;
				integer = integer >> 1;
			}
			if ((integer & 0x00000001) == 1)
			{
				return count;
			}
			return 0;
		}

		public static bool IsPowerOf2(UInt64 v)
		{
			return (v & (v - 1)) == 0;
		}

		public static UInt64 AlignTo8(UInt64 v)
		{
			return (v + (8 - 1)) & ~((UInt64)8 - 1);
		}

		public static UInt64 AlignTo16(UInt64 v)
		{
			return (v + (16 - 1)) & ~((UInt64)16 - 1);
		}
		public static UInt64 AlignTo32(UInt64 v)
		{
			return (v + (32 - 1)) & ~((UInt64)32 - 1);
		}
	}

	public static class IntExtensions
	{
		public static string ToByteSize(this int size)
		{
			return String.Format(new FileSizeFormatProvider(), "{0:fs}", size);
		}

		public static string ToByteSize(this Int64 size)
		{
			return String.Format(new FileSizeFormatProvider(), "{0:fs}", size);
		}
		public static string ToByteSize(this UInt64 size)
		{
			return String.Format(new FileSizeFormatProvider(), "{0:fs}", size);
		}

		public struct FileSize : IFormattable
		{
			private ulong _value;

			private const int DEFAULT_PRECISION = 2;

			private static IList<string> Units = new List<string>() { "bytes", "KB", "MB", "GB", "TB" };

			public FileSize(ulong value)
			{
				_value = value;
			}

			public static explicit operator FileSize(ulong value)
			{
				return new FileSize(value);
			}

			override public string ToString()
			{
				return ToString(null, null);
			}

			public string ToString(string format)
			{
				return ToString(format, null);
			}

			public string ToString(string format, IFormatProvider formatProvider)
			{
				int precision;

				if (String.IsNullOrEmpty(format))
					return ToString(DEFAULT_PRECISION);
				else if (int.TryParse(format, out precision))
					return ToString(precision);
				else
					return _value.ToString(format, formatProvider);
			}

			/// <summary>
			/// Formats the FileSize using the given number of decimals.
			/// </summary>
			public string ToString(int precision)
			{
				double pow = Math.Floor((_value > 0 ? Math.Log(_value) : 0) / Math.Log(1024));
				pow = Math.Min(pow, Units.Count - 1);
				double value = (double)_value / Math.Pow(1024, pow);
				return value.ToString(pow == 0 ? "F0" : "F" + precision.ToString()) + " " + Units[(int)pow];
			}
		}

		public class FileSizeFormatProvider : IFormatProvider, ICustomFormatter
		{
			public object GetFormat(Type formatType)
			{
				if (formatType == typeof(ICustomFormatter)) return this;
				return null;
			}

			/// <summary>
			/// Usage Examples:
			///		Console2.WriteLine(String.Format(new FileSizeFormatProvider(), "File size: {0:fs}", 100));
			/// </summary>

			private const string fileSizeFormat = "fs";
			private const Decimal OneKiloByte = 1024M;
			private const Decimal OneMegaByte = OneKiloByte * 1024M;
			private const Decimal OneGigaByte = OneMegaByte * 1024M;

			public string Format(string format, object arg, IFormatProvider formatProvider)
			{
				if (format == null || !format.StartsWith(fileSizeFormat))
				{
					return defaultFormat(format, arg, formatProvider);
				}

				if (arg is string)
				{
					return defaultFormat(format, arg, formatProvider);
				}

				Decimal size;

				try
				{
					size = Convert.ToDecimal(arg);
				}
				catch (InvalidCastException)
				{
					return defaultFormat(format, arg, formatProvider);
				}

				string suffix;
				if (size >= OneGigaByte)
				{
					size /= OneGigaByte;
					suffix = " GB";
				}
				else if (size >= OneMegaByte)
				{
					size /= OneMegaByte;
					suffix = " MB";
				}
				else if (size >= OneKiloByte)
				{
					size /= OneKiloByte;
					suffix = " kB";
				}
				else
				{
					suffix = " B";
				}

				string precision = format.Substring(2);
				if (String.IsNullOrEmpty(precision)) precision = "2";
				return String.Format("{0:N" + precision + "}{1}", size, suffix);

			}

			private static string defaultFormat(string format, object arg, IFormatProvider formatProvider)
			{
				IFormattable formattableArg = arg as IFormattable;
				if (formattableArg != null)
				{
					return formattableArg.ToString(format, formatProvider);
				}
				return arg.ToString();
			}

		}
	}
}
