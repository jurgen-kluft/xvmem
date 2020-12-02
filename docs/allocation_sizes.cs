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

        public class BinMapConfig
        {
            public UInt64 Count { get; set; }
            public UInt64 L1Len { get; set; }
            public UInt64 L2Len { get; set; }
        }

        public static UInt64 CalcAddressRange(UInt64 size)
        {
            UInt64 addressrange;
            if (size <= 256) addressrange = 256 * 1024 * 1024;
            else if (size <= 2048) addressrange = 256 * 1024 * 1024;
            else if (size <= 32768) addressrange = 1 * 1024 * 1024 * 1024;
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
            else if (size <= 32768) chunksize = 4 * 1024 * 1024;
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

        public static void CalcBinMap(BinMapConfig bm, UInt64 allocCount, UInt64 chunksize)
        {
            UInt64 l2_len = ((allocCount / (UInt64)16) + (UInt64)3) & ~((UInt64)3);
            if ((allocCount / 16) > 16)
            {
                l2_len = (((l2_len / (UInt64)4) * (UInt64)4) + (UInt64)3) & ~((UInt64)3);
            }
            else
            {
                l2_len = ((allocCount / 16) + (UInt64)1) & ~((UInt64)1);
            }
            UInt64 l1_len = (l2_len + 15) / (UInt64)16;
            if (l1_len < 2) l1_len = 2;

            bm.Count = (uint)(allocCount);
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

            bm.L1Len = CeilPo2(bm.L1Len);
            bm.L2Len = CeilPo2(bm.L2Len);
        }

        class AllocSizeBin
		{
            public UInt64 Size { get; set; }
		}

        static List<UInt64> BinToSize = new List<UInt64>();
        static List<AllocSizeBin> Bins = new List<AllocSizeBin>();

        public static void Main()
        {
            try
            {
                UInt64 maxAllocSize = MB(256);
                for (UInt64 b = 8; b <= maxAllocSize; )
				{
                    UInt64 d = b / 4;
                    UInt64 s = b;
                    while (s < (b<<1))
                    {
                        //Console.WriteLine("AllocSize: {0}, Bin: {1}", s, AllocSizeToBin(s));
                        BinToSize.Add(s);
                        Bins.Add(new AllocSizeBin() { Size = s });
                        s += AlignTo8(d);
                    }
                    b = s;
                }

                UInt64 page = 64 * 1024;
                {
                    UInt64 size = 8;
                    foreach (Allocator am in Allocators)
                    {
                        am.MinAllocSize = size;
                        int bin = AllocSizeToBin(am.MaxAllocSize);
                        size = BinToSize[bin + 1];
                    }
                }
                foreach (Allocator am in Allocators)
                {
                    am.ChunkManager = new ChunkManager();
                    for (UInt64 size = am.MinAllocSize; size <= am.MaxAllocSize;)
                    {
                        ChunkManager cm = am.ChunkManager;
                        cm.PagesPerChunk = (int)(am.ChunkSize / page);

                        int bin = AllocSizeToBin(size);
                        UInt64 inc = FloorPo2(size) / 4;
                        while (AllocSizeToBin(size) == bin)
                            size += inc;
                    }
                }

                UInt64 totalChunkCount = 0;
                int allocatorIndex = 0;
                foreach (Allocator am in Allocators)
                {
                    for (UInt64 size = am.MinAllocSize; size <= am.MaxAllocSize;)
                    {
                        ChunkManager cm = am.ChunkManager;
                        cm.OneAllocPerChunk = (am.ChunkSize / am.MaxAllocSize) <= 1;

                        UInt64 chunkSize = am.ChunkSize;
                        UInt64 chunkCount = am.AddressRange / am.ChunkSize;
                        UInt64 allocCountPerChunk = (chunkSize / size);
                        UInt64 pages;
                        if (chunkSize > 65536)
                        {
                            do
                            {
                                if (((allocCountPerChunk * size) & (page - 1)) == 0)
                                    break;
                                allocCountPerChunk--;
                            } while (true);
                            pages = (allocCountPerChunk * size) / page;
                        }
                        else
                        {
                            pages = 1;
                        }
                        am.ChunkCount = chunkCount;

                        BinMapConfig bm = new BinMapConfig();
                        if (!cm.OneAllocPerChunk)
                        {
                            CalcBinMap(bm, allocCountPerChunk, am.ChunkSize);
                        }


                        int bin = AllocSizeToBin(size);

                        Console.Write("{0}:", allocatorIndex);
                        Console.Write("{0} AllocSize:{1}, AllocCount:{2}, ChunkSize:{3}, AddressRange:{4}, ChunkCount:{5}, UsedPagesPerChunk:{6}", bin, size.ToByteSize(), allocCountPerChunk, chunkSize.ToByteSize(), am.AddressRange.ToByteSize(), chunkCount, pages);
                        Console.Write(", BinMap(4,{0},{1}):{2}", bm.L1Len, bm.L2Len, 4 + 2 * (bm.L1Len + bm.L2Len));
                        Console.Write(", Chunk: Pages:{0}, Min:{1}, Max:{2}, OneAlloc:{3}", cm.PagesPerChunk, am.MinAllocSize.ToByteSize(), am.MaxAllocSize.ToByteSize(), cm.OneAllocPerChunk ? "Yes" : "No");
                        Console.WriteLine();

                        UInt64 inc = FloorPo2(size) / 4;
                        while (AllocSizeToBin(size) == bin)
                            size += inc;
                    }

                    totalChunkCount += (UInt64)am.ChunkCount;
                    allocatorIndex += 1;
                }
                Console.WriteLine();
                Console.WriteLine("Memory Stats:");
                Console.Write("    Total Chunk Data = (4+4+4)x{0} = {1}", totalChunkCount, ((4+4+4)* totalChunkCount).ToByteSize());
            }
            catch (Exception e)
            {
                Console.WriteLine("Exception: {0}", e);
            }
            Console.WriteLine("Done...");
        }

        public class ChunkManager
        {
            public bool OneAllocPerChunk { get; set; } = false;
            public int PagesPerChunk { get; set; }
        }

        public class Allocator
        {
            public UInt64 MinAllocSize { get; set; } = 0;
            public UInt64 MaxAllocSize { get; set; } = 0;
            public UInt64 AddressRange { get; set; } = 0;
            public UInt64 ChunkSize { get; set; } = 0;
            public UInt64 ChunkCount { get; set; } = 0;
            public ChunkManager ChunkManager { get; set; }
            public UInt64 MemoryFootPrint { get; set; } = 0;
        };

        // CPU Memory Configuration for ~5 GB of device memory (PS4, Xbox One, Nintendo Switch)
        static Allocator[] Allocators = new Allocator[] {
            new Allocator { MinAllocSize=8, MaxAllocSize=     256, AddressRange= MB(128), ChunkSize=  KB(64) },
			new Allocator { MinAllocSize=0, MaxAllocSize= KB(512), AddressRange= MB(512), ChunkSize= KB(512) },
			new Allocator { MinAllocSize=0, MaxAllocSize=   MB(1), AddressRange=   GB(1), ChunkSize=   MB(1) },
			new Allocator { MinAllocSize=0, MaxAllocSize=   MB(2), AddressRange=   GB(1), ChunkSize=   MB(2) },
			new Allocator { MinAllocSize=0, MaxAllocSize=   MB(4), AddressRange=   GB(1), ChunkSize=   MB(4) },
			new Allocator { MinAllocSize=0, MaxAllocSize=   MB(8), AddressRange=   GB(1), ChunkSize=   MB(8) },
			new Allocator { MinAllocSize=0, MaxAllocSize=  MB(16), AddressRange=   GB(1), ChunkSize=  MB(16) },
			new Allocator { MinAllocSize=0, MaxAllocSize=  MB(32), AddressRange=   GB(1), ChunkSize=  MB(32) },
			new Allocator { MinAllocSize=0, MaxAllocSize=  MB(64), AddressRange=   GB(1), ChunkSize=  MB(64) },
			new Allocator { MinAllocSize=0, MaxAllocSize= MB(128), AddressRange=   GB(2), ChunkSize= MB(128) },
			new Allocator { MinAllocSize=0, MaxAllocSize= MB(256), AddressRange=   GB(4), ChunkSize= MB(256) },
		};




        ///  ----------------------------------------------------------------------------------------------------------
        ///  ----------------------------------------------------------------------------------------------------------
        ///  ----------------------------------------------------------------------------------------------------------
        ///                                               UTILITY FUNCTIONS
        ///  ----------------------------------------------------------------------------------------------------------
        ///  ----------------------------------------------------------------------------------------------------------
        ///  ----------------------------------------------------------------------------------------------------------


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
                integer <<= 32;
            }
            if ((integer & 0xFFFF000000000000UL) == 0)
            {
                count += 16;
                integer <<= 16;
            }
            if ((integer & 0xFF00000000000000UL) == 0)
            {
                count += 8;
                integer <<= 8;
            }
            if ((integer & 0xF000000000000000UL) == 0)
            {
                count += 4;
                integer <<= 4;
            }
            if ((integer & 0xC000000000000000UL) == 0)
            {
                count += 2;
                integer <<= 2;
            }
            if ((integer & 0x8000000000000000UL) == 0)
            {
                count += 1;
                integer <<= 1;
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
                integer >>= 32;
            }
            if ((integer & 0x0000FFFF) == 0)
            {
                count += 16;
                integer >>= 16;
            }
            if ((integer & 0x000000FF) == 0)
            {
                count += 8;
                integer >>= 8;
            }
            if ((integer & 0x0000000F) == 0)
            {
                count += 4;
                integer >>= 4;
            }
            if ((integer & 0x00000003) == 0)
            {
                count += 2;
                integer >>= 2;
            }
            if ((integer & 0x00000001) == 0)
            {
                count += 1;
                integer >>= 1;
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
            private readonly ulong _value;

            private const int DEFAULT_PRECISION = 2;

            private readonly static IList<string> Units = new List<string>() { " B", " KB", " MB", " GB", " TB" };

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
                string str = value.ToString(pow == 0 ? "F0" : "F" + precision.ToString());
                if (str.EndsWith(".00"))
                    str = str.Substring(0, str.Length - 3);
                return str + Units[(int)pow];

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
                if (size == Decimal.Floor(size))
                    precision = "0";
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
