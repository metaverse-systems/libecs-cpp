# Build Environment

The build system and its dependencies are designed around Linux, 
if you want to build on Windows you will need to use Windows Subsystem for Linux.

## Native

* Add Node repository

```
sudo su -
curl -sL https://deb.nodesource.com/setup_13.x | bash -
```

* Install base packages

```
sudo apt install build-essential libtool pkg-config curl git nodejs
```

## Win64

* Add architecture for Wine


```
sudo su -
dpkg --add-architecture i386
apt update
```

* Install Windows dev packages

```
sudo apt install libz-mingw-w64-dev mingw-w64-x86-64-dev binutils-mingw-w64-x86-64 \
g++-mingw-w64-x86-64 gcc-mingw-w64-x86-64 cmake wine wine32 wine64 wixl osslsigncode
```

* Download std::thread implementation for mingw

```
sudo su -
wget -O /usr/x86_64-w64-mingw32/include/mingw.thread.h \
https://raw.githubusercontent.com/meganz/mingw-std-threads/master/mingw.thread.h

wget -O /usr/x86_64-w64-mingw32/include/mingw.invoke.h \
https://raw.githubusercontent.com/meganz/mingw-std-threads/master/mingw.invoke.h

wget -O /usr/x86_64-w64-mingw32/include/mingw.mutex.h \
https://raw.githubusercontent.com/meganz/mingw-std-threads/master/mingw.mutex.h
```

* Install cross-compile instructions for cmake

```
cp mingw64.cmake ~
```
