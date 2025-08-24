# Lock-free SPSC FIFO

A lock-free SPSC FIFO data structure written in modern C++(20) for low-latency, high-performant, mulithreaded use cases.

---

This content is based off of the work done by Charles Frasch. Charles' CppCon 2023 presentation on this subject was a fantastic resource in understanding the theory of a lock-free SPSC FIFO. The `SpscFifo*` classes I've written are based off of his original implementations. <b>Please note that my code is heavily annoted for educational purposes.</b> For reference please see:

- [Charles Frasch - SPSC FIFO code repository](https://github.com/CharlesFrasch/cppcon2023)
- [SPSC lock-free FIFO From the Ground Up - Charles Frasch - CppCon 2023 (YouTube)](https://www.youtube.com/watch?v=K3P_Lmq6pw0)

---

### What is SPSC FIFO?

SPSC (Single Producer, Single Consumer) refers to a system that has only two active parties: a single producer that is producing and writing some abitrary content in to the system, and a single consumer that's consuming and reading that content from the system. At first, this seems like an arbitrary constraint to put on any given system, but in practice these constraints allow us to design efficient data exchange interfaces.

An SPSC FIFO is a First-In-First-Out data structure - such as a queue - built with SPSC in mind. This means that for a queue, only one party - the Producer - will ever `push()` items in, and only one party - the Consumer - will `pop()` items out. A Producer/Consumer in this context could be a system module, process, or application thread.

Finally, a lock-free SPSC FIFO refers to an SPSC FIFO data structure that can be used in a multithreaded context without locking. In reality it's not realistic to prevent all locks - some level of synchronization between threads is needed to avoid undefined behaviour. But with the use of modern C++ and a good understanding of concurrent computing, we can reduce locking and achieve drastically better performance than we might get with simple mutex usage.

### Optimizations Overview

Below is a breakdown of the sequential improvements implemented on top of each iteration.

#### [Fifo](./fifo.hpp)

A simple FIFO circular queue.
*NOT* suitable for SPSC multithreaded use as there's a likely possibility
of data-races when `pop()`ing or `push()`ing. This class is used as an example
implementation of a FIFO data structure to build our SPSC FIFOs on top of.

#### [SpscFifo0](./spsc_fifo_0.hpp)

A thread-safe Single-Consumer, Single-Producer circular FIFO queue.

Exact same as [Fifo](./fifo.hpp) except for one thing: we use an atomic
(`std::atomic` (C++11)) type for our variables that keep track of our push and
pop positions. This ensures well-defined behaviour when our threads read/write these variables.
Now one thread can call `push()` (the Producer thread), and another thread can call `pop()`
(the Consumer thread) without UB.<br>

<i>See: [std::atomic (C++ reference)](https://en.cppreference.com/w/cpp/atomic/atomic)</i>

#### [SpscFifo1](./spsc_fifo_1.hpp)

A thread-safe Single-Consumer, Single-Producer circular FIFO queue with optimized inter-thread synchronization, and false-sharing-avoidance.

The first thing we've done is specified the atomic operation ordering policy
of read/writes via the `atomic::load()` and `atomic::store()` functions for our position-keeping variables.
Before this, read/writes were using the default [Sequentially-consistent](https://en.cppreference.com/w/cpp/atomic/memory_order#Sequentially-consistent_ordering) ordering policy. From [cppreference.com](https://cppreference.com):

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

<i>See: [std::memory_order (C++ reference)](https://en.cppreference.com/w/cpp/atomic/memory_order)</i>

The second improvement present in `SpscFifo1` is a reduction in false-sharing.
In short, false-sharing occurs when a thread <i>t1</i> alters a variable <i>v1</i> which shares a cache
line with another variable <i>v2</i> that's being accessed by another thread <i>t2</i>. Even though there
was no alteration to <i>v2</i>, <i>t2</i> is still forced to reload the cache line. Thus, it would appear
to the programmer that one thread has inadvertently interrupted the other.

Though the consequences of this surprising side-effect are pretty dire in terms of performance, the
solution is luckily quite straight forward. We simply specify an alignment for our variables of
atleast the size of our cache line. This way only a single one of our variables can fit inside
a cache line, preventing other variables from being inadvertently invalidated.

<i>See: [False Sharing (Wikipedia)](https://en.wikipedia.org/wiki/False_sharing)</i><br>
<i>See also: [Video: False Sharing in C++ (YouTube)](https://www.youtube.com/watch?v=O0HCGOzFLm0)</i><br>
<i>See also: [alignas specifier (C++ reference)](https://en.cppreference.com/w/cpp/language/alignas)</i>

#### [SpscFifo2](./spsc_fifo_2.hpp)

A thread-safe Single-Consumer, Single-Producer circular FIFO queue with optimized inter-thread synchronization, false-sharing-avoidance, and cached position variables.

Another improvement we can make to our implementation - on top of the previous improvements - is to cache our position-keeping variables. Each thread now has a copy of the variable which it would usually have to acquire
from the other other thread. This copy is only ever accessed by the thread in question, and so it doesn't need
to be atomic. For example, there's now a copy of the `pop_pos_` variable - `pop_pos_cached_` - which the Producer
thread will use in `push()`es.

Now, instead of `load()`ing the `pop_pos_` variable every time the Producer makes a `push()`, the Producer uses
the cached variable to check if the queue is full. If it is, only then does the Producer thread acquire the 'real'
variable and re-cache it. It can then check again if the queue is definitely full. Similarly, the Consumer thread does the same thing with `push()` with the `push_pos_` variable.

As before, we also must make sure that these two new variables exists on their own cache line, and so they are aligned in the same way as the original shared variables.

### Benchmarks

Built and run on Windows 11 | Windows Subsystem for Linux 2, g++12 (`-std=c++20, -O3`)

Queue item type = `std::int64_t`

| Class     | Operations/sec |
| --------- | -------------- |
| SpscFifo0 | 5,932,375      |
| SpscFifo1 | 55,924,026     |
| SpscFifo2 | 160,280,425    |

*Note (24-08-2024): I'd like to come back to this project at some point. I would especially like to come back and run some more profiles on different environments, using different queue items.*
