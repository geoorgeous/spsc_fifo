#pragma once

#include <memory>

/* 
	A simple FIFO circular queue.

	
	*NOT* suitable for SPSC multithreaded use as there's a likely possibility
	of data-races when `pop()`ing or `push()`ing. This class is used as an
	example implementation of a FIFO data structure to build our SPSC FIFOs on
	top of.
*/

/* Note: Optional allocator type for user-specified allocation policies. */
template<typename T, typename TAlloc = std::allocator<T>>
class Fifo : private TAlloc
{
public:
	/* Note: std::allocator_traits (C++11)
	   See: https://en.cppreference.com/w/cpp/memory/allocator_traits */
	using allocator_traits = std::allocator_traits<TAlloc>;

	using size_type = typename allocator_traits::size_type;
	using value_type = T;

	/* Note: We mark the constructor as explicit to avoid unexpected implicit
	   type conversions. */
	explicit Fifo(size_type capacity, TAlloc const& alloc = TAlloc{})
		: TAlloc{alloc}
		, capacity_{capacity}
		, allocation_{allocator_traits::allocate(*this, capacity_)}
	{}

	/* Note: Explicity delete the copy and move constructors/operator
	   overloaders. */
	Fifo(Fifo const&) = delete;
	Fifo& operator=(Fifo const&) = delete;
	Fifo(Fifo&&) = delete;
	Fifo& operator=(Fifo&&) = delete;

	~Fifo() {
		while (!isEmpty()) {
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
		   call the destructor later in pop() and ~Fifo().
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
	size_type push_pos_;    /* Points to where new items shall be constructed */
	size_type pop_pos_;     /* Points to where items should be popped from */
};