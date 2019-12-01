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

### FSA very small

  RegionSize = 256 x 1024 x 1024
  PageSize = 4 KB (if possible)
  MinSize = 8
  MaxSize = 512
  Size Increment = 8
  Number of FSA = 512 / 8 = 64

### FSA small

  RegionSize = 256 x 1024 x 1024
  PageSize = 64 KB
  MinSize = 512
  MaxSize = 8192
  Size Increment = 64
  Number of FSA = (8192-512) / 64 = 120

- Tiny implementation [+]
- Very low wastage [+]
- Can make use of flexible memory [+]
- Fast [+]
- Difficult to detect memory corruption [-]

## Coalesce Allocator [WIP]

- Can use more than one instance
- Size example: 8 KB < Size < 128 KB
- Size alignment: 256
- A reserved memory range of contiguous virtual pages
- Releases pages back to its underlying page allocator
- Best-Fit strategy
- Suitable for GPU memory

## Segregated Allocator -  [WIP]

- Can use more than one instance
- Sizes to go here, example 1 MB < Size < 32 MB
- Size-Alignment = Page-Size (64 KB)
  Alloc-Alignment is Level:Size
- Segregated; every Level = Size + MemoryRange
- Can have multiple identical Levels
- Suitable for GPU memory

```C++
class xsegregated : public xalloc
{
public:
    virtual void* allocate(u32 size, u32 alignment);
    virtual void  deallocate(void* addr);

protected:
    struct xlevel
    {
        u32      m_block_size;   // e.g. 2 MB
        u32      m_mem_range;
        void*    m_mem_base;
        xlevel*  m_prev;
        xlevel*  m_next;
    };

    xvirtual_memory* m_vmem;
    xalloc*          m_internal_heap;
    u32              m_level_cnt;
    xlevel*          m_levels;
};

```

## Temporal Allocator [WIP]

- For requests that have a very similar life-time (frame based allocations)
- Contiguous virtual pages
- Moves forward when allocating (this is an optimization)
- Large address space (~128 GB)
- Tracked with external bookkeeping
- Suitable for GPU memory
- Configured with a maximum number of allocations

```C++
class xtemporal : public xalloc
{
public:
    void initialize(xalloc* internal_heap, u32 max_num_allocs, xvirtual_memory* vmem, u64 mem_range);

    virtual void* allocate(u32 size, u32 alignment);
    virtual void  deallocate(void* addr);

protected:
    struct xentry
    {
        void* m_address;
        u32   m_size;
        u32   m_state;      // Free / Allocated
    };
    xalloc*          m_internal_heap;
    xvirtual_memory* m_vmem;
    void*            m_mem_base;
    u64              m_mem_range;
    u32              m_entry_write;
    u32              m_entry_write;
    u32              m_entry_max;
    xentry*          m_entry_array;
};
```

## Large Size Allocator

- Sizes > 32 MB
- Size alignment is page size
- Small number of allocations
- Allocation tracking done with circular array
- Reserves huge virtual address space (~128 GB)
- Maps and unmaps pages on demand
- Guarantees contiguous memory
- 128 GB / 32 MB = 4096

Pros and Cons:

- No headers [+]
- Simple implementation (~200 lines of code) [+]
- No fragmentation [+]
- Size rounded up to page size [-]
- Mapping and unmapping kernel calls relatively slow [-]

## Clear Values (1337 speak)

- memset to byte value
  - Keep it memorable
  - 0x0A10C8ED
  - 0x0DE1E7ED
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

- Min/Max-Alloc-Size, Heap 1 =   8 KB / 128 KB
  - Size-Alignment = 256 B
  - Find Size is using a simple slot array and nodes
    128 KB / 256 B = 512 slots
    With a small bit-array to quickly find upper-bound size
    - Every size bucket is a BST tree sorted by address

- Min/Max-Alloc-Size, Heap 2 = 128 KB / 1024 KB
  - Size-Alignment = 2048 B
  - Find Size is using a simple slot array and nodes
    1024 KB / 2048 B = 512 slots
    With a small bit-array to quickly find upper-bound size
    - Every size bucket is a BST tree sorted by address

- Deallocate: Find pointer to get node.
  Divisor = Min-Alloc-Size * 16
  Slot index = (pointer - base) / divisor
  Addr = (u32)((pointer - base) / size-alignment)
  Then traverse the list there to find the node with that addr
  - Heap 1:   8 KB * 16 =  128 KB = 1 GB /  128 KB = 8192 slots
  - Heap 2: 128 KB * 16 = 2048 KB = 1 GB / 2048 KB =  512 slots

```C++

// Could benefit a lot from a virtual fsa that can grow
struct naddr_t
{
    u32         m_addr;       // [Allocated, Free, Locked] (m_addr * size step) + base addr
    u32         m_nsize;      // size node
    u32         m_addr_prev;  // previous node in memory
    u32         m_addr_next;  // next node in memory
#if defined X_ALLOCATOR_DEBUG
    s32         m_file_line;
    const char* m_file_name;
    const char* m_func_name;
#endif
};

struct nsize_t
{
    u32         m_list_prev;  // either in the allocation slot array as a list node
    u32         m_list_next;  // or in the size slot array as a list node
    u32         m_size;       // size
    u32         m_addr;       // addr node
};

```

### Notes 1

Medium Heap Region Size 1 = 1 GB
Medium Heap Region Size 2 = 1 GB

Coalesce Heap Region Size = Medium Heap Region Size 1
Coalesce Heap Min-Size = 8 KB
Coalesce Heap Max-Size = 128 KB
Coalesce Heap Step-Size = 256 B

Coalesce Heap Region Size = Medium Heap Region Size 2
Coalesce Heap Min-Size = 128 KB,
Coalesce Heap Max-Size = 1 MB
Coalesce Heap Step-Size = 2 KB

### Notes 2

PS4 = 994 GB address space
<http://twvideo01.ubm-us.net/o1/vault/gdc2016/Presentations/MacDougall_Aaron_Building_A_Low.pdf>

### Notes 3

For allocations that are under but close to sizes like 4K, 8/12/16/20 we could allocate them in a separate allocator. These sizes are very efficient and could benefit from a fast allocator.

### Notes 4

For GPU resources it is best to analyze the resource constraints, for example; On Nintendo Switch shaders should be allocated with an alignment of 256 bytes and the size should also be a multiple of 256.

### Notes 5

A direct size and address table design:

- Heap 1
  MemSize = 1 GB, SizeAlignment = 256 B, MinSize = 8 KB, MaxSize = 128 KB
  Address-Table = 1 GB / (8 KB * 16) = 8192
  Size-Table = (128 KB - 8 KB) / 256 B = 480
  Every entry is a linked-list

- Heap 2
  MemSize = 1 GB, SizeAlignment = 2 KB, MinSize = 128 KB, MaxSize = 1 MB
  Address-Table = 1 GB / (16 * 128 KB) = 512
  Every entry is a linked list of addr nodes
  Size-Table = (1024 KB - 128 KB) / 2 KB = 448
  Every entry is a linked-list of size nodes

### Notes 6

Memory Debugging:

- Memory Corruption
- Memory Leaks
- Memory Tracking

Writing allocators to be able to debug allocations. We can do this by writing allocators that
are proxy classes that do some extra work.
