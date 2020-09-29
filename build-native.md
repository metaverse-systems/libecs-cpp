# Install dependencies

## jsoncpp

```
sudo apt install jsoncpp
```

# Install libecs-cpp

## libecs-cpp - https://github.com/metaverse-systems/libecs-cpp.git

```
git clone https://github.com/metaverse-systems/libecs-cpp.git
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
