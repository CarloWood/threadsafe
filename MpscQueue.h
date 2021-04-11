#pragma once

// Translated from http://www.1024cores.net/home/lock-free-algorithms/queues/intrusive-mpsc-node-based-queue
// into modern C++ by Carlo Wood (c) 2021.
//
// For this reason this file has the following license.
//
// Copyright (c) 2010-2011 Dmitry Vyukov. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
//
//   1. Redistributions of source code must retain the above copyright notice, this list of
//      conditions and the following disclaimer.
//
//   2. Redistributions in binary form must reproduce the above copyright notice, this list
//      of conditions and the following disclaimer in the documentation and/or other materials
//      provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY DMITRY VYUKOV "AS IS" AND ANY EXPRESS OR IMPLIED
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
// SHALL DMITRY VYUKOV OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
// BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
// DAMAGE.
//
// The views and conclusions contained in the software and documentation are those of
// the authors and should not be interpreted as representing official policies,
// either expressed or implied, of Dmitry Vyukov.
//

#include <atomic>

namespace aithreadsafe {

struct MpscNode
{
  std::atomic<MpscNode*> m_next;
};

// MpscQueue
//
// Empty list:
//
//   m_tail ---> m_stub ===> nullptr
//                  ^
//                  |
//                m_head
//
// where '--->' means "points to the node",
// and '===>' "is a node with an m_next pointer that points to".
//
// Apply one push on an empty list:
//
//   m_tail ---> m_stub ===> node1 ===> nullptr
//                             ^
//                             |
//                           m_head
//
// A list with one or more nodes:
//
//   m_tail ---> [m_stub ===>] node1 ===> node2 ===> ... ===> nodeN ===> nullptr
//                                                              ^
//                                                              |
//                                                            m_head
//
// If no pop was performed since the last time the list was empty,
// then m_tail points to m_stub. If one or more pop's where performed
// not leading to an empty list then m_tail points to the next node
// that will be popped.
//
// Because the last line in push fixes the m_next pointer that m_head was
// pointing at (from nullptr to the new node), a list that has a few nodes
// pushed concurrently can also look like this:
//
//   m_tail ---> [m_stub ===>] node1 ===> node2 ===> ... ===> nodeK ===> nullptr, nodeL ===> nullptr, nodeM ===> nullptr, nodeN ===> nullptr
//                                                                                                                          ^
//                                                                                                                          |
//                                                                                                                        m_head
// Where the m_next pointers of nodes K, L and M are filled in "later" in any order, to end as
//
//   m_tail ---> [m_stub ===>] node1 ===> node2 ===> ... ===> nodeK ===> nodeL ===> nodeM ===> nodeN ===> nullptr
//                                                                                               ^
//                                                                                               |
//                                                                                             m_head
//
// Hence, trying to pop a node that was already pushed might fail (returning nullptr) if
// not all previous pushes also finished.
//
class MpscQueue
{
 private:
  std::atomic<MpscNode*> m_head;
  MpscNode*              m_tail;
  MpscNode               m_stub;

 public:
  MpscQueue() : m_head(&m_stub), m_tail(&m_stub), m_stub{{nullptr}} { }

  void push(MpscNode* node)
  {
    node->m_next.store(nullptr);
    MpscNode* prev = std::atomic_exchange(&m_head, node);
    // Here m_head points to the new node, which either points to null
    // or already points to the NEXT node that was pushed AND completed, etc.
    // Now fix the next pointer of the node that m_head was pointing at.
    prev->m_next.store(node);
  }

  MpscNode* pop()
  {
    MpscNode* tail = m_tail;
    MpscNode* next = tail->m_next.load();
    if (tail == &m_stub)
    {
      // If m_tail ---> m_stub ===> nullptr, at the time of the above load(), then return nullptr.
      // This is only the case for an empty list (or when the first push (determined by the first push
      // that performed the atomic_exchange) to an empty list didn't complete yet).
      if (nullptr == next)
        return nullptr;
      // Skip m_stub.
      m_tail = next;
      tail = next;
      next = next->m_next.load();
    }
    // Are there at least two nodes in the list?
    // Aka, m_tail ---> node1 ===> node2
    if (next)
    {
      // Remove node and return it.
      m_tail = next;
      return tail;
    }
    // If we get here we had the situation, at the time of the above load(), of
    // m_tail ---> [m_stub ===>] node1 ===> nullptr and we now have
    // tail ---> node1, where it at least very recently, node1 ===> nullptr.
    MpscNode* head = m_head.load();
    // If head was changed in the meantime then a push is or was in progress
    // and we fail to read node1 for now.
    if (tail != head)
      return nullptr;
    // Make sure we have at least two nodes again.
    push(&m_stub);
    // In the simplest case of no other races we now have:
    //
    //   tail ---> node1 ===> m_stub ===> nullptr
    //                           ^
    //                           |
    //                         m_head
    //
    // If there where other, not yet completed pushes however, we
    // can have a situation like this:
    //
    //   tail --->  node1 ===> nullptr, node2 ===> node3 ===> nullptr, m_stub ===> nullptr
    //                                                                   ^
    //                                                                   |
    //                                                                 m_head
    // where node2 was (incompletely) pushed before we pushed &m_stub.
    // In that case this next will be null:
    next = tail->m_next.load();
    if (next)
    {
      // Remove node and return it.
      m_tail = next;
      return tail;
    }
    return nullptr;
  }
};

} // namespace aithreadsafe
