# xcore virtual memory library (WIP)

A library that provides cross-platform usage of virtual memory.

## Superalloc

Currently a new allocator is implemented, 'superalloc', that is ~1200 lines of code for the core of superalloc.
The new allocator will be configuration(data)-driven and all book-keeping data will be outside of the managed
memory making it very suitable for different kind of memory (read-only, GPU etc..).

Futhermore the performance will be a lot better than the current version.

Note: Benchmarks are on the TODO list.
Note: A large test (60 million alloc/free operations) was done without crashing, so this version is the first
      release candidate.
