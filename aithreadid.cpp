/**
 * @file aithreadid.cpp
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
 * CHANGELOG
 *   and additional copyright holders.
 *
 *   27/02/2015
 *   - Initial version, written by Aleric Inglewood @ SL
 */

#include "aithreadid.h"

std::thread::id const s_main_thread_id = std::this_thread::get_id();
