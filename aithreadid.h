/**
 * @file aithreadid.h
 * @brief Declaration of AIThreadID.
 *
 * Copyright (c) 2015, Aleric Inglewood.
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
 *   27/02/2015
 *   Initial version, written by Aleric Inglewood @ SL
 */

#ifndef AITHREADID
#define AITHREADID

#include "utils/macros.h"

#include <thread>

// Debugging function.
// Usage:
//   static std::thread::id s_id;
//   assert(is_single_threaded(s_id));	// Fails if more than one thread executes this line.
inline bool is_single_threaded(std::thread::id& thread_id)
{
  if (AI_LIKELY(thread_id == std::this_thread::get_id()))
    return true;
  bool const first_call = thread_id == std::thread::id();
  if (AI_LIKELY(first_call))
    thread_id = std::this_thread::get_id();
  return first_call;
}

#endif // AITHREADID
