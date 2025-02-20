#pragma once

#include <atomic>
#include <cassert>
#include <memory>

/* 
	A thread-safe Single-Consumer, Single-Producer circular FIFO queue with
	optimized inter-thread synchronization, false-sharing-avoidance, and cached
	position variables.


	Another improvement we can make to our implementation - on top of the
	previous improvements - is to cache our position-keeping variables. Each
	thread now has a copy of the variable which it would usually have to acquire
	from the other other thread. This copy is only ever accessed by the thread
	in question, and so it doesn't need to be atomic. For example, there's now a
	copy of the `pop_pos_` variable - `pop_pos_cached_` - which the Producer
	thread will use in `push()`es.

	Now, instead of `load()`ing the `pop_pos_` variable every time the Producer
	makes a `push()`, the Producer uses the cached variable to check if the
	queue is full. If it is, only then does the Producer thread acquire the
	'real' variable and re-cache it. It can then check again if the queue is
	definitely full. Similarly, the Consumer thread does the same thing with
	`push()` with the `push_pos_` variable.

	As before, we also must make sure that these two new variables exists on
	their own cache line, and so they are aligned in the same way as the
	original shared variables.
*/

/* Note: Optional allocator type for user-specified allocation policies. */
template<typename T, typename TAlloc = std::allocator<T>>
class SpscFifo2 : private TAlloc
{
public:
	/* Note: std::allocator_traits is C++11
	   See: https://en.cppreference.com/w/cpp/memory/allocator_traits */
	using allocator_traits = std::allocator_traits<TAlloc>;

	using size_type = typename allocator_traits::size_type;
	using value_type = T;

	/* Note: We mark the constructor as explicit to avoid unexpected implicit
	   type conversions. */
	explicit SpscFifo2(size_type capacity, TAlloc const& alloc = TAlloc{})
		: TAlloc{alloc}
		, capacity_{capacity}
		, allocation_{allocator_traits::allocate(*this, capacity_)}
	{}

	/* Note: Explicity delete the copy and move constructors/operator
	   overloaders. */
	SpscFifo2(SpscFifo2 const&) = delete;
	SpscFifo2& operator=(SpscFifo2 const&) = delete;
	SpscFifo2(SpscFifo2&&) = delete;
	SpscFifo2& operator=(SpscFifo2&&) = delete;

	~SpscFifo2()
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
		if ((push_pos - pop_pos_cached_) == capacity_)
		{
			/* Note: Reading variable written to by other thread: Acquire! */
			pop_pos_cached_ = pop_pos_.load(std::memory_order_acquire);
			if ((push_pos - pop_pos_cached_) == capacity_)
				return false;
		}
			
		/* Note: Using 'Placement new' (C++17) to construct the object at the
		   previously-allocated block of memory. This does mean we must manually
		   call the destructor later in pop() and ~SpscFifo2().
		   See: https://en.cppreference.com/w/cpp/language/new#Placement_new */
		new (&allocation_[push_pos % capacity_]) T(value);

		/* Note: Writing variable read by other thread: Release! */
		push_pos_.store(push_pos + 1, std::memory_order_release);
		
		return true;
	}

	bool pop(T& value)
	{
		/* Note: Use Relaxed operation ordering policy. */
		const size_type pop_pos = pop_pos_.load(std::memory_order_relaxed);
		if (push_pos_cached_ == pop_pos)
		{
			/* Note: Reading variable written to by other thread: Acquire! */
			push_pos_cached_ = push_pos_.load(std::memory_order_acquire);
			if (push_pos_cached_ == pop_pos)
				return false;
		}

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

	/* Exclusive to Consumer thread. */
	alignas(hardware_destructive_interference_size) size_type push_pos_cached_{};
	
	/* Exclusive to Producer thread. */
	alignas(hardware_destructive_interference_size) size_type pop_pos_cached_{};

	/* Node: Padding at the end of our class instance to avoid false
	   sharing with nearby objects. Padding is equal to the HDIS minus the
	   size of the above atomic variables' underlying type. */
	char padding_[hardware_destructive_interference_size - sizeof(size_type)];
};