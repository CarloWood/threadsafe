# threadsafe submodule

This repository is a [git submodule](https://git-scm.com/book/en/v2/Git-Tools-Submodules)
providing C++ utilities for larger projects, including:

* <tt>threadsafe::Wrapper&lt;T, policy::P&gt;</tt> : template class to construct a T / mutex pair with locking policy P.
* <tt>ReadWrite</tt>, <tt>Primitive</tt>, <tt>OneThread</tt> : Locking policies.
* <tt>AccessConst</tt> and <tt>Access</tt> : Obtain read/write access to Primitive or OneThread locked objects.
* <tt>ConstReadAccess</tt>, <tt>ReadAccess</tt> and <tt>WriteAccess</tt> : Obtain access to ReadWrite protected objects.
* <tt>AIReadWriteMutex</tt> : A mutex class that provides read/write locking.
* Several utilities like <tt>is_single_threaded</tt>.

The root project should be using
[cmake](https://cmake.org/overview/)
[cwm4](https://github.com/CarloWood/cwm4) and
[libcwd](https://github.com/CarloWood/libcwd).

## Example

For example, to create an object of type <tt>Foo</tt>
that has read/write protection, one could do:

```C++
using namespace threadsafe;
using foo_t = Wrapper<Foo, policy::ReadWrite<AIReadWriteMutex>>;

// Create an object Foo, AIReadWriteMutex pair. Foo will be inaccessible.
foo_t foo;

// A function that gets an already read-locked foo passed, created from a const foo_t.
void f(foo_t::crat const& foo_cr) { foo_cr->const_member(); }

// A function that gets a non-const foo_t passed and write locks it to get write access.
void g(foo_t& foo)
{
  foo_t::wat foo_w(foo);        // Obtain lock.
  foo_w->write_access();
  f(foo_w);                     // Being write locked is OK to pass too.
}                               // Release lock.

// A function that gets a read-locked foo passed but needs write access.
void h(foo_t::rat& foo_r)
{
  // The next line might throw, see below. Normally it would just
  // block until all other threads released their read-locks.
  foo_t::wat foo_w(foo_r);      // Convert read to write lock without releasing the lock.
  foo_w->write_access();
}

// A function that takes a read-lock most of the time
// but needs to call h() at the end.
void v(foo_t& foo)
{
  for(;;) {
    try {
      foo_t::rat foo_r(foo);
      // Stuff here during which we cannot release the read lock.
      h(foo_r); // Throws if some other thread is also trying to get a read-->write lock.
    } catch(std::exception const&) {
      foo.rd2wryield();
      continue;
    }
    break;
  }
}
```

## Checking out a project that uses the threadsafe submodule.

To clone a project example-project that uses threadsafe simply run:

    git clone --recursive <URL-to-project>/example-project.git
    cd example-project
    AUTOGEN_CMAKE_ONLY=1 ./autogen.sh

The ``--recursive`` is optional because ``./autogen.sh`` will fix
it when you forgot it.

When using [GNU autotools](https://en.wikipedia.org/wiki/GNU_Autotools) you should of course
not set ``AUTOGEN_CMAKE_ONLY``. Also, you probably want to use ``--enable-mainainer-mode``
as option to the generated ``configure`` script.

In order to use ``cmake`` configure as usual, for example to build with 16 cores a debug build:

    mkdir build_debug
    cmake -S . -B build_debug -DCMAKE_MESSAGE_LOG_LEVEL=DEBUG -DCMAKE_BUILD_TYPE=Debug -DCMAKE_VERBOSE_MAKEFILE=ON -DEnableDebugGlobal:BOOL=OFF
    cmake --build build_debug --config Debug --parallel 16

Or to make a release build:

    mkdir build_release
    cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release
    cmake --build build_release --config Release --parallel 16

## Adding the threadsafe submodule to a project

To add this submodule to a project, that project should already
be set up to use [utils](https://github.com/CarloWood/ai-utils).

Then simply execute the following in a directory of that project
where you want to have the ``threadsafe`` subdirectory (the
root of the project is recommended as that is the only thing
I've tested so far):

    git submodule add https://github.com/CarloWood/threadsafe.git

This should clone threadsafe into the subdirectory ``threadsafe``, or
if you already cloned it there, it should add it.

Checkout [threadsafe-testsuite](https://github.com/CarloWood/threadsafe-testsuite)
for an example of a project that uses this submodule.
