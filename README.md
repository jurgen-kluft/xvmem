# xcore virtual memory library (WIP)

A library that provides cross-platform usage of virtual memory.

The virtual memory allocator based on the size request is delegating the call to:

- fixed size allocator (8B to 4KB, 512 MB memory range)
- medium size (coalesce) allocator (4KB to 64KB, region based commit/decommit)
- medium/large size (coalesce) allocator (64KB to 512KB, region based commit/decommit)
- segregated allocator (512KB to 32MB, 128 GB memory range, direct commit/decommit)
- large size allocator (32MB and above, 128 GB memory range, direct commit/decommit)

Note: The memory ranges are for a system with around 4 to 5 GB of memory where a large part is also for GPU resources.
