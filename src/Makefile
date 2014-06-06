CFLAGS = -Wall -g -std=c99

all: json_test fridge

clean:
	$(RM) json_test fridge

json_test: LDLIBS = -ljansson
json_test: json_test.c

fridge: LDLIBS = `sdl2-config --libs` -lSDL2_image -ljansson
fridge: CFLAGS += `sdl2-config --cflags`
fridge: fridge.c
