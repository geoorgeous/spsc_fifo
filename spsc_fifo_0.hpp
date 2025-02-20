#pragma once

#include <atomic>
#include <cassert>
#include <memory>

/* 
	A thread-safe Single-Consumer, Single-Producer circular FIFO queue.


	Exact same as [Fifo](./fifo.hpp) except for one thing: we use an atomic
	(`std::atomic` (C++11)) type for our variables that keep track of our push
	and pop positions. This ensures well-defined behaviour when our threads
	read/write these variables. Now one thread can call `push()` (the Producer
	thread), and another thread can call `pop()` (the Consumer thread) without
	UB.

	See: https://en.cppreference.com/w/cpp/atomic/atomic
*/

/* Note: Optional allocator type for user-specified allocation policies. */
template<typename T, typename TAlloc = std::allocator<T>>
class SpscFifo0 : private TAlloc
{
public:
	/* Note: std::allocator_traits (C++11)
	   See: https://en.cppreference.com/w/cpp/memory/allocator_traits */
	using allocator_traits = std::allocator_traits<TAlloc>;

	using size_type = typename allocator_traits::size_type;
	using value_type = T;

	/* Note: We mark the constructor as explicit to avoid unexpected implicit
	   type conversions. */
	explicit SpscFifo0(size_type capacity, TAlloc const& alloc = TAlloc{})
		: TAlloc{alloc}
		, capacity_{capacity}
		, allocation_{allocator_traits::allocate(*this, capacity_)}
	{}

	/* Note: Explicity delete the copy and move constructors/operator
	   overloaders. */
	SpscFifo0(SpscFifo0 const&) = delete;
	SpscFifo0& operator=(SpscFifo0 const&) = delete;
	SpscFifo0(SpscFifo0&&) = delete;
	SpscFifo0& operator=(SpscFifo0&&) = delete;

	~SpscFifo0()
	{
		while (!isEmpty())
		{
			allocation_[pop_pos_ % capacity_].~T();
			++pop_pos_;
		}
		allocator_traits::deallocate(*this, allocation_, capacity_);
	}

	size_type getCapacity() const noexcept { return capacity_; }

	size_type getSize() const noexcept { return push_pos_ - pop_pos_; }

	bool isEmpty() const noexcept { return getSize() == 0; }

	bool isFull() const noexcept { return getSize() == capacity_; }

	bool push(T const& value)
	{
		if (isFull())
			return false;
			
		/* Note: Using 'Placement new' (C++17) to construct the object at the
		   previously-allocated block of memory. This does mean we must manually
		   call the destructor later in pop() and ~SpscFifo0().
		   See: https://en.cppreference.com/w/cpp/language/new#Placement_new */
		new (&allocation_[push_pos_ % capacity_]) T(value);
		++push_pos_;
		return true;
	}

	bool pop(T& value)
	{
		if (isEmpty())
			return false;

		T& t = allocation_[pop_pos_ % capacity_];
		value = t;
		t.~T();

		++pop_pos_;
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

	/* Points to where new items shall be constructed. 
	   Note: Read and written-to by the Producer thread.
	   Read by the Consumer thread. */
	pos_type push_pos_;     /* Points to where new items shall be constructed */

	
	/* Points to where items should be popped from. 
	   Note: Read and written-to by the Consumer thread.
	   Read by the Producer thread. */
	pos_type pop_pos_;
};