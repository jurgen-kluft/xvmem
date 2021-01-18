---
marp: true
theme: gaia
_class: lead
paginate: true
backgroundColor: #fff
backgroundImage: url('superalloc-background.jpg')
---

![bg left:40% 80%](superalloc.svg)

# **SuperAlloc**

Virtual Memory Allocator for 64-bit Games or Applications

---

# Background

Current well known memory allocators like DLMalloc are mainly based on handling small size requests using a simple FSA approach and for medium/large requests to use a coalesce best-fit approach.

---

# Problems

* Wasted a lot of memory
* Suffered from fragmentation
* Not able to use it for GPU memory, so you need another implementation to handle GPU memory

---

# Memory Fragmentation

* Heap fragmented in small non-contigues blocks
* Allocations can fail despite enough memory
* Caused by mixed allocation lifetimes

```md
               - Allocation 128 KB                                                                   
              /                                                                                     
        +---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---+       
  0 B   | F | X | F | X | X | X | X | X | X | X | X | F | X | X | X | X | X | F | X | X | X |   1 MB
        +---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---+       
                  \                                                                                 
                   -Free                                                                           
```

---

# Design Goals

* Low fragmentation
* High utilisation
* Configurable
* Support many platforms (PC, Mac, Linux, Playstation, Xbox, Nintendo)
* Support GPU and texture streaming
* Debugging support

---

# Virtual Memory

* Process uses virtual addresses
* Virtual addresses mapped to physical addresses
* CPU looks up physical address
* Requires OS and hardware support

---

# Benefits of Virtual Memory

* Reduced memory fragmentation
  * Fragmentation is address fragmentation
  * We use virtual addresses
  * Virtual address space is larger than physical
  * Contiguous virtual memory not contiguous in physical memory
* Able to release back unused 'memory'

---

# Virtual Address Space

```md

 Virtual Address Space

 +-------------------------------------------------------------------------------------------------+       
 |                                                                                                 | 944 GB
 +-------------------------------------------------------------------------------------------------+       

 Physical Memory                                                                                           

 +-+
 | | 8 GB
 +-+
```

---

# Memory Pages

* Mapped in pages
* x64 supports:
  * 4 KB and 2 MB pages
* PlayStation 4 OS uses:
  * 16 KB (4x4 KB) and 2 MB
* GPU has more sizes

---

# Page Sizes

* 2 MB pages are the fastest
* 16 KB pages wastes less memory
* We use 64 KB (4 x 16 KB pages on PlayStation 4)
  * Smallest optimal size for PlayStation 4 GPU
* Also able to use 4 or 16 KB for special cases

---

# PlayStation Onion Bus & Garlic Bus

* CPU & GPU can access both
  * But at different bandwidths
* Onion = fast CPU access
* Garlic = fast GPU access

---

# SuperAllocator

* Splits up large virtual address space into `blocks`
* Every `block` is dedicated to one specific `chunk` size
* All bookkeeping data is outside of the managed memory
* A superalloc allocator manages a range of allocation sizes
* Only uses 2 data structures:
  * Linked Lists
  * BinMap (3 level hierarchical bitmap)
* Number of code lines = 1200, excluding the 2 data structures

---

# Allocator

```c++
class alloc_t
{
public:
    virtual void* allocate(u32 size, u32 align) = 0;
    virtual u32   deallocate(void* pMemory) = 0;
};
```

---

# SuperAllocator Virtual Address Space

Address space is divided into blocks of 1 GB.
(X = Used, F = Free)

```md
                -> Block 1 GB                                                                           
               /                                                                                      
              /                                                                                       
        +---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---+       
        |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |       
 0 GB   | X | X | F | F | F | F | F | F | F | F | F | F | F | F | F | F | F | F | 128 GB
        |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |       
        +---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---+       
                                                                                                      
```

---

# Block

A block of 1 GB is divided into chunks of a fixed size.

```md
         Chunk (64 KB)
      __/______________________________
0 GB | X | F | F |                     | 1 GB
      ---------------------------------
```

Chunk size is a power-of-2 multiple of 64 KB, some examples of chunk sizes are 64 KB, 256 KB, 512 KB, 1 MB, 2 MB, 4 MB, 8 MB ... 256 MB.

---

# Chunk

The smallest chunk-size of 64 KB, handled by the first super alloc, is good for allocation sizes of 8 B to 256 B. The next super alloc has a chunk-size of 512 KB and handles allocation sizes between 256 B and 64 KB. Chunks are managed in two distinct ways, when the allocation size does not need to track the actual number of pages used we can use a binmap. Otherwise a chunk is used for a single allocation and the actual number of physical pages is stored.

---

# Binmap

The implementation relies mainly on instruction `count trailing zeros`, which is a single instruction on most CPU's nowadays. You have a `find` function which can give you a 'free' element as well as `set` and `clear` functions.

```md
          32 bit, level 0                                                                                                     
            +--------------------------------+                                                                                
            |                               0|                                                                                
            +--------------------------------+                                                                                
                                             \--------------|                                                                 
      16-bit words, level 1                                 |                                                                 
          +----------------+   +----------------+   +-------|--------+                                                        
          |                |   |                |   |              01|                                                        
          +----------------+   +----------------+   +--------------||+ First bit '1' means that                               
                                                                   /\  16-bit word on level 2 is full                         
 16-bit words, level 2                              |--------------  |                                                        
 +----------------+   +----------------+   +--------|-------+    +---|------------+                                           
 |                |   |                |   |0001000010000010|    |1111111111111111|                                           
 +----------------+   +----------------+   +----------------+    +----------------+                                           
```

---

# SuperAlloc

In total there are 13 super allocators that make up the main allocator. In the implementation you can find a configuration example for a desktop application.
