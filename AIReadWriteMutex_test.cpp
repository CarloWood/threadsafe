#include "AIReadWriteMutex.h"

#include <iostream>
#include <thread>
#include <vector>
#include <cassert>

int const number_of_threads = std::thread::hardware_concurrency();
int const n = 100000;

long volatile count[9];
std::atomic<int> write_access;
std::atomic<int> read_access;
std::atomic<int> thr_count;
std::atomic<int> max_readers;

inline void add(long d, int i = 0)
{
  ++write_access;
  assert(write_access == 1 && read_access == 0);
  count[0] = count[i] + d;
  --write_access;
}

inline void read(int i)
{
  ++read_access;
  assert(write_access == 0);
  count[i] = count[0];
  int v = read_access;
  if (v > max_readers)
    max_readers = v;
  --read_access;
}

AIReadWriteMutex m;

void run()
{
  int thr = ++thr_count;
  double sum = 0;
  for (int i = 0; i < n; ++i)
  {
    m.wrlock();
    add(1);
    m.wrunlock();
    for(int tries = 1;; ++tries)
    {
      std::this_thread::yield();
      m.rdlock();
      read(thr);
      std::this_thread::yield();
      try
      {
	m.rd2wrlock();
      }
      catch(std::exception const&)
      {
	// Failed to obtain the write lock because another thread is attempting
	// to convert its read lock into a write lock.
	// We have to release our read lock in order to allow the other thread
	// to succeed. Then we try again, starting with re-reading.
	m.rdunlock();
	m.rd2wryield();
	continue;
      }
      add(-1, thr);
      m.wrunlock();
      sum += tries;
      break;
    }
  }
  std::cout << "Thread " << thr << " finished: needed on average " << (sum / n) << " tries.\n";
}

int main()
{
  std::vector<std::thread> thread_pool;
  for (int i = 0; i < number_of_threads; ++i)
  {
    thread_pool.emplace(thread_pool.end(), run);
  }
  std::cout << "All started!" << std::endl;

  for (int i = 0; i < number_of_threads; ++i)
  {
    thread_pool[i].join();
  }
  std::cout << "All finished!" << std::endl;

  std::cout << max_readers << " simultaneous readers!" << std::endl;
  std::cout << "count = " << count[0] << std::endl;
}
