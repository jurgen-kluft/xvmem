using System;
using System.Collections.Generic;

namespace SuperAlloc
{
    public class Program
    {
        public static int AllocSizeToBin(UInt64 size)
        {
            int w = CountLeadingZeros(size);
            UInt64 f = (UInt64)0x8000000000000000 >> w;
            UInt64 r = (f >> 3) * 0xF;
            UInt64 t = ((f - 1) >> 3);
            size = (size + t) & ~t;
            int i = (int)((size & r) >> (60 - w)) + ((60 - w) * 8);
            return i;
        }

        public class BinMapConfig
        {
            public UInt64 Count { get; set; }
            public UInt64 L1Len { get; set; }
            public UInt64 L2Len { get; set; }
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

        public class SuperBin_t
		{
            public SuperBin_t(UInt64 size)
			{
                Size = (UInt32)size;
                NumPages = 1;
			}
            public UInt32 Size { get; set; }
            public int AllocIndex { get; set; }
            public UInt32 NumPages { get; set; }
            public UInt32 Waste { get; set; }
            public UInt32 AllocCount { get; set; }
            public UInt32 BmL1Len { get; set; }
            public UInt32 BmL2Len { get; set; }
		}
        public class SuperAlloc_t
        {
            public List<SuperBin_t> AllocSizes { get; set; } = new List<SuperBin_t>();
            public UInt64 ChunkSize { get; set; } = 0;
            public UInt64 ChunkCount { get; set; } = 0;
        };

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

        public static void Main()
        {
            try
            {
				List<UInt64> BinToSize = new List<UInt64>();
				List<SuperBin_t> AllocSizes = new List<SuperBin_t>();

				Dictionary<int, List<UInt64>> binSizes = new Dictionary<int, List<UInt64>>();
                UInt64 maxAllocSize = MB(256);
                for (UInt64 b = 8; b <= maxAllocSize; )
				{
                    UInt64 d = b / 8;
                    UInt64 s = b;
                    while (s < (b<<1))
                    {
                        Console.WriteLine("AllocSize: {0}, Bin: {1}", s, AllocSizeToBin(s));
                        int bin = AllocSizeToBin(s);
                        while (BinToSize.Count < bin)
							BinToSize.Add(s);
						AllocSizes.Add(new SuperBin_t(s));
						if (binSizes.ContainsKey(bin) == false)
						{
                            binSizes.Add(bin, new List<UInt64>());
						}
                        binSizes[bin].Add(s);
                        s += d;
                    }
                    b = s;
                }

                // Go over all the power-of-2 chunk sizes and determine which allocation sizes to add
                UInt32 pageSize = (UInt32)KB(64);
                HashSet<UInt32> allocSizesToDo = new HashSet<UInt32>();
				foreach (SuperBin_t allocSize in AllocSizes)
				{
                    allocSizesToDo.Add(allocSize.Size);
				}
				List<SuperAlloc_t> Allocators = new List<SuperAlloc_t>();
				for (UInt64 chunkSize = KB(64); chunkSize <= MB(512); chunkSize *= 2)
                {
                    SuperAlloc_t allocator = new SuperAlloc_t();
                    allocator.ChunkSize = chunkSize;
                    foreach (SuperBin_t allocSize in AllocSizes)
                    {
                        if (!allocSizesToDo.Contains(allocSize.Size))
                            continue;
                        if (allocSize.Size > chunkSize)
                            continue;
                        //if (allocSize > pageSize)
                        //{
                        //   allocator.AllocSizes.Add(allocSize);
                        //  allocSizesToDo.Remove(allocSize);
                        // continue;
                        //}
                        // Figure out if this size can be part of this Chunk Size
                        // Go down in chunksize until page-size to try and fit the allocation size
                        bool addToAllocator = false;
                        UInt32 numPages = 1;
                        UInt32 lowestWaste = pageSize;
                        for (UInt64 cs = chunkSize; cs >= pageSize; cs -= pageSize)
                        {
                            UInt32 chunkWaste = (UInt32)(cs % allocSize.Size);
                            if ((chunkWaste <= (UInt32)(cs / 100)) && chunkWaste < lowestWaste)
                            {
                                numPages = (UInt32)(cs / pageSize);
                                lowestWaste = chunkWaste;
                                addToAllocator = true;
                                break;
                            }
                        }
                        if (addToAllocator)
						{
                            allocSizesToDo.Remove(allocSize.Size);
                            allocSize.AllocCount = (pageSize * numPages) / allocSize.Size;
                            allocSize.NumPages = numPages;
                            allocSize.Waste = lowestWaste;
                            allocSize.AllocIndex = (int)Allocators.Count;
							allocator.AllocSizes.Add(allocSize);
						}
					}
                    Allocators.Add(allocator);
                }

                UInt64 totalChunkCount = 0;
                int allocatorIndex = 0;
                foreach (SuperAlloc_t am in Allocators)
                {
                    foreach (SuperBin_t allocSize in am.AllocSizes)
                    { 
                        UInt64 allocCountPerChunk = am.ChunkSize / allocSize.Size;
                        UInt64 chunkSize = am.ChunkSize;
                        int bin = AllocSizeToBin(allocSize.Size);

                        Console.Write("{0}:", allocatorIndex);
                        Console.Write("{0} AllocSize:{1}, AllocCount:{2}, ChunkSize:{3}, UsedPagesPerChunk:{4}", bin, allocSize.Size.ToByteSize(), allocCountPerChunk, chunkSize.ToByteSize(), allocSize.NumPages);
                        
                        if (allocSize.AllocCount > 1)
                        {
                            BinMapConfig bm = new BinMapConfig();
                            CalcBinMap(bm, allocCountPerChunk, am.ChunkSize);
                            allocSize.BmL1Len = (UInt32)bm.L1Len;
                            allocSize.BmL2Len = (UInt32)bm.L2Len;
                            Console.Write(", BinMap({0},{1}):{2}", bm.L1Len, bm.L2Len, 4 + 2 * (bm.L1Len + bm.L2Len));
                        }
                        Console.WriteLine();
                    }

                    totalChunkCount += (UInt64)am.ChunkCount;
                    allocatorIndex += 1;
                }
                Console.WriteLine();
            }
            catch (Exception e)
            {
                Console.WriteLine("Exception: {0}", e);
            }
            Console.WriteLine("Done...");
        }

        // Targetting 10% allocation waste, this is hard for a specific range of allocation sizes
        // that are close to the page-size of 64 KB. For this we would like to re-route to a region
        // that can deal with 4 KB or 16 KB pages.
        // For example, sizes between 64 KB and 128 KB like 80 KB is automatically wasting 48 KB at
        // the tail. We can reduce this only by going for a page-size of 4 KB / 16 KB.
        // 

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

        public static UInt64 AlignTo(UInt64 v, UInt64 a)
        {
            return (v + (a - 1)) & ~((UInt64)a - 1);
        }
        public static UInt64 AlignTo8(UInt64 v)
        {
            return AlignTo(v, 8);
        }
        public static UInt64 AlignTo16(UInt64 v)
        {
            return AlignTo(v, 16);
        }
        public static UInt64 AlignTo32(UInt64 v)
        {
            return AlignTo(v, 32);
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
        public static string ToByteSize(this UInt32 size)
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
