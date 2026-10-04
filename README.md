# libecs-cpp - Entity Component System for C++

libecs-cpp is a shared C++20 library for programs built as an entity component system. Entities are the
things in the program, components are the data attached to them, and systems are the code that runs
repeatedly on that data. The library keeps them in containers, updates the systems on a schedule (on a
thread of its own for each container, or from your own loop), and carries JSON messages between systems.

## Documentation

| Document | Content |
|---|---|
| This file | Building, installing and linking the library, and a first program |
| [GUIDE.md](GUIDE.md) | A program built step by step: components, a system, a timer, a message |
| [REFERENCE.md](REFERENCE.md) | The behaviour of the library, by topic |
| [API documentation](https://metaverse-systems.github.io/libecs-cpp/) | Every class and member; build it locally with `make doxygen` |
| [NEWS.md](NEWS.md) | The versioning rule, release notes and migration steps |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Running the tests and working on the library |

## Requirements

The build system is designed around Linux. To build on Windows, use the Windows Subsystem for Linux and
the cross-build in [Building for Windows](#building-for-windows).

What you need depends on what you want to do. On Debian and Ubuntu:

| To do this | You need | Install |
| --- | --- | --- |
| Build and install the library | a C++20 compiler, make, autoconf, automake, libtool, pkg-config | `sudo apt install build-essential autoconf automake libtool pkg-config` |
| Run the tests | Catch2 v3 (`catch2-with-main` in pkg-config) | `sudo apt install catch2` |
| Build the API documentation | doxygen and graphviz | `sudo apt install doxygen graphviz` |
| Build for Windows | the mingw-w64 cross compiler and its libraries | `sudo apt install g++-mingw-w64-x86-64 gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 mingw-w64-x86-64-dev mingw-w64-tools libz-mingw-w64-dev` |
| Run the Windows programs | wine | `sudo apt install wine wine64` |

Only the first row is needed to build and install the library.

## Build and install

```
./autogen.sh
./configure
make
sudo make install
```

`./autogen.sh` generates `configure`. It is only needed in a checkout of the repository, not in a source
archive.

`make install` puts these files under the prefix, `/usr/local` by default, and nothing else:

* the headers, in `include/libecs-cpp`, including the bundled `json.hpp`;
* the shared and the static library, and `libecs-cpp.la`;
* the pkg-config file, `lib/pkgconfig/ecs-cpp.pc`.

`make uninstall` removes exactly those files.

The library can also be built in a separate directory, which leaves the source directory untouched:

```
mkdir build && cd build
../configure
make
```

## Configure options

| Option | Default | Meaning |
| --- | --- | --- |
| `--enable-tests=auto\|yes\|no` | `auto` | Build the tests. `auto` builds them when Catch2 is found, `yes` stops `configure` when it is not, `no` never builds them. |
| `--enable-werror=no\|yes` | `no` | Treat compiler warnings as errors in the library, the example and the tests. |
| `--enable-sanitizer=no\|address\|thread` | `no` | Build the library and the tests with AddressSanitizer and UndefinedBehaviorSanitizer (`address`) or with ThreadSanitizer (`thread`). Any other value stops `configure` with an error. |

The standard Autoconf options work as usual, for example `--prefix=DIR` to install somewhere other than
`/usr/local` and `--host=TRIPLET` to cross-compile.

## Building for Windows

The library is cross-compiled on Linux with the mingw-w64 compiler; the packages are in
[Requirements](#requirements). Point `pkg-config` at the Windows prefix, configure with `--host` and
install into that prefix:

```
export PKG_CONFIG_PATH=/usr/x86_64-w64-mingw32/lib/pkgconfig/
./autogen.sh
make distclean
./configure --host=x86_64-w64-mingw32 --prefix=/usr/x86_64-w64-mingw32
make
sudo make install
unset PKG_CONFIG_PATH
```

`make distclean` is needed only when the directory was configured before.

The example can be run under wine. It needs the directory of the compiler's runtime DLLs, which has
`posix` in its name, and the directory the library was installed in:

```
RUNTIME_DIR=/usr/lib/gcc/x86_64-w64-mingw32/$(ls /usr/lib/gcc/x86_64-w64-mingw32/ | grep posix | head -n1)
WINEPATH="$RUNTIME_DIR;/usr/x86_64-w64-mingw32/lib" wine64 src/example.exe
```

## Using the library

The installed `ecs-cpp.pc` tells the compiler and the linker what a program needs. Build a program with:

```
g++ $(pkg-config --cflags ecs-cpp) main.cpp $(pkg-config --libs ecs-cpp) -o main
```

* The compile flags are the include directory and `-std=c++20 -pthread`. A program that wants a later
  standard puts its own `-std=` after the pkg-config flags.
* The link flags are the library directory, `-lecs-cpp` and `-pthread`.
* An Autoconf project requires the library with `PKG_CHECK_MODULES([LIBECS], [ecs-cpp >= 2.0.0])`.
* If the library is installed outside the default prefix, add `PREFIX/lib/pkgconfig` to `PKG_CONFIG_PATH`.

The shared library is `libecs-cpp.so.2` on Linux and `libecs-cpp-2.dll` on Windows. The number changes
only when a release is not compatible with programs built against the older library. A program keeps
running with a newer library of the same number, and the loader refuses to start it with a library of a
different number. The rule is in [Versioning](NEWS.md#versioning).

### A first program

A container holds entities, components and systems. `ECS` is the process-wide manager that creates
containers and shuts them down. A system derives from `ecs::System`.

```cpp
#include <libecs-cpp/ecs.hpp>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

class HelloSystem : public ecs::System
{
  public:
    HelloSystem() : ecs::System("HelloSystem") {}

    void Update() override
    {
        std::cout << this->Handle << " updated" << std::endl;
    }

    nlohmann::json Export() const override
    {
        return {{"Handle", this->Handle}};
    }
};

int main()
{
    ecs::Container *container = ECS->Container();
    container->System(std::make_unique<HelloSystem>());

    // The container updates its systems on its own thread, about 30 times a second.
    container->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Stops every container and waits until its systems have been shut down.
    ECS->Shutdown();
    return 0;
}
```

The [guide](GUIDE.md) continues from here: it adds components, a timer and a message to a program like
this one. `src/example.cpp` is a second complete program.
