TARGET = lsmega
CC = g++
SOURCES= lsmega.cpp
OBJECTS=$(SOURCES:.cpp=.o)
# set -DDEBUG=1 to enable debug output
#DEBUG_FLAG=-DDEBUG=1
DEBUG_FLAG=
CXXFLAGS=$(DEBUG_FLAG) -Wall `pkg-config --cflags libmega libcrypto++` `curl-config --cflags`
LDFLAGS=`pkg-config --libs libmega libcrypto++` `curl-config --libs` -lrt

all: $(TARGET)

# objects
.cpp.o:
	$(CXX) -c $(CXXFLAGS) $(INC) $< -o $@

# apps
$(TARGET): $(OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LIB) $(LDFLAGS) 

clean:
	-rm $(TARGET)
	-rm -f *.o
	-rm -f core
