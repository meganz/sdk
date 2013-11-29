TARGET = lsmega
CC = g++
SOURCES= lsmega.cpp
OBJECTS=$(SOURCES:.cpp=.o)
CXXFLAGS=`pkg-config --cflags libmega libcrypto++` -L/usr/include `curl-config --cflags`
LDFLAGS=`pkg-config --libs libmega libcrypto++` `curl-config --libs`

all: $(TARGET)

# objects
.cpp.o:
	$(CXX) -c $(CXXFLAGS) $(INC) $< -o $@

# apps
$(TARGET): $(OBJECTS)
	$(CXX) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LIB)

clean:
	-rm tests
	-rm -f *.o
	-rm -f core
