# Virtual Memory Allocator

These are just thoughts on a virtual memory allocator

1. CPU Fixed-Size-Allocator, 8 <= SIZE <= 4 KB
   16 KB page size
2. Virtual Memory: Alloc, Cache and Free pages back to system

Let's say an APP has 640 GB of address space and it has the following behaviour:

1. Many small allocations (FSA Heap)
2. CPU and GPU allocations (different calls, different page settings, VirtualAlloc/XMemAlloc)
3. Categories of GPU resources have different min/max size, alignment requirements, count and frequency

## Fixed Size Allocator [Ok]

Not too hard to make multi-thread safe using atomics where the only hard multi-threading problem is page commit/decommit.

FSA very small
  RegionSize = 256 x 1024 x 1024
  MinSize = 8
  MaxSize = 1024
  Size Increment = 8
  Number of FSA = 1024 / 8 = 128

FSA small
  RegionSize = 256 x 1024 x 1024
  MinSize = 1024
  MaxSize = 4096
  Size Increment = 128
  Number of FSA = (4096-1024) / 128 = 24

Total number of actual FSA's = 128 + 24 = 152

- Tiny implementation [+]
- Very low wastage [+]
- Can make use of flexible memory [+]
- Fast [+]
- Difficult to detect memory corruption [-]

## Medium Size Allocator - 1 [WIP]

- All other sizes go here (4 KB < Size < 64 KB)
- Segregated; every size has a memory range
- Size is covering 8-bits [0000.0000.0000.0000.xxxx.xxxx.0000.0000]
- A reserved memory range of contiguous virtual pages
- Releases pages back to its underlying page allocator
- 8 GB address range
- Address dexer is covering 25-bits [0000.0000.0000.000x][xxxx.xxxx.xxxx.xxxx.xxxx.xx00.0000.0000]
- Suitable for GPU memory

## Medium Size Allocator - 2 [WIP]

- All other sizes go here (64 KB < Size < 32 MB)
- Segregated; every size has a memory range
- Size-alignment = Page-Size (64 KB)
- Every bin has 8 GB address range
  Address dexer is covering 17-bits [0000.0000.0000.000x][xxxx.xxxx.xxxx.xxxx.0000.0000.0000.0000]
- Suitable for GPU memory

## Medium Size Temporal Allocator [WIP]

- For requests that have a very similar life-time (frame based allocations)
- Contiguous virtual pages
- Moves forward when allocating (this is an optimization)
- 128 GB address space
- Tracked with external bookkeeping
- Configured with a maximum number of allocations
  This allows easier tracking with a circular array struct{ void* address; u32 numpages; }
- Suitable for GPU memory

## Large Size Allocator

- Sizes > 1 MB
- Size alignment is page size
- Reserves huge virtual address space (160GB)
- Each table divided into equal sized slots
- Maps and unmaps pages on demand
- Guarantees contiguous memory

Pros and Cons:

- No headers [+]
- Simple implementation (~200 lines of code) [+]
- No fragmentation [+]
- Size rounded up to page size [-]
- Mapping and unmapping kernel calls relatively slow [-]

## Clear Values (1337 speak)

- memset to byte value
  - Keep it memorable
- 0xFA – Fixed-Size (FSA) Memory Allocated
- 0xFF – Fixed-Size (FSA) Memory Free
- 0xDA – Direct Memory Allocated
- 0xDF – Direct Memory Free
- 0xA1 – Memory ALlocated
- 0xDE – Memory DEallocated
- 0x9A – GPU Memory Allocated
- 0x9F – GPU Memory Free

## Allocation management for `Medium Size Allocator`

### Address and Size Table

- Min/Max-Alloc-Size, Heap 1 =   4 KB / 128 KB
  - Size-Alignment = 64 bytes (2 * cache-line)
  - Find Size is using a simple slot array and nodes
    With a hibitset to quickly find upper bound slots
    124 KB / 64 B = 1984 slots
- Min/Max-Alloc-Size, Heap 2 = 128 KB / 1   MB
  - Size-Alignment = 256 bytes (multiple of cache-line)
  - Find Size is using a simple slot array and nodes
    896 KB / 256 B = 3584 slots
    Also using a hibitset to quickly find free size slot (upper bound)

- Deallocate: Find pointer to get node.
  Slot index = (pointer - base) / 64 KB
  Addr = (u32)((pointer - base) / size-alignment)
  Then traverse the list there to find the node with that addr

  If heap region is 768 MB then we have 768 MB / 64 KB = 12 K slots

```C++

// Could benefit a lot from a virtual fsa that can grow
struct node_t
{
    u32         m_addr;       // (m_addr * size step) + base addr
    u32         m_flags;      // Allocated, Free, Locked
    u32         m_addr_prev;  // previous node in memory, can be free, can be allocated
    u32         m_addr_next;  // next node in memory, can be free, can be allocated
#if defined X_ALLOCATOR_DEBUG
    s32         m_file_line;
    const char* m_file_name;
    const char* m_func_name;
#endif
};

struct ll_size_node_t
{
    u32         m_list_prev;  // either in the allocation slot array as a list node
    u32         m_list_next;  // or in the size slot array as a list node
    u32         m_size;
    u32         m_node_idx;   //
};

btree32 has nodes that are of size 16

```

### Notes 1

Medium Heap Region Size 1 = 8 GB
Medium Heap Region Size 2 = 8 GB

Coalesce Heap Region Size = Medium Heap Region Size 1
Coalesce Heap Min-Size = 4 KB
Coalesce Heap Max-Size = 64 KB
Coalesce Heap Step-Size = 4 KB

Coalesce Heap Region Size = Medium Heap Region Size 2
Coalesce Heap Min-Size = 64 KB,
Coalesce Heap Max-Size = 32 MB
Coalesce Heap Step-Size = 64 KB

### Notes 2

PS4 = 994 GB address space
<http://twvideo01.ubm-us.net/o1/vault/gdc2016/Presentations/MacDougall_Aaron_Building_A_Low.pdf>

### Notes 3

For allocations that are under but close to size like 4K, 8/12/16/20 we could allocate them in a separate allocator. These sizes are very efficient and could benefit from a fast allocator.

### Notes 4

For GPU resources it is best to analyze the resource constraints, for example; On Nintendo Switch shaders should be allocated with an alignment of 256 bytes and the size should also be a multiple of 256.

### Notes 5

A direct size and address table design:

- Heap 1
  MemSize = 8 GB, SizeAlignment = 4 KB, MinSize = 4 KB, MaxSize = 64 KB
  
  Address-Root-Table = 1024 entries
  8 GB / Root-Table-Size = 8 MB
  Every entry is a btree32
  8 MB = 23 bits
  Size Alignment = 8 bits
    btree covers 14 bits (max depth = 7)

  Size-Root-Table = 1024 entries
  Every entry is a linked-list of free spaces

- Heap 2
  MemSize = 8 GB, SizeAlignment = 64K, MinSize = 64 KB, MaxSize = 32 MB
  
  Address-Root-Table = 1024 entries
  8 GB / Root-Table-Size = 8 MB
  Every entry is a btree32

  Size-Root-Table = 1024 entries
  Every entry is a linked-list of free spaces

### Notes 6

Memory Debugging:

- Memory Corruption
- Memory Leaks
- Memory Tracking

Writing allocators to be able to debug allocations. We can do this by writing allocators that
are proxy classes that do some extra work.
