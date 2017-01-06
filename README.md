# ai-threadsafe submodule

This repository is a [git submodule](https://git-scm.com/book/en/v2/Git-Tools-Submodules)
providing C++ utilities for larger projects, including:

* <tt>aithreadsafe::Wrapper&lt;T, policy::P&gt;</tt> : template class to construct a T / mutex pair with locking policy P.
* <tt>ReadWrite</tt>, <tt>Primitive</tt>, <tt>OneThread</tt> : Locking policies.
* <tt>AccessConst</tt> and <tt>Access</tt> : Obtain read/write access to Primitive or OneThread locked objects.
* <tt>ConstReadAccess</tt>, <tt>ReadAccess</tt> and <tt>WriteAccess</tt> : Obtain access to ReadWrite projected objects.
* <tt>AIReadWriteMutex</tt> : A mutex class that provides read/write locking.
* Several utilities like <tt>is_single_threaded</tt>.

The root project should be using
[autotools](https://en.wikipedia.org/wiki/GNU_Build_System autotools),
[cwm4](https://github.com/CarloWood/cwm4) and
[libcwd](https://github.com/CarloWood/libcwd).

## Example

For example, to create an object of type <tt>Foo</tt>
that has read/write protection, one could do:

```C++
using namespace aithreadsafe;
typedef Wrapper<Foo, policy::ReadWrite<AIReadWriteMutex>> foo_t;

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

## Checking out a project that uses the ai-threadsafe submodule.

To clone a project example-project that uses ai-threadsafe simply run:

<pre>
<b>git clone --recursive</b> &lt;<i>URL-to-project</i>&gt;<b>/example-project.git</b>
<b>cd example-project</b>
<b>./autogen.sh</b>
</pre>

The <tt>--recursive</tt> is optional because <tt>./autogen.sh</tt> will fix
it when you forgot it.

Afterwards you probably want to use <tt>--enable-mainainer-mode</tt>
as option to the generated <tt>configure</tt> script.

## Adding the ai-threadsafe submodule to a project

To add this submodule to a project, that project should already
be set up to use [cwm4](https://github.com/CarloWood/cwm4).

Simply execute the following in a directory of that project
where you what to have the <tt>threadsafe</tt> subdirectory:

<pre>
git submodule add https://github.com/CarloWood/ai-threadsafe.git threadsafe
</pre>

This should clone ai-threadsafe into the subdirectory <tt>threadsafe</tt>, or
if you already cloned it there, it should add it.

Changes to <tt>configure.ac</tt> and <tt>Makefile.am</tt>
are taken care of my <tt>cwm4</tt>, except for linking
which works as usual.

For example a module that defines a

<pre>
bin_PROGRAMS = foobar
</pre>

would also define

<pre>
foobar_CXXFLAGS = @LIBCWD_FLAGS@
foobar_LDADD = ../utils/libutils.la ../cwd/libcwd.la @LIBCWD_LIBS@
</pre>

or whatever the path to `utils` is, to link with libutils, and
assuming you'd also use the [cwd](https://github.com/CarloWood/cwd) submodule.

Finally, run

<pre>
./autogen.sh
</pre>

to let cwm4 do its magic, and commit all the changes.

Checkout [ai-threadsafe-testsuite](https://github.com/CarloWood/ai-threadsafe-testsuite)
for an example of a project that uses this submodule.
