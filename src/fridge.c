#include <stdio.h>
#include <json.h>
#include <SDL.h>

SDL_Renderer *init(void);

int main(void) {
	SDL_Renderer *r;
	json_object *obj;
	
	r = init();

	obj = json_object_from_file("game.json");
	printf("%s\n", json_object_to_json_string(obj));
	json_object_put(obj);

	SDL_RenderClear(r);

	printf("%p\n", r);
	getchar();

	SDL_Quit();

	return 0;
}

SDL_Renderer *init(void) {
    SDL_Renderer *renderer;
    SDL_Window *window;
    int w = 800;
    int h = 600;
    int i;

    i = SDL_Init(SDL_INIT_VIDEO);
    if (i < 0) {
    	return 0;
    }

    window = SDL_CreateWindow("Fridge Filler", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, 0);
    if (!window) {
        fprintf(stderr, "Could not init video: %s\n", SDL_GetError());
        return 0;
    }

    /*SDL_SetWindowIcon(window, temp);*/
    renderer = SDL_CreateRenderer(window, -1, 0);

    return renderer;
}
