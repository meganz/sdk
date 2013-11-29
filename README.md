MEGA SDK

### Dependencies
* Crypto++
* cURL

### Building
```
sh autogen.sh
./configure
make
sudo make install
```

### Usage
Take a look at the sample project in ```doc/example``` how to use Mega SDK in your applications.
In order to compile and link your application with Mega SDK library you can use ```pkg-config``` script:
```
g++ $(pkg-config --cflags libmega) -o main.o -c main.cpp
g++ $(pkg-config --libs libmega) -o app main.o
```
