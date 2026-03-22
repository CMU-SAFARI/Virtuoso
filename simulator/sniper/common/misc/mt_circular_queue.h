#ifndef MT_CIRCULAR_QUEUE_H
#define MT_CIRCULAR_QUEUE_H

#include "circular_queue.h"
#include "lock.h"
#include "cond.h"

/**
 * @brief A thread-safe circular queue.
 * @details This class extends the basic CircularQueue with locking and condition variables
 * to allow for safe concurrent access from multiple threads.
 * @tparam T The type of elements stored in the queue.
 */
template <class T> class MTCircularQueue : public CircularQueue<T>
{
   private:
      Lock m_lock;                 // Mutex to protect access to the queue's state.
      ConditionVariable m_full;    // Condition variable to signal when the queue is no longer full.
      ConditionVariable m_empty;   // Condition variable to signal when the queue is no longer empty.

   public:
      class iterator {
          public:
             // NOTE: unless we add locking, iterating over a MTCircularQueue which can be updated by another thread is unsafe!
             // A proper thread-safe iterator would need to lock the queue for its entire lifetime, which is often undesirable.
             iterator(const MTCircularQueue &queue, UInt32 idx) = delete; // Deleted to prevent unsafe use.
      };

      MTCircularQueue(UInt32 size = 64) : CircularQueue<T>(size) {}

      void push(const T& t);
      void push_wait(const T& t);
      void push_locked(const T& t);
      T pop(void);
      T pop_wait(void);
      T pop_locked(void);
      void clear(void);
      void full_wait(void);
      void empty_wait(void);
      void full_wait_locked(void);
      void empty_wait_locked(void);
};

/**
 * @brief Waits until the queue is not full. Acquires lock internally.
 */
template <class T>
void
MTCircularQueue<T>::full_wait(void)
{
   ScopedLock sl(m_lock);
   full_wait_locked();
}

/**
 * @brief Waits until the queue is not full. Assumes lock is already held.
 */
template <class T>
void
MTCircularQueue<T>::full_wait_locked(void)
{
   while(CircularQueue<T>::full())
      m_full.wait(m_lock);
}

/**
 * @brief Waits until the queue is not empty. Acquires lock internally.
 */
template <class T>
void
MTCircularQueue<T>::empty_wait(void)
{
   ScopedLock sl(m_lock);
   empty_wait_locked();
}

/**
 * @brief Waits until the queue is not empty. Assumes lock is already held.
 */
template <class T>
void
MTCircularQueue<T>::empty_wait_locked()
{
   while(CircularQueue<T>::empty())
      m_empty.wait(m_lock);
}


/**
 * @brief Pushes an element onto the queue. Assumes lock is already held.
 * @param t The element to push.
 */
template <class T>
void
MTCircularQueue<T>::push_locked(const T& t)
{
   bool wasEmpty = CircularQueue<T>::empty();

   CircularQueue<T>::push(t);

   // If the queue was empty, there might be consumers waiting. Signal one of them.
   if (wasEmpty)
      m_empty.signal();
}

/**
 * @brief Pushes an element onto the queue. Acquires lock internally.
 * @param t The element to push.
 */
template <class T>
void
MTCircularQueue<T>::push(const T& t)
{
   ScopedLock sl(m_lock);
   push_locked(t);
}

/**
 * @brief Waits until the queue is not full, then pushes an element.
 * @param t The element to push.
 */
template <class T>
void
MTCircularQueue<T>::push_wait(const T& t)
{
   ScopedLock sl(m_lock);
   full_wait_locked();
   push_locked(t);
}


/**
 * @brief Pops an element from the queue. Assumes lock is already held.
 * @return The popped element.
 */
template <class T>
T
MTCircularQueue<T>::pop_locked()
{
   bool wasFull = CircularQueue<T>::full();

   T t = CircularQueue<T>::pop();

   // If the queue was full, there might be producers waiting. Signal one of them.
   if (wasFull)
      m_full.signal();

   return t;
}

/**
 * @brief Pops an element from the queue. Acquires lock internally.
 * @return The popped element.
 */
template <class T>
T
MTCircularQueue<T>::pop()
{
   ScopedLock sl(m_lock);
   return pop_locked();
}

/**
 * @brief Waits until the queue is not empty, then pops an element.
 * @return The popped element.
 */
template <class T>
T
MTCircularQueue<T>::pop_wait()
{
   ScopedLock sl(m_lock);
   empty_wait_locked();
   return pop_locked();
}

/**
 * @brief Clears all elements from the queue.
 * @details This is a thread-safe operation. It acquires a lock, clears the queue,
 * and notifies all waiting producer threads that the queue has space available.
 */
template <class T>
void
MTCircularQueue<T>::clear()
{
    ScopedLock sl(m_lock);
    bool wasFull = CircularQueue<T>::full();

    CircularQueue<T>::clear();

    // If the queue was full, producers may be waiting for space.
    // Since the queue is now empty, there is plenty of space, so we
    // wake up all waiting producers.
    if (wasFull) {
        m_full.broadcast();
    }
}

#endif //MT_CIRCULAR_QUEUE_H
