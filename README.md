# xcore virtual memory library (WIP)

A library that provides cross-platform usage of virtual memory.

The virtual memory allocator based on the size request is delegating the call to:

- very small fixed-size allocator (8B to 1KB, 512 MB memory range)
- small fixed size allocator (1KB to 4KB, 512 MB memory range)
- small/medium size (coalesce) allocator (4KB to 64KB, N (8) regions of 32 MB)
- medium/large size (coalesce) allocator (64KB to 512KB, N (4) regions of 64 MB)
- segregated allocator (512KB to 32MB, 128 GB memory range)
- large size allocator (32MB and above, 128 GB memory range)

Note: The memory ranges are for a system with around 4 to 5 GB of memory where a large part is also for GPU resources.
