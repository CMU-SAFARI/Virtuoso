#ifndef CIRCULAR_QUEUE_H
#define CIRCULAR_QUEUE_H

#include <assert.h>
#include <iterator>
#include <string.h>

// A placeholder for a 32-bit unsigned integer type.
// In a real-world scenario, this would come from a types header like <cstdint> (uint32_t).
using UInt32 = unsigned int;
// A placeholder for an 8-bit unsigned integer type.
using UInt8 = unsigned char;


template <class T> class CircularQueue
{
   private:
      const UInt32 m_size;
      volatile UInt32 m_first; // Index for the next element to be inserted
      UInt8 padding1[60];      // Padding to prevent false sharing on multi-core systems
      volatile UInt32 m_last;  // Index of the last (oldest) element
      UInt8 padding2[60];      // Padding to prevent false sharing
      T* const m_queue;        // The underlying array for the queue

   public:
      typedef T value_type;
      class iterator
      {
         private:
            CircularQueue &_queue;
            UInt32 _idx;
         public:
           using iterator_category = std::forward_iterator_tag;
           using difference_type   = std::ptrdiff_t;
           using value_type        = T;
           using pointer           = const T*;
           using reference         = const T&;

            iterator(CircularQueue &queue, UInt32 idx) : _queue(queue), _idx(idx) {}
            T& operator*() const { return _queue.at(_idx); }
            T* operator->() const { return &_queue.at(_idx); }
            iterator& operator++() { _idx++; return *this; }
            bool operator==(iterator const& rhs) const { return &_queue == &rhs._queue && _idx == rhs._idx; }
            bool operator!=(iterator const& rhs) const { return ! (*this == rhs); }
      };

      CircularQueue(UInt32 size = 63);
      CircularQueue(const CircularQueue &queue);
      ~CircularQueue();
      void push(const T& t);
      void pushCircular(const T& t);
      T& next(void);
      T pop(void);
      T& front(void);
      const T& front(void) const;
      T& back(void);
      const T& back(void) const;
      bool full(void) const;
      bool empty(void) const;
      void clear(void);
      UInt32 size(void) const;
      iterator begin(void) { return iterator(*this, 0); }
      iterator end(void) { return iterator(*this, size()); }
      T& operator[](UInt32 idx) const { return m_queue[(m_last + idx) % m_size]; }
      T& at(UInt32 idx) const { assert(idx < size()); return (*this)[idx]; }
};

/**
 * @brief Construct a new Circular Queue< T>:: Circular Queue object
 * @param size The maximum number of elements the queue can hold.
 */
template <class T>
CircularQueue<T>::CircularQueue(UInt32 size)
   // We use m_first == m_last as the empty condition.
   // To distinguish between empty and full, we can hold at most m_size-1 elements.
   // So, we allocate one extra space.
   : m_size(size + 1)
   , m_first(0)
   , m_last(0)
   , m_queue(new T[m_size])
{
}

/**
 * @brief Copy constructor. Creates a new empty queue with the same capacity.
 * @note This constructor asserts that the queue being copied is empty.
 * @param queue The CircularQueue to copy the size from.
 */
template <class T>
CircularQueue<T>::CircularQueue(const CircularQueue &queue)
   : m_size(queue.m_size)
   , m_first(0)
   , m_last(0)
   , m_queue(new T[m_size])
{
   assert(queue.size() == 0);
}

/**
 * @brief Destroy the Circular Queue< T>:: Circular Queue object
 */
template <class T>
CircularQueue<T>::~CircularQueue()
{
   delete [] m_queue;
}

/**
 * @brief Adds an element to the back of the queue.
 * @details Asserts that the queue is not full.
 * @param t The element to add.
 */
template <class T>
void
CircularQueue<T>::push(const T& t)
{
   assert(!full());
   m_queue[m_first] = t;
   m_first = (m_first + 1) % m_size;
}

/**
 * @brief Adds an element to the back of the queue. If the queue is full, the oldest element is removed.
 * @param t The element to add.
 */
template <class T>
void
CircularQueue<T>::pushCircular(const T& t)
{
  if (full())
    pop();
  push(t);
}

/**
 * @brief Gets a reference to the next available slot and advances the write pointer.
 * @details This is a less safe way to add an element, intended for performance-critical code.
 * The caller is responsible for initializing the returned reference.
 * Asserts that the queue is not full.
 * @return A reference to the slot for the new element.
 */
template <class T>
T&
CircularQueue<T>::next(void)
{
   assert(!full());
   T& t = m_queue[m_first];
   m_first = (m_first + 1) % m_size;
   return t;
}

/**
 * @brief Removes and returns the element from the front of the queue.
 * @details Asserts that the queue is not empty.
 * @return The element that was at the front of the queue.
 */
template <class T>
T
CircularQueue<T>::pop()
{
   assert(!empty());
   UInt32 idx = m_last;
   m_last = (m_last + 1) % m_size;
   return m_queue[idx];
}

/**
 * @brief Returns a reference to the element at the front of the queue.
 * @details Asserts that the queue is not empty.
 * @return A reference to the front element.
 */
template <class T>
T &
CircularQueue<T>::front()
{
   assert(!empty());
   return m_queue[m_last];
}

/**
 * @brief Returns a const reference to the element at the front of the queue.
 * @details Asserts that the queue is not empty.
 * @return A const reference to the front element.
 */
template <class T>
const T &
CircularQueue<T>::front() const
{
   assert(!empty());
   return m_queue[m_last];
}

/**
 * @brief Returns a reference to the element at the back of the queue.
 * @details Asserts that the queue is not empty.
 * @return A reference to the back element.
 */
template <class T>
T &
CircularQueue<T>::back()
{
   assert(!empty());
   return m_queue[(m_first + m_size - 1) % m_size];
}

/**
 * @brief Returns a const reference to the element at the back of the queue.
 * @details Asserts that the queue is not empty.
 * @return A const reference to the back element.
 */
template <class T>
const T &
CircularQueue<T>::back() const
{
   assert(!empty());
   return m_queue[(m_first + m_size - 1) % m_size];
}

/**
 * @brief Checks if the queue is full.
 * @return true if the queue is full, false otherwise.
 */
template <class T>
bool
CircularQueue<T>::full(void) const
{
   return (m_first + 1) % m_size == m_last;
}

/**
 * @brief Checks if the queue is empty.
 * @return true if the queue is empty, false otherwise.
 */
template <class T>
bool
CircularQueue<T>::empty(void) const
{
   return m_first == m_last;
}

/**
 * @brief Returns the number of elements in the queue.
 * @return The current size of the queue.
 */
template <class T>
UInt32
CircularQueue<T>::size() const
{
   return (m_first + m_size - m_last) % m_size;
}

/**
 * @brief Clears the queue, making it empty.
 * @details This operation is very fast as it only resets an index pointer.
 * It does not deallocate or destroy the elements in the underlying array.
 */
template <class T>
void
CircularQueue<T>::clear()
{
   m_first = m_last;
}


#endif // CIRCULAR_QUEUE_H
