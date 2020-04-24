# Virtual Memory Allocator

These are just thoughts on a virtual memory allocator

1. CPU Fixed-Size-Allocator (FSA), 8 <= SIZE <= 4 KB
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

### FSA

- RegionSize = 512 x 1024 x 1024
- PageSize = 4 KB (if possible), otherwise 64 KB
- Min/Max/Step = 8/64/8, 64/512/16, 512/1024/64, 1024/2048/128, 2048/4096/256

#### Pros / Cons

- Tiny implementation [+]
- Very low wastage [+]
- Can make use of flexible memory [+]
- Fast [+]
- Can cache a certain amount of free pages [+]
- Difficult to detect memory corruption [-]

## Coalesce Allocator Direct 1 (512 MB)

- Can use more than one instance
- Size range: 4 KB < Size < 64 KB
  - Size alignment: 256
  - Size-DB: 256 entries
- A memory range of 512 MB
  - Addr-DB: 4096 entries
  - 512 MB / 4096 = 128 KB per address node
- Best-Fit strategy
- Suitable for GPU memory

## Coalesce Allocator Direct 2 (512 MB)

- Can use more than one instance
- Size range: 64 KB < Size < 512 KB
- Size alignment: 4096
- Size-DB: 128 entries
- A memory range of 512 MB
  - Addr-DB: 512 entries
  - 512 MB / 512 = 1 MB per address node
- Best-Fit strategy
- Suitable for GPU memory

## Coalesce Allocator, Release/Commit Pages

Releasing pages back to free memory

## Segregated Allocator [WIP]

- Segregated:
  - Upon allocation the necessary pages are committed
  - Upon deallocation the pages are decommitted
  - The allocation is Size but will only commit used pages
- A reserved memory range (16GB) of virtual pages
- Can use more than one instance
- Sizes to use are multiple of 64KB (page-size)
  Sizes; 512 KB, ..., 16 MB, 32 MB (Also can handle 64 KB, 128 KB and 256 KB)
- Size-Alignment = Page-Size (64 KB)
- Suitable for GPU memory

## Large Size Allocator

- 32 MB < Size < 512 MB
- Size alignment is page size
- Small number of allocations (<32)
- Allocation tracking is done with blocks (kinda like FSA)
- Reserves huge virtual address space (~128 GB)
- Maps and unmaps pages on demand
- Guarantees contiguous memory
- 128 GB / 512 MB = 256 maximum number of allocations

Pros and Cons:

- No headers [+]
- Relatively simple implementation (~200 lines of code) [+]
- Will have no fragmentation [+]
- Size rounded up to page size [-]
- Mapping and unmapping kernel calls relatively slow [-]

## Proxy allocators for 'commit/decommit' of virtual memory

1. Direct 
   Upon allocation virtual memory is committed
   Upon deallocation virtual memory is decommitted
2. Regions 
   Upon allocation, newly intersecting regions are committed
   Upon deallocation, intersecting regions that become non-intersected are decommitted
3. Regions with caching 
   A region is not directly committed or decommitted but it is first added to a list
   When the list reaches its maximum the oldest ones are decommitted

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

## Clear Values (1337 speak)

- memset to byte value
  - Keep it memorable
- 0xF5A10C8E – Fixed-Size (FSA) Memory Allocated
- 0xF5ADE1E7 – Fixed-Size (FSA) Memory Free
- 0xD3A10C8E – Direct Memory Allocated
- 0xD3ADE1E7 – Direct Memory Free
- 0x3A10C8ED – Memory ALlocated
- 0x3DE1E7ED – Memory DEallocated
- 0x96A10C8E – GPU(96U) Memory Allocated
- 0x96DE1E7E – GPU(96U) Memory Free

## Allocation management for `Coalesce Allocator` using array's and lists

### Address and Size Tables

- Min/Max-Alloc-Size, Heap 1 =   8 KB / 128 KB
  - Size-Alignment = 1024 B
  - Find Size is using a size-array of (128K-8K)/1024 = 120 -> 128 entries

- Min/Max-Alloc-Size, Heap 2 = 128 KB / 1024 KB
  - Size-Alignment = 8192 B
  - Find Size is using a size-array of (1024K-128K)/8K = 112 -> 128
  - 1 MB per address node, 256 nodes = covering 256 MB
  - NOTE: This heap configuration may be better of using the Segregated strategy

- Address-Range = 32MB
  We define average size as 2 \* Min-Alloc-Size = 2 \* 8KB = 16KB
  Targetting 8 nodes per block means 32M / (8 \* 16KB) = 256 blocks
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
We can solve this for every size entry by also introducing an address-db and bitset.

For the size entry that stores the larger than Max-Alloc-Size it at least will have an address bias. However there will still be many different sizes.

Problem: If you do not align the size by Min-Alloc-Size then you can get size fragments that are smaller than Min-Alloc-Size.

### Notes 1

Small/Medium Heap Region Size = 256 MB
Medium/Large Heap Region Size = 256 MB

Coalesce Heap Region Size = Small/Medium Heap Region Size
Coalesce Heap Min-Size = 4 KB
Coalesce Heap Max-Size = 64 KB
Coalesce Heap Step-Size = 256 B

Coalesce Heap Region Size = Medium/Large Heap Region Size
Coalesce Heap Min-Size = 64 KB
Coalesce Heap Max-Size = 512 KB
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

Memory Debugging:

- Memory Corruption
- Memory Leaks
- Memory Tracking

Writing allocators to be able to debug allocations. We can do this by writing allocators that
are proxy classes that do some extra work.
