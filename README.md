# C++ essential additions

## ActorThread: Active Object pattern in C++

Implementation of the
[Active Object pattern](http://www.drdobbs.com/parallel/prefer-using-active-objects-instead-of-n/225700095)
wrapping a standard C++11 thread.

### Simple
* Whole implementation contained in **a single header file!**
* Inherit from a template and you are done. See the examples directory.

### Main features
* Exchange messages of any type (does not requires them to derive from a common base class)
* Messages are delivered in the order they arrive
* Allows to invoke callbacks on clients of unknown type (useful for libraries)
* Callbacks on the active object *auto-store themselves* with no boilerplate code
* Timers ability with *client-driven handlers* (no need for handler&harr;object resolving maps)

### Robustness
* The wrapped thread lifecycle overlaps and is driven by the object existence
* The object is kept alive by smart pointers (whoever has a reference can safely send messages)
* No internal strong references (only the final users determine the destruction/end)
* Nonetheless, callbacks onto already deleted active objects do not crash the application

### Minimum compiler required
* Mininum gcc version supported is 4.9.0 (which fixed [#57172](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=57172))
* Works with clang 3.3 and Visual Studio 2015 Update 3 (no previous versions tested on both)
* Clean, standard C++11 (no conditional code, same implementation for all platforms)
