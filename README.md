# libecs-cpp - Entity Component System for C++

See ```src/example.cpp``` for a minimal example.

## Build environment setup
  
The build system and its dependencies are designed around Linux,
if you want to build on Windows you will need to use Windows Subsystem for Linux.

* Install base packages

```
sudo apt install build-essential libtool pkg-config curl git jsoncpp cmake
```

* Add Node repository

```
sudo su -
curl -sL https://deb.nodesource.com/setup_13.x | bash -
apt install nodejs
exit
```

### Add Windows support

* Install wine and Windows dev packages

```
sudo su -
dpkg --add-architecture i386
apt update
apt install libz-mingw-w64-dev mingw-w64-x86-64-dev binutils-mingw-w64-x86-64 \
g++-mingw-w64-x86-64 gcc-mingw-w64-x86-64 wine wine32 wine64 wixl osslsigncode
exit
```

* Download std::thread implementation for mingw

```
sudo su -

curl -o /usr/x86_64-w64-mingw32/include/mingw.thread.h \
https://raw.githubusercontent.com/meganz/mingw-std-threads/master/mingw.thread.h

curl -o /usr/x86_64-w64-mingw32/include/mingw.invoke.h \
https://raw.githubusercontent.com/meganz/mingw-std-threads/master/mingw.invoke.h

curl -o  /usr/x86_64-w64-mingw32/include/mingw.mutex.h \
https://raw.githubusercontent.com/meganz/mingw-std-threads/master/mingw.mutex.h

exit
```

then

  * See ```build-native.md``` for Linux/MacOS build instructions.

or

  * See ```build-win64.md``` for Windows cross-compile build instructions.
