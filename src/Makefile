CFLAGS = -Wall -g -ansi

all: json_test fridge

clean:
	$(RM) json_test fridge

json_test: LDLIBS = -ljson-c
json_test: CFLAGS += -I/usr/include/json-c
json_test: json_test.c

fridge: LDLIBS = `sdl2-config --libs` -ljson-c
fridge: CFLAGS += -I/usr/include/json-c
fridge: CFLAGS += `sdl2-config --cflags`
fridge: fridge.c
