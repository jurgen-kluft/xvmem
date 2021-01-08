# xcore virtual memory library (WIP)

A library that provides cross-platform usage of virtual memory.

## Superalloc

Currently this allocator is implemented, 'superalloc', that is ~1200 lines of code for the core.
This allocator is very configurable and all book-keeping data is outside of the managed memory
making it very suitable for different kind of memory (read-only, GPU etc..).

Note: Benchmarks are on the TODO list.
Note: A large test (60 million alloc/free operations) was done without crashing, so this version is the first
      release candidate.
