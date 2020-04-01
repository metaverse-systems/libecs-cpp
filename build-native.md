# Build environment

Setup build environment as described in ```build-environment.md```

# Install dependencies

## jsoncpp - https://github.com/open-source-parsers/jsoncpp.git

```
sudo apt install jsoncpp
```

# Install libecs-cpp

## libecs-cpp - https://github.com/metaversesystems/libecs-cpp.git

```
git clone https://github.com/metaversesystems/libecs-cpp.git
cd libecs-cpp
./autogen.sh
./configure
make
sudo make install
```

Test with:

```
./src/example
```