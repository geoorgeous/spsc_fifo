# Lock-free SPSC FIFO

A lock-free SPSC FIFO data structure written in modern C++(20) for low-latency, high-performant, mulithreaded use cases.

<b>The code is heavily annoted for educational purposes.</b>

---

This content is based off of the work done by Charles Frasch. Charles' CppCon 2023 presentation on this subject was a fantastic resource in understanding the theory of a lock-free SPSC FIFO. The `SpscFifo*` classes I've written are based off of his original implementations. For reference please see:

- [Charles Frasch - SPSC FIFO code repository](https://github.com/CharlesFrasch/cppcon2023)
- [SPSC lock-free FIFO From the Ground Up - Charles Frasch - CppCon 2023 (YouTube)](https://www.youtube.com/watch?v=K3P_Lmq6pw0)

### What is SPSC FIFO?

SPSC (Single Producer, Single Consumer) refers to the idea that a system has only two parties: a single producer that is producing and writing some abitrary content in to the system, and a single consumer that's consuming and reading that content from the system.

An SPSC FIFO... to-do

Finally, a lock-free SPSC FIFO... to-do

### Optimizations Overview

Below is a breakdown of the sequential improvements implemented on top of each iteration.

#### fifo.hpp

A simple FIFO queue.
*NOT* suitable for SPSC multithreaded use as there's a likely possibility
of data-races when `pop()`ing or `push()`ing. This class is used as an example
implementation of a FIFO data structure to build our SPSC FIFOs on top of.

#### spsc_fifo_0.hpp

A thread-safe Single-Consumer, Single-Producer circular FIFO queue.

Exact same as [fifo.hpp](./fifo.hpp) except for one thing: we use an atomic
(`std::atomic` (C++11)) type for our variables that keep track of our push and
pop positions. This ensures well-defined behaviour when our threads read/write these variables.
Now one thread can call `push()` (the Producer thread), and another thread can call `pop()`
(the Consumer thread) without UB.<br>
See: [std::atomic (C++ reference)](https://en.cppreference.com/w/cpp/atomic/atomic)

#### spsc_fifo_1.hpp

A thread-safe Single-Consumer, Single-Producer circular FIFO queue with optimized inter-thread synchronization, and false-sharing-avoidance.

The first thing we've done is we've specified the operation ordering policy
of the `atomic::load()` and `atomic::store()` operations for our queue position-
keeping variables. Before this, those operation were using the default [Sequentially-consistent](https://en.cppreference.com/w/cpp/atomic/memory_order#Sequentially-consistent_ordering) ordering policy. From [cppreference.com](https://cppreference.com):

>The default behavior of all atomic operations in the library provides
for sequentially consistent ordering (see discussion below). That
default can hurt performance, but the library's atomic operations can
be given an additional `std::memory_order` argument to specify the exact
constraints, beyond atomicity, that the compiler and processor must
enforce for that operation."

For variables that we know aren't written to on other threads, we can use
the [Relaxed](https://en.cppreference.com/w/cpp/atomic/memory_order#Relaxed_ordering) ordering policy. This removes unneccessary and costly synchronization constraints that were in place before. For
example, we never have to worry about synchronization semantics on the
Producer thread when we're reading the `push_pos_` variable because the
Producer thread is the only thread that ever writes to that variable.

When a thread reads a variable that the other thread writes to, or if a
thread writes to a variable that the other thread reads from, we do indeed
care about synchronizating the order of operations. For this we specify the
[Release-Acquire](https://en.cppreference.com/w/cpp/atomic/memory_order#Release-Acquire_ordering) ordering policy. This ensures that any writes to a variable made from one thread are visible to the other thread.<br>

See: [std::memory_order (C++ reference)](https://en.cppreference.com/w/cpp/atomic/memory_order)

We've reduced direct sharing of atomic variables. We can also tackle false-
sharing. In short, false-sharing occurs when one thread forces another
to reload a block of memory because a variabled has been altered, even
though 

See: [False Sharing (Wikipedia)](https://en.wikipedia.org/wiki/False_sharing)<br>
See also: [Video: False Sharing in C++ (YouTube)](https://www.youtube.com/watch?v=O0HCGOzFLm0)

#### spsc_fifo_2.hpp

to-do

#### spsc_fifo_3.hpp

to-do

### Benchmarks

Built and run on Windows 11 | Windows Subsystem for Linux 2, g++12 (`-std=c++20, -O3`)

Queue item type = `std::int64_t`

| Class | Operations/sec |
| ------------- | ------------- |
| SpscFifo0 | to-do |
| SpscFifo1 | to-do |
| SpscFifo2 | to-do |
| SpscFifo3 | to-do |
