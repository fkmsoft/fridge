CFLAGS = -Wall -g -std=c99

all: json_test fridge editor

clean:
	$(RM) json_test fridge

json_test: LDLIBS = -ljansson
json_test: json_test.c

fridge: LDLIBS = `sdl2-config --libs` -lSDL2_image -lSDL2_ttf -ljansson
fridge: CFLAGS += `sdl2-config --cflags`
fridge: fridge.c engine.o

editor: LDLIBS = `sdl2-config --libs` -lSDL2_image -lSDL2_ttf
editor: CFLAGS += `sdl2-config --cflags`
editor: editor.c engine.o

engine.o: CFLAGS += `sdl2-config --cflags`
