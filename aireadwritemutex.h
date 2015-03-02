/**
 * @file aireadwritemutex.h
 * @brief Implementation of AIReadWriteMutex.
 *
 * Copyright (c) 2010 - 2015, Aleric Inglewood.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * CHANGELOG
 *   and additional copyright holders.
 *
 *   01/03/2015
 *   Moved code from Singularity to separate repository.
 *   Changed the license to the GNU Affero General Public License.
 *   Major rewrite to make it more generic and use C++11 thread support.
 */

#ifndef AIREADWRITEMUTEX_H
#define AIREADWRITEMUTEX_H

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <exception>

class AIReadWriteMutex
{
  public:
    AIReadWriteMutex(void) : m_readers_count(0), m_waiting_writer(false) { }

  private:
    std::mutex m_waiting_writers_mutex;				//!< This mutex is locked while a thread is trying to obtain a write lock,
    								//!< causing all other threads to block upon entering rdlock(), wrlock() or rd2wrlock().
    std::atomic<int> m_rd2wr_count;				//!< Number of threads that try to go from a read lock to a write lock.
    std::mutex m_state_mutex;					//!< Guards all member variables below.
    std::condition_variable m_condition_unlocked;		//!< Condition variable used to wait for no readers or writers left (to tell waiting writers).
    std::condition_variable m_condition_no_writer_left;		//!< Condition variable used to wait for no writers left (to tell waiting readers).
    std::condition_variable m_condition_one_reader_left;	//!< Condition variable used to wait for one reader left (to tell that reader that it can become a writer).
    int m_readers_count;					//!< Number of readers or -1 if a writer locked this object.
    bool m_waiting_writer;					//!< True if one or more threads are waiting for a write lock. Used to block new readers.

  public:
    void rdlock(bool high_priority = false)
    {
      std::unique_lock<std::mutex> lk(m_state_mutex);					// Get exclusive access.

      // Fuzzy delay to give writers a higher priority.
      if (m_waiting_writer && !high_priority)						// If there is a writer interested,
      {
	lk.unlock();
	m_waiting_writers_mutex.lock(); 						// then give it precedence and wait here.
	// If we get here then the writer got its access and m_readers_count is -1.
	m_waiting_writers_mutex.unlock();
	lk.lock();
      }

      // This is the actual rdlock code.
      m_condition_no_writer_left.wait(lk, [&]{return m_readers_count >= 0;});		// Wait till m_readers_count is no longer -1.
      ++m_readers_count;								// One more reader.
    }

    void rdunlock(void)
    {
      std::unique_lock<std::mutex> lk(m_state_mutex);					// Get exclusive access.
      if (--m_readers_count <= 1)							// Decrease reader count. Was this the (second) last reader?
      {
	if (m_readers_count == 1)							// Still one reader left, so only notify m_condition_one_reader_left.
	{
	  m_condition_one_reader_left.notify_one();
	  return;
	}
	// In most practical cases there are no race conditions, so it is more efficient to first unlock m_state_mutex and only then kick waiting threads:
	// if we did that the other way around then threads woken up would immediately block again on trying to obtain m_state_mutex before leaving wait().
	// In the case that we try to wake up threads who can't be woken up because another thread now locked this object then the wait predicate will stop
	// them from being woken up.
	lk.unlock();
	m_condition_unlocked.notify_one();						// No readers left, tell waiting writers.
      }
    }

    void wrlock(void)
    {
      std::lock_guard<std::mutex> writer_waiting_guard(m_waiting_writers_mutex);	// Block new threads from getting in a wait queue.
      std::unique_lock<std::mutex> lk(m_state_mutex);					// Get exclusive access.
      m_waiting_writer = true;								// Also block threads from getting in the m_condition_no_writer_left wait queue.
      m_condition_unlocked.wait(lk, [&]{return m_readers_count == 0;});			// Wait untill m_readers_count is 0 (nobody else has the lock).
      m_waiting_writer = false;								// Stop blocking new readers from getting in the wait quue.
      m_readers_count = -1;								// We are a writer now.
    }											// Unlock m_waiting_writers_mutex and allow blocked threads to trickle into the wait queues:
    											// only one writer can be added to this wait queue and/or multiple readers (if there is
											// no writer left then all readers will be added).
    void rd2wrlock(void)
    {
      if (++m_rd2wr_count > 1)								// Only the first thread that calls rd2wrlock will get passed this.
      {											// Here m_rd2wr_count could equal two, hence that we have to test > 1 in the previous line as opposed to == 2.
	--m_rd2wr_count;
	// It is impossible to recover from this: two threads have a read lock
	// and require to turn that into a write lock. The only way out of this
	// is to throw an exception and let the caller solve the mess.
	// To solve this situation, either only use rd2wrlock() from a single
	// thread (other threads may still call the other member functions), or
	// catch this exception and call rdunlock() and then std::this_thread::yield().
	// After that a new attempt can be made.
	throw std::exception();
      }
      std::unique_lock<std::mutex> writer_waiting_guard(m_waiting_writers_mutex, std::try_to_lock);	// Block new threads from getting in a wait queue.
      std::unique_lock<std::mutex> lk(m_state_mutex);					// Get exclusive access.
      m_waiting_writer = true;								// Also block threads from getting in the m_condition_no_writer_left wait queue.
      m_condition_one_reader_left.wait(lk, [&]{return m_readers_count == 1;});		// Wait till m_readers_count is 1 (only this thead has its read lock).
      m_waiting_writer = false;								// Stop blocking new readers from getting in the wait quue.
      m_readers_count = -1;								// We are a writer now.
      --m_rd2wr_count;									// Allow additional calls to rd2wrlock().
    }											// Unlock m_waiting_writers_mutex and allow blocked threads to trickle into the wait queues:
    											// only one writer can be added to this wait queue and/or multiple readers (if there is
											// no writer left then all readers will be added).
    void wrunlock(void)
    {
      m_state_mutex.lock();								// Get exclusive access.
      m_readers_count = 0;								// We have no writer anymore.
      m_state_mutex.unlock();								// Release m_state_mutex so that threads can leave their respective wait() immediately.
      m_condition_unlocked.notify_one();						// Tell waiting writers.
      m_condition_no_writer_left.notify_one();						// Tell waiting readers.
    }

    void wr2rdlock(void)
    {
      m_state_mutex.lock();								// Get exclusive access.
      m_readers_count = 1;								// Turn writer into a reader.
      m_state_mutex.unlock();								// Release m_state_mutex so that threads can leave their respective wait() immediately.
      // Don't call m_condition_one_reader_left.notify_one() because it is impossible that any thread
      // is waiting there: they'd need to have been a reader before and that is not allowed while
      // we had the write lock.
      m_condition_no_writer_left.notify_one();						// Tell waiting readers.
    }
};

#endif // AIREADWRITEMUTEX_H
