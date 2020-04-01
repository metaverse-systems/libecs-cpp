# Build environment

Setup build environment as described in ```build-environment.md```

# Install dependencies

## jsoncpp - https://github.com/open-source-parsers/jsoncpp.git

```
export PKG_CONFIG_PATH=/usr/x86_64-w64-mingw32/lib/pkgconfig/
git clone https://github.com/open-source-parsers/jsoncpp.git
cd jsoncpp
mkdir build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE=~/mingw64.cmake -DCMAKE_INSTALL_PREFIX=/usr/x86_64-w64-mingw32 ..
WINEPATH="/usr/lib/gcc/x86_64-w64-mingw32/8.3-posix;/usr/x86_64-w64-mingw32/lib" make
sudo make install
unset PKG_CONFIG_PATH
```

# Install libecs-cpp

## libecs-cpp - https://github.com/metaversesystems/libecs-cpp.git

```
export PKG_CONFIG_PATH=/usr/x86_64-w64-mingw32/lib/pkgconfig/
git clone https://github.com/metaversesystems/libecs-cpp.git
cd libecs-cpp
./autogen.sh
./configure --host=x86_64-w64-mingw32 --prefix=/usr/x86_64-w64-mingw32
make
sudo make install
unset PKG_CONFIG_PATH
```

Test with:

```
wine src/example.exe
```