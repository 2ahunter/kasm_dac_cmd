objects = UDP_client.o main.o filter.o

CFLAGS = -Wall -Wextra -pedantic -std=gnu17

LDLIBS = -lc -lpthread


# $@ is the target, $^ are the prerequisites
cmd_kasm: $(objects)
	cc -o $@ $^ $(LDLIBS)


main.o: main.c UDP_client.h protocol.h filter.h 

filter.o: filter.c filter.h

UDP_client.o: UDP_client.c UDP_client.h

.PHONY : clean
clean :
	rm -f cmd_kasm $(objects)