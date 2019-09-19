# xcore virtual memory library (WIP)

A library that provides cross-platform usage of virtual memory.

The virtual memory allocator based on the size request is delegating the call to a:
- very small fixed-size allocator
- small fixed size allocator
- medium size allocator
- large size allocator
