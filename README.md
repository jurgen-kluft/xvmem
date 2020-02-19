# xcore virtual memory library (WIP)

A library that provides cross-platform usage of virtual memory.

The virtual memory allocator based on the size request is delegating the call to:

- very small fixed-size allocator (8B to 1KB, 512 MB memory range)
- small fixed size allocator (1KB to 8KB, 512 MB memory range)
- medium size (coalesce) allocator (8KB to 640KB, 2 regions, every region 768MB)
- segregated allocator (640KB to 32MB, 128 GB memory range)
- large size allocator (32MB and above, 128 GB memory range)

Note: The memory ranges are for a system with around 4 to 5 GB of memory where a large part is also for GPU resources.
