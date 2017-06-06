CXX = g++
#CXXFLAGS = -O2 -Wall -Werror -Isrc -std=c++11
CXXFLAGS = -O0 -g -Wall -Werror -Isrc -std=c++11
LDFLAGS = -lpthread -ldl

all: bin/server bin/client32 bin/client64 done

test: bin/test done

done:
	# done!

bin/server: bin bin/server64.o bin/client_handler64.o bin/tcp_socket64.o bin/au_stream_socket64.o bin/au_stream_socket_impl64.o bin/au_stream_socket_utils64.o
	$(CXX) bin/server64.o bin/client_handler64.o bin/tcp_socket64.o bin/au_stream_socket64.o bin/au_stream_socket_impl64.o bin/au_stream_socket_utils64.o -o bin/server $(LDFLAGS) -m64

bin/client32: bin bin/client32.o bin/tcp_socket32.o bin/au_stream_socket32.o bin/au_stream_socket_impl32.o bin/au_stream_socket_utils32.o
	$(CXX) bin/client32.o bin/tcp_socket32.o bin/au_stream_socket32.o bin/au_stream_socket_impl32.o bin/au_stream_socket_utils32.o -o bin/client32 $(LDFLAGS) -m32

bin/client64: bin bin/client64.o bin/tcp_socket64.o bin/au_stream_socket64.o bin/au_stream_socket_impl64.o bin/au_stream_socket_utils64.o
	$(CXX) bin/client64.o bin/tcp_socket64.o bin/au_stream_socket64.o bin/au_stream_socket_impl64.o bin/au_stream_socket_utils64.o -o bin/client64 $(LDFLAGS) -m64

bin/test: bin bin/test64.o bin/tcp_socket64.o bin/au_stream_socket64.o bin/au_stream_socket_impl64.o bin/au_stream_socket_utils64.o
	$(CXX) bin/test64.o bin/tcp_socket64.o bin/au_stream_socket64.o bin/au_stream_socket_impl64.o bin/au_stream_socket_utils64.o -o bin/test $(LDFLAGS) -m64

bin/%32.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -m32 -c -MMD -o $@ $<

bin/%64.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -m64 -c -MMD -o $@ $<

bin/test64.o: test/test.cpp
	$(CXX) $(CXXFLAGS) -m64 -c -MMD -o $@ $<

include $(wildcard bin/*.d)
include $(wildcard bin/client/client32.d)
include $(wildcard bin/client/client64.d)

bin:
	mkdir -p bin/client

clean:
	rm -rf bin

.PHONY: clean all done
