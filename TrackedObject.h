#ifndef THREADSAFE_TRACKED_OBJECT_H
#define THREADSAFE_TRACKED_OBJECT_H

#include "AIReadWriteSpinLock.h"
#include "threadsafe.h"
#include "utils/Badge.h"
#include "utils/is_complete.h"
#include <memory>
#include "debug.h"

// threadsafe::ObjectTracker
//
// Allows objects to have a non-moving tracker object, allocated on the heap,
// that manages a pointer that points back to them, all while being thread-safe.
//
// This allows other objects to point to the tracker object without having
// to worry if the tracked object is moved in memory.
//
// Usage:
#if -0 // EXAMPLE_CODE

// Forward declare the object that should be tracked and protected against concurrent access.
class locked_Node;

// Define the Unlocked version (using the desired locking policy).
using Node = threadsafe::UnlockedTrackedObject<locked_Node, threadsafe::policy::ReadWrite<AIReadWriteMutex>>;

// Define a corresponding tracker class.
using NodeTracker = threadsafe::ObjectTracker<Node>;

// Optionally derive from ObjectTracker.
class NodeTracker : public threadsafe::ObjectTracker<Node>
{
 public:
  // The argument must be a Node const& which is passed to ObjectTracker base class.
  NodeTracker(Node const& tracked) : threadsafe::ObjectTracker<Node>(tracked) { }
};

// Note: if you run into the compile error 'incomplete locked_Node' during the
// instantiation of the destructor of this NodeTracker class, then move its
// constructors and destructor out of the header (to a .cxx file).

// The to-be-tracked object must be derived from threadsafe::TrackedObject,
// passing both, the Unlocked type as well as the tracker class.
class locked_Node : public threadsafe::TrackedObject<Node, NodeTracker>
{
 private:
  std::string s_;

 public:
  Node(std::string const& s) : s_(s) { }

  std::string const& s() const { return s_; }
};

int main()
{
  // Now one can construct a Node object:
  Node node("hello");
  // And obtain the tracker at any moment (also after it was moved):
  std::weak_ptr<NodeTracker> node_tracker = node;

  // Even if node is moved,
  Node node2(std::move(node));
  // node_tracker will point to node2:
  auto node_r{node_tracker.lock()->tracked_rat()};
  std::cout << "s = " << node_r->s() << std::endl;  // Prints "s = hello".
}
#endif // EXAMPLE_CODE

namespace threadsafe {

// TrackedObject
//
// Base class of the unlocked data type.
// The template parameters must be (derived from) UnlockedTrackedObject and ObjectTracker respectively.
//
template<typename TrackedType, typename Tracker>
class TrackedObject
{
 public:
  using tracked_type = TrackedType;
  using tracker_type = Tracker;

 protected:
  std::shared_ptr<Tracker> tracker_;

  TrackedObject();
  TrackedObject(TrackedObject&& orig);
  ~TrackedObject();

 public:
  // Accessor for the Tracker object. Make sure to keep the TrackedObject alive while using this.
  Tracker const& tracker() const
  {
    // Note that tracker_ can only be null when the Tracker was moved.
    // Do not call this function (or any other member function except the destructor) on a moved object!
    ASSERT(tracker_);
    return *tracker_;
  }

  Tracker& tracker()
  {
    // See above.
    ASSERT(tracker_);
    return *tracker_;
  }

  // Automatic conversion to a weak_ptr.
  operator std::weak_ptr<Tracker>() const { return tracker_; }
};

} // namespace threadsafe

#endif // THREADSAFE_TRACKED_OBJECT_H
