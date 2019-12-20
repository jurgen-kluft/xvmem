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
  Alloc-Alignment is Level:Size but will only commit used pages
- Segregated; every Level takes one or more ranges
- Levels can consume additional ranges but also release them back
- Suitable for GPU memory

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
- Allocation tracking is done with levels + BST
  - Could easily track if a level is empty and free it back to main
- Reserves huge virtual address space (~128 GB)
- Maps and unmaps pages on demand
- Guarantees contiguous memory
- 128 GB / 32 MB = 4096

Pros and Cons:

- No headers [+]
- Relatively simple implementation (~200 lines of code) [+]
- Will have no fragmentation [+]
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
  - Find Size is using a size-BST

- Min/Max-Alloc-Size, Heap 2 = 128 KB / 1024 KB
  - Size-Alignment = 2048 B
  - Find Size is using a size-BST

- Allocate: Find free node with best size in size-BST, insert allocated address in address-BST
- Deallocate: Find pointer in address-BST

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

For allocations that are under but close to sizes like 4K, 8/12/16/20 we could allocate them in a separate allocator.
These sizes are very efficient and could benefit from a fast allocator.

### Notes 4

For GPU resources it is best to analyze the resource constraints, for example; On Nintendo Switch shaders should be allocated with an alignment of 256 bytes and the size should also be a multiple of 256.

### Notes 5

A direct size and address table design:

- Heap 1
  MemSize = 1 GB, SizeAlignment = 256 B, MinSize = 8 KB, MaxSize = 128 KB
  Address-BST
  Size-BST

- Heap 2
  MemSize = 1 GB, SizeAlignment = 2 KB, MinSize = 128 KB, MaxSize = 1 MB
  Address-BST
  Size-BST

### Notes 6

Memory Debugging:

- Memory Corruption
- Memory Leaks
- Memory Tracking

Writing allocators to be able to debug allocations. We can do this by writing allocators that
are proxy classes that do some extra work.
