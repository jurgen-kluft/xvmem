# xcore virtual memory library (WIP)

A library that provides cross-platform usage of virtual memory.

The virtual memory allocator based on the size request is delegating the call to:

- very small fixed-size allocator (8B to 1KB, 256 MB memory range)
- small fixed size allocator (1KB to 4KB, 256 MB memory range)
- medium size allocator (4KB to 32MB, 2 regions, every region 768MB)
- large size allocator (32MB and above)

Note: The memory ranges are for a system with around 4 to 5 GB of memory where a large part is also for GPU resources.
