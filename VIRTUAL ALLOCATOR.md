# Virtual Memory Allocator

These are just thoughts on a virtual memory allocator

1. CPU Fixed-Size-Allocator (FSA), 8 <= SIZE <= 8 KB
2. Virtual Memory: Alloc, Cache and Free pages back to system

Let's say an APP has 640 GB of address space and it has the following behaviour:

1. Many small allocations (FSA Heap)
2. CPU and GPU allocations (different calls, different page settings, VirtualAlloc/XMemAlloc)
3. Categories of GPU resources have different min/max size, alignment requirements, count and frequency

## Implementations

- Fixed Size Allocator (intrusive) :white_check_mark:
- Coalesce Strategy :white_check_mark:
- Segregated Strategy :white_check_mark:
- Large Strategy :white_check_mark:
- Temporal Strategy :soon:

## Fixed Size Allocator [Ok]

This is an intrusive fixed size allocator targetted at small size allocations.

### Multi Threading

Not too hard to make multi-thread safe using atomics where the only hard multi-threading problem is page commit/decommit.

### FSA very small

- RegionSize = 512 x 1024 x 1024
- PageSize = 4 KB (if possible), otherwise 64 KB
- MinSize = 8
- MaxSize = 512
- Size Increment = 8
- Number of FSA = 512 / 8 = 64

### FSA small

- RegionSize = 256 x 1024 x 1024
- PageSize = 64 KB
- MinSize = 512
- MaxSize = 1024
- Size Increment = 64
- Number of FSA = (8192-512) / 64 = 120

### FSA medium

- RegionSize = 256 x 1024 x 1024
- PageSize = 64 KB
- MinSize = 1024
- MaxSize = 2048
- Size Increment = 128
- Number of FSA = (2048-1024) / 128 = 8

### FSA l1

- RegionSize = 256 x 1024 x 1024
- PageSize = 64 KB
- MinSize = 2048
- MaxSize = 4096
- Size Increment = 256
- Number of FSA = (4096-2048) / 256 = 8

### FSA l2

- RegionSize = 256 x 1024 x 1024
- PageSize = 64 KB
- MinSize = 4096
- MaxSize = 8192
- Size Increment = 512
- Number of FSA = (8192-4096) / 512 = 8

#### Pros / Cons

- Tiny implementation [+]
- Very low wastage [+]
- Can make use of flexible memory [+]
- Fast [+]
- Difficult to detect memory corruption [-]

## Coalesce Allocator A

- Can use more than one instance
- Size range: 8 KB < Size < 128 KB
- Size alignment: 1024
- Size-DB: 120 entries
- A reserved memory range (Max 512MB) of contiguous virtual pages
- Releases pages back to its underlying page allocator
- Best-Fit strategy
- Suitable for GPU memory

## Coalesce Allocator B

- Can use more than one instance
- Size range: 128 KB < Size < 640 KB
- Size alignment: 4096
- Size-DB: 128 entries
- A reserved memory range (Max 512MB) of contiguous virtual pages
- Releases pages back to its underlying page allocator
- Best-Fit strategy
- Suitable for GPU memory

## Segregated Allocator [WIP]

- Segregated:
  - Upon allocation the necessary pages are committed
  - Upon deallocation the pages are decommitted
  - The allocation is Size but will only commit used pages
- A reserved memory range (16GB) of virtual pages
- Can use more than one instance
- Sizes to use are multiple of 64KB (page-size)
  Sizes; 64 KB, 128 KB, 256 KB, 512 KB, 1024 KB
- Size-Alignment = Page-Size (64 KB)
- Suitable for GPU memory

## Temporal Allocator [WIP]

- For requests that have a very similar life-time (1 or more frames based allocations)
- Contiguous virtual pages
- Moves forward when allocating and wraps around (this is an optimization)
- Large address space (~32 GB)
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
    u32              m_entry_read;
    u32              m_entry_max;
    xentry*          m_entry_array;
};
```

## Large Size Allocator

- 32 MB < Size < 512 MB
- Size alignment is page size
- Small number of allocations (<32)
- Allocation tracking is done with blocks
- Reserves huge virtual address space (~128 GB)
- Maps and unmaps pages on demand
- Guarantees contiguous memory
- 128 GB / 512 MB = 256

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

## Allocation management for `Medium Size Allocator` using array's and lists

### Address and Size Table

- Min/Max-Alloc-Size, Heap 1 =   8 KB / 128 KB
  - Size-Alignment = 1024 B
  - Find Size is using a size-array of (128K-8K)/1024 = 120 -> 128

- Min/Max-Alloc-Size, Heap 2 = 128 KB / 1024 KB
  - Size-Alignment = 8192 B
  - Find Size is using a size-array of (1024K-128K)/8K = 112 -> 128
  - NOTE: This heap configuration is better of using the Segregated strategy

- Address-Range = 32MB
  We define average size as 2 \* Min-Alloc-Size = 2 \* 8KB = 16KB
  Targetting 4<->4 list items per block means 32M / ((4+4) \* 16KB) = 256 blocks
  Maximum number of allocations is 32 MB / Min-Alloc-Size = 32K/8 = 4K

- Allocate: Find free node with best size in size-array and remove from size-db
            If size needs to be split add the left-over back to the size-db.
- Deallocate: Find pointer in address-db, check prev-next for coalesce.
  When removing a node we need to figure out if this is the last 'size' entry for
  that block, if it is the last one we need to tag it in the size entry block db.

The size-db for every size entry only needs to store a bitset indicating at which block
we have one or more 'free' nodes of that size. We do have to search it in the list.

Per size we have 256 b = 32 B = 8 W
Size-db is 128 * 32 B = 4 KB

The only downside of using array's and lists is that the size-db is NOT sorted by default.
We can almost solve this for every size entry by also introducing an address-db and hbitset.

For the size entry that stores the larger than Max-Alloc-Size it at least will have an address bias. However there will still be many different sizes.

Problem: If you do not align the size by Min-Alloc-Size then you can get size fragments that are smaller than Min-Alloc-Size.

### Notes 1

Medium Heap Region Size 1 = 1 GB
Medium Heap Region Size 2 = 1 GB

Coalesce Heap Region Size = Medium Heap Region Size 1
Coalesce Heap Min-Size = 8 KB
Coalesce Heap Max-Size = 128 KB
Coalesce Heap Step-Size = 1 KB

Coalesce Heap Region Size = Medium Heap Region Size 2
Coalesce Heap Min-Size = 128 KB
Coalesce Heap Max-Size = 1024 KB
Coalesce Heap Step-Size = 8 KB

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
