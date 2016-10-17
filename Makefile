BIN = crawler
INCLUDE = -I./3rdparty/gumbo-parser/include \
 -I./3rdparty/gumbo-query/include\
 -I./3rdparty/cppnetlib/include 

LIBPATHS = -L./3rdparty/lib

LIBBOOST = -lboost_system -lboost_filesystem -lboost_thread

LIBS = $(LIBPATHS) -lgq -lgumbo -lcppnetlib-uri -lcppnetlib-client-connections\
 $(LIBBOOST)\
 -lcrypto -lssl -lpthread


all :
	$(CXX) -std=c++11 main.cpp $(INCLUDE) $(LIBS) -o $(BIN)
