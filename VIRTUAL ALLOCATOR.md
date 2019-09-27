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

## Medium Size Allocator 1 [WIP]

- All other sizes go here (4 KB < Size < 128 KB)
- Size-alignment = 64 bytes
- For Size dexer is covering 12-bits [0000.0000.0000.000x.xxxx.xxxx.xxx0.0000]
- Non-contiguous virtual pages
- Releases empty pages back to the page allocator
- 128 GB address range
  For address dexer is covering 29-bits [0000.0000.000x.xxxx][xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.0000.0000]
  Root array, 9 bits, 20 bits left for tree, 2 bits per node, 10 nodes deep
- Suitable for GPU memory

## Medium Size Allocator 2 [WIP]

- All other sizes go here (128 KB < Size < 1 MB)
- Size-alignment = 256 bytes
- For Size dexer is covering 12-bits [0000.0000.0000.000x.xxxx.xxxx.xxx0.0000]
- Non-contiguous virtual pages
- Releases empty pages back to the page allocator
- 128 GB address range
  For address dexer is covering 29-bits [0000.0000.000x.xxxx][xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.0000.0000]
- Suitable for GPU memory


## Medium Size Temporal/Forward Allocator [WIP]

- For requests that have a very similar life-time
- Non-contiguous virtual pages
- Moves forward when allocating (this is an optimization)
- 128 GB address space
- Space = 32 MB (N pages)
- Space is tracked with simplified external bookkeeping
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

Medium Heap Region Size 1 = 768 MB
Medium Heap Region Size 2 = 768 MB

Coalesce Heap Region Size = Medium Heap Region Size 1
Coalesce Heap Min-Size = 4 KB
Coalesce Heap Max-Size = 128 KB
Coalesce Heap Step-Size = 64 B

Coalesce Heap Region Size = Medium Heap Region Size 2
Coalesce Heap Min-Size = 128 KB,
Coalesce Heap Max-Size = 1 MB
Coalesce Heap Step-Size = 256 B

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
  MemSize = 128 GB, SizeAlignment = 128, MinSize = 4 KB, MaxSize = 128 KB
  
  Address-Root-Table = 1024 entries
  128 GB / Root-Table-Size = 128 MB
  Every entry is a btree32

  Size-Root-Table = 1024 entries
  Every entry is a linked-list of free spaces

- Heap 2
  MemSize = 128 GB, SizeAlignment = 256, MinSize = 128 KB, MaxSize = 1 MB
  
  Address-Root-Table = 1024 entries
  128 GB / Root-Table-Size = 128 MB
  Every entry is a btree32

  Size-Root-Table = 1024 entries
  Every entry is a linked-list of free spaces

  Linked-List-Item:
  u32 m_next;
  u32 m_prev;
  u32 m_item;
