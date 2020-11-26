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

		public static UInt64 AllocSize(UInt64 size)
		{
			size = AlignTo8(size);
			UInt64 f = FloorPo2(size);
			f = AlignTo8(f);
			size = f + ((size - f) + ((f >> 2) - 1) & ~((f >> 2) - 1));
			return size;
		}

		public struct BinMap
		{
			public UInt64 ChunkSize { get; set; }
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

		public static BinMap CalcChunkSize(UInt64 s)
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

		public static BinMap CalcBinMap(UInt64 size)
		{
			UInt64 chunksize = CalcChunkSize(size);

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

			BinMap bm = new BinMap();
			bm.ChunkSize = (UInt64)chunksize;
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
			foreach (UInt64 size in Sizes)
			{
				BinMap bm = CalcBinMap(size);
				UInt64 c = (bm.ChunkSize / size);
				UInt64 pages;
				if (bm.ChunkSize > 65536)
				{
					do
					{
						if (((c * size) & (page - 1)) == 0)
							break;
						c--;
					} while (true);
					pages = (c * size) / page;
				}
				else
				{
					pages = 1;
				}

				Console.WriteLine("{0}: Size:{1}, Pages:{2}, ChunkSize:{3}, Count:{4}, u32:u16[{5}]:u16[{6}], BinMapSize:{7}", AllocSizeToBin(size), size, pages, bm.ChunkSize.ToByteSize(), c, bm.L1Len, bm.L2Len, 4 + 2 * (bm.L1Len + bm.L2Len));
			}
		}

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

		static UInt64[] Sizes = new UInt64[] {
			8,16,24,32,40,48,56,64,
			80,96,112,128,
			160,192,224,256,
			320,384,448,512,
			640,768,896,1024,
			1280,1536,1792,2048,
			2560,3072,3584,4096,
			5120,
			6144,
			7168,
			8192,
			10240,
			12288,
			14336,
			16384,
			20480,
			24576,
			28672,
			32768,
			36864,
			40960,
			49152,
			57344,
			65536,

			(UInt64)( 80*1024),
			(UInt64)( 96*1024),
			(UInt64)(112*1024),
			(UInt64)(128*1024),
			(UInt64)(160*1024),
			(UInt64)(192*1024),
			(UInt64)(224*1024),
			(UInt64)(256*1024),
			(UInt64)(320*1024),
			(UInt64)(384*1024),
			(UInt64)(448*1024),

			(UInt64)(512 + 0*128)*1024,
			(UInt64)(512 + 1*128)*1024,
			(UInt64)(512 + 2*128)*1024,
			(UInt64)(512 + 3*128)*1024,
			(UInt64)(1*1024 +    0*256)*1024,
			(UInt64)(1*1024 +    1*256)*1024,
			(UInt64)(1*1024 +    2*256)*1024,
			(UInt64)(1*1024 +    3*256)*1024,
			(UInt64)(2*1024 +    0*512)*1024,
			(UInt64)(2*1024 +    1*512)*1024,
			(UInt64)(2*1024 +    2*512)*1024,
			(UInt64)(2*1024 +    3*512)*1024,
			(UInt64)(4*1024 + 0*1*1024)*1024,
			(UInt64)(4*1024 + 1*1*1024)*1024,
			(UInt64)(4*1024 + 2*1*1024)*1024,
			(UInt64)(4*1024 + 3*1*1024)*1024,
			(UInt64)(8*1024 + 0*2*1024)*1024,
			(UInt64)(8*1024 + 1*2*1024)*1024,
			(UInt64)(8*1024 + 2*2*1024)*1024,
			(UInt64)(8*1024 + 3*2*1024)*1024,
			(UInt64)( 16*1024 +  0*4*1024)*1024,
			(UInt64)( 16*1024 +  1*4*1024)*1024,
			(UInt64)( 16*1024 +  2*4*1024)*1024,
			(UInt64)( 16*1024 +  3*4*1024)*1024,
			(UInt64)( 32*1024 +  0*8*1024)*1024,
			(UInt64)( 32*1024 +  1*8*1024)*1024,
			(UInt64)( 32*1024 +  2*8*1024)*1024,
			(UInt64)( 32*1024 +  3*8*1024)*1024,
			(UInt64)( 64*1024 + 0*16*1024)*1024,
			(UInt64)( 64*1024 + 1*16*1024)*1024,
			(UInt64)( 64*1024 + 2*16*1024)*1024,
			(UInt64)( 64*1024 + 3*16*1024)*1024,
			(UInt64)(128*1024 + 0*32*1024)*1024,
			(UInt64)(128*1024 + 1*32*1024)*1024,
			(UInt64)(128*1024 + 2*32*1024)*1024,
			(UInt64)(128*1024 + 3*32*1024)*1024,
			(UInt64)(256*1024 + 0*64*1024)*1024,
		};
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
