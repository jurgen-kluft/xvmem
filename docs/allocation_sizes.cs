using System;

public class Program
{
	public static void Main()
	{
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
		
		UInt64 chunk = 2*1024*1024;
		UInt64 page = 64*1024;
		Console.WriteLine("{0:X}", ~(page-1));
		int i = 1;
		foreach(UInt64 size in sizes)
		{
			UInt64 c = (chunk / size);
			do
			{
				if ( ((c * size) & (page-1)) == 0)
					break;
				c--;
			} while(true);
			
			UInt64 p = (c * size) / page;
			
			Console.WriteLine("{3}: Size: {0}, Pages: {1}, Count: {2}", size, p, c, i++);
		}
	}
}