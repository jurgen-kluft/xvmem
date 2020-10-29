# xcore virtual memory library (WIP)

A library that provides cross-platform usage of virtual memory.

The virtual memory allocator based on the size request is delegating the call to:

- fixed size allocator (FSA) (8B to 4KB, 512 MB memory range, page based commit/decommit with page caching)
- medium size (coalesce) allocator (4KB to 64KB, 1 GB memory range, region based commit/decommit)
- medium/large size (coalesce) allocator (64KB to 512KB, 1 GB memory range, region based commit/decommit)
- segregated allocator (512KB to 32MB, 128 GB memory range, direct commit/decommit)
- large size allocator (32MB and above, 128 GB memory range, direct commit/decommit)

Note: This allocator is not thread-safe (yet)

Note: On initialization, the allocator itself is doing around 60 allocations and will allocate close to 200KB.

Note: KB = Kilobyte (``1024 bytes``), MB = Megabyte (``1024*1024 bytes``), GB = Gigabyte (``1024*1024*1024 bytes``).

Note: Apart from the FSA allocator, all other allocators have their book-keeping data outside of the managed memory.
      This makes those allocators suitable for any kind of memory.
