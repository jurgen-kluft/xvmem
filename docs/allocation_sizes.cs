using System;

public class Program
{
    public static int AllocSizeToBin(UInt64 size)
    {
        UInt64 f = FloorPo2(size);
        int r = CountTrailingZeros(f >> 4) * 4;
        int t = CountTrailingZeros(AlignTo32(f) >> 2);
        int i = (int)((size - (f & ~((UInt64)32 - 1))) >> t);
        i += r;
        return i;
    }

    public static UInt64 AllocSize(UInt64 size)
    {
        size = AlignTo8(size);
        UInt64 f = FloorPo2(size);
        f = AlignTo8(f);
        size = f + ((size - f) + ((f >> 2) - 1) & ~((f >> 2) - 1));
        return size;
    }

    public static void Main()
    {
        UInt64 p = 0;
        for (UInt64 s = 8; s < (256 * 1024); s += 8)
        {
            UInt64 a = AllocSize(s);
            if (a != p)
            {
                Console.WriteLine("Bin: {0}, AllocSize: {1}", AllocSizeToBin(a), a);
                p = a;
            }
        }

        UInt64[] sizes = new UInt64[] {
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
            80*1024,
            96*1024,
            112*1024,
            128*1024,
            160*1024,
            192*1024,
            224*1024,
            256*1024,
        };

        UInt64 chunk = 2 * 1024 * 1024;
        UInt64 page = 64 * 1024;
        int i = 1;
        foreach (UInt64 size in sizes)
        {
            UInt64 c = (chunk / size);
            do
            {
                if (((c * size) & (page - 1)) == 0)
                    break;
                c--;
            } while (true);

            UInt64 pages = (c * size) / page;

            Console.WriteLine("{3}: Size: {0}, Pages: {1}, Count: {2}", size, pages, c, i++);
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
}