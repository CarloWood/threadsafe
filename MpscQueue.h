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

struct MpscNode
{
  std::atomic<MpscNode*> m_next;
};

class MpscQueue
{
 private:
  std::atomic<MpscNode*> m_head;
  MpscNode*              m_tail;
  MpscNode               m_stub;

 public:
  MpscQueue() : m_head(&m_stub), m_tail(&m_stub), m_stub{nullptr} { }

  void push(MpscNode* node)
  {
    node->m_next.store(nullptr);
    MpscNode* prev = std::atomic_exchange(&m_head, node);
    prev->m_next.store(node);
  }

  MpscNode* pop()
  {
    mpscq_node_t* tail = m_tail;
    mpscq_node_t* next = tail->m_next.load();
    if (tail == &m_stub)
    {
      if (nullptr == next)
        return nullptr;
      m_tail = next;
      tail = next;
      next = m_next->m_next.load();
    }
    if (next)
    {
      m_tail = next;
      return tail;
    }
    mpscq_node_t* head = m_head.load();
    if (tail != head)
      return nullptr;
    push(&m_stub);
    next = tail->m_next.load();
    if (next)
    {
      m_tail = next;
      return tail;
    }
    return nullptr;
  }
};
