# spsc_fifo

A lock-free SPSC FIFO data structure written in modern C++(20) for low-latency, high-performant, mulithreaded use cases.

---

This content is based off of the work done by Charles Frasch. Charles' CppCon 2023 presentation on this subject was a fantastic resource in understanding the theory of a lock-free SPSC FIFO. The `SpscFifo*` classes I've written are based off of his original implementations. For reference please see:

- [Charles Frasch - SPSC FIFO code repository](https://github.com/CharlesFrasch/cppcon2023)
- [SPSC lock-free FIFO From the Ground Up - Charles Frasch - CppCon 2023 (YouTube)](https://www.youtube.com/watch?v=K3P_Lmq6pw0)

### What is SPSC FIFO?

SPSC (Single Producer, Single Consumer) refers to the idea that a system has only two parties: a single producer that is producing and writing some abitrary content in to the system, and a single consumer that's consuming and reading that content from the system.

An SPSC FIFO...

Finally, a lock-free SPSC FIFO...

