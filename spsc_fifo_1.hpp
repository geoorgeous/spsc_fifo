#pragma once

#include <atomic>
#include <cassert>
#include <memory>

/* 
	A thread-safe Single-Consumer, Single-Producer circular FIFO queue with
	optimized inter-thread synchronization, and false-sharing-avoidance.


	The first thing we've done is specified the atomic operation ordering policy
	of read/writes via the `atomic::load()` and `atomic::store()` functions for
	our position-keeping variables. Before this, read/writes were using the
	default Sequentially-consistent ordering policy. From cppreference.com:

		"The default behavior of all atomic operations in the library provides
		for sequentially consistent ordering (see discussion below). That
		default can hurt performance, but the library's atomic operations can
		be given an additional `std::memory_order` argument to specify the exact
		constraints, beyond atomicity, that the compiler and processor must
		enforce for that operation."

	For variables that we know aren't written to on other threads, we can use
	the Relaxed ordering policy. This removes unneccessary and costly
	synchronization constraints that were in place before. For example, we never
	have to worry about synchronization semantics on the Producer thread when
	we're reading the `push_pos_` variable because the Producer thread is the
	only thread that ever writes to that variable.

	When a thread reads a variable that the other thread writes to, or if a
	thread writes to a variable that the other thread reads from, we do indeed
	care about synchronizating the order of operations. For this we specify the
	Release-Acquire ordering policy. This ensures that any writes to a variable
	made from one thread are visible to the other thread.

	See: https://en.cppreference.com/w/cpp/atomic/memory_order


	The second improvement present in `SpscFifo1` is a reduction in
	false-sharing. In short, false-sharing occurs when a thread 't1' alters a
	variable 'v1' which shares a cache line with another variable 'v2' that's
	being accessed by another thread 't2'. Even though there was no alteration
	to 'v2', 't2' is still forced to reload the cache line. Thus, it would
	appear to the programmer that one thread has inadvertently interrupted the
	other.

	Though the consequences of this surprising side-effect are pretty dire in
	terms of performance, the solution is luckily quite straight forward. We
	simply specify an alignment for our variables of atleast the size of our
	cache line. This way only a single one of our variables can fit inside a
	cache line, preventing other variables from being inadvertently invalidated.

	See: https://en.wikipedia.org/wiki/False_sharing
	See also: https://www.youtube.com/watch?v=O0HCGOzFLm0
	See also: https://en.cppreference.com/w/cpp/language/alignas
*/

/* Note: Optional allocator type for user-specified allocation policies. */
template<typename T, typename TAlloc = std::allocator<T>>
class SpscFifo1 : private TAlloc
{
public:
	/* Note: std::allocator_traits is C++11
	   See: https://en.cppreference.com/w/cpp/memory/allocator_traits */
	using allocator_traits = std::allocator_traits<TAlloc>;

	using size_type = typename allocator_traits::size_type;
	using value_type = T;

	/* Note: We mark the constructor as explicit to avoid unexpected implicit
	   type conversions. */
	explicit SpscFifo1(size_type capacity, TAlloc const& alloc = TAlloc{})
		: TAlloc{alloc}
		, capacity_{capacity}
		, allocation_{allocator_traits::allocate(*this, capacity_)}
	{}

	/* Note: Explicity delete the copy and move constructors/operator
	   overloaders. */
	SpscFifo1(SpscFifo1 const&) = delete;
	SpscFifo1& operator=(SpscFifo1 const&) = delete;
	SpscFifo1(SpscFifo1&&) = delete;
	SpscFifo1& operator=(SpscFifo1&&) = delete;

	~SpscFifo1()
	{
		while (!isEmpty())
		{
			allocation_[pop_pos_ % capacity_].~T();
			++pop_pos_;
		}
		allocator_traits::deallocate(*this, allocation_, capacity_);
	}

	size_type getCapacity() const noexcept { return capacity_; }

	size_type getSize() const noexcept
	{
		/* Note: We prevent the default usage of the Sequentailly-consistent
		   operation ordering policy by specifying these load() operations to
		   use the Relaxed ordering policy, avoiding the unnecessary
		   and costly thread synchronization constraints. */
		const size_type push_pos = push_pos_.load(std::memory_order_relaxed);
		const size_type pop_pos = pop_pos_.load(std::memory_order_relaxed);
		return push_pos - pop_pos;
	}

	bool isEmpty() const noexcept { return getSize() == 0; }

	bool isFull() const noexcept { return getSize() == capacity_; }

	bool push(T const& value)
	{
		/* Note: Use Relaxed operation ordering policy. */
		const size_type push_pos = push_pos_.load(std::memory_order_relaxed);

		/* Note: Reading variable written to by other thread: Acquire! */
		const size_type pop_pos = pop_pos_.load(std::memory_order_acquire);

		if ((push_pos - pop_pos) == capacity_)
			return false;
			
		/* Note: Using 'Placement new' (C++17) to construct the object at the
		   previously-allocated block of memory. This does mean we must manually
		   call the destructor later in pop() and ~SpscFifo1().
		   See: https://en.cppreference.com/w/cpp/language/new#Placement_new */
		new (&allocation_[push_pos % capacity_]) T(value);

		/* Note: Writing variable read by other thread: Release! */
		push_pos_.store(push_pos + 1, std::memory_order_release);
		
		return true;
	}

	bool pop(T& value)
	{
		/* Note: Accessing variable written to by other thread: Acquire! */
		const size_type push_pos = push_pos_.load(std::memory_order_acquire);
		
		/* Note: Use Relaxed operation ordering policy. */
		const size_type pop_pos = pop_pos_.load(std::memory_order_relaxed);
		if (push_pos == pop_pos)
			return false;

		T& t = allocation_[pop_pos % capacity_];
		value = t;
		t.~T();

		/* Note: Writing variable read by other thread: Release! */
		pop_pos_.store(pop_pos + 1, std::memory_order_release);

		return true;
	}

private:
	size_type capacity_;    /* Maximum number of items */
	T*        allocation_;  /* Handle to our allocated block of memory */

	using pos_type = std::atomic<size_type>;

	/* Note: Here we make sure to assert that the size_type the user is using
	   is_always_lock_free (C++17) when used with std::atomic. If it's not,
	   then that defeats the entire purpose of this data structure!
	   See: https://en.cppreference.com/w/cpp/atomic/atomic/is_always_lock_free */
	static_assert(pos_type::is_always_lock_free);

	/* Note: Using hardcoded alignment value instead of
	   std::hardware_destructive_interference_size.
	   See: g++ output:
	   error: use of ‘std::hardware_destructive_interference_size’ [-Werror=interference-size]
	   note: its value can vary between compiler versions or with different ‘-mtune’ or ‘-mcpu’ flags
	   note: if this use is part of a public ABI, change it to instead use a constant variable you define
	   note: the default value for the current CPU tuning is 64 bytes
	   note: you can stabilize this value with ‘--param hardware_destructive_interference_size=64’, or disable this warning with ‘-Wno-interference-size’ */
	static constexpr size_type hardware_destructive_interference_size =
		size_type{64};

	/* Points to where new items shall be constructed. 
	   Note: Read and written-to by the Producer thread.
	   Read by the Consumer thread.
	   Aligned to its own cache line to avoid false-sharing. */
	alignas(hardware_destructive_interference_size) pos_type push_pos_;

	/* Points to where items should be popped from. 
	   Note: Read and written-to by the Consumer thread.
	   Read by the Producer thread.
	   Aligned to its own cache line to avoid false-sharing. */
	alignas(hardware_destructive_interference_size) pos_type pop_pos_;

	/* Node: Padding at the end of our class instance to avoid false
	   sharing with nearby objects. Padding is equal to the HDIS minus the
	   size of the above atomic variables' underlying type. */
	char padding_[hardware_destructive_interference_size - sizeof(size_type)];
};