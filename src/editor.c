#include <stdio.h>

#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>

#include "engine.h"

#define TICK 40

typedef struct {
	SDL_bool quit;
	SDL_bool start_select;
	SDL_bool select;
	SDL_Point coord;
} editor_action;

typedef struct {
	SDL_bool run;
	SDL_Point start;
	SDL_Rect selection;
	TTF_Font *font;
} editor_state;

void update_state(editor_action const *a, editor_state *s)
{
	s->run = !a->quit;
	if (a->start_select) {
		s->start = a->coord;
	}

	if (a->select) {
		s->selection.x = s->start.x;
		s->selection.y = s->start.y;
		s->selection.w = a->coord.x - s->start.x;
		s->selection.h = a->coord.y - s->start.y;
	}
}

static void handle_event(SDL_Event const *e, editor_action *a)
{
	switch (e->type) {
	case SDL_QUIT:
		a->quit = SDL_TRUE;
		break;
	case SDL_KEYUP:
		switch(e->key.keysym.sym) {
		case SDLK_q:
			a->quit = SDL_TRUE;
			break;
		default:
			break;
		}
		break;
	case SDL_MOUSEBUTTONDOWN:
		a->start_select = SDL_TRUE;
		a->coord.x = e->button.x;
		a->coord.y = e->button.y;
		break;
	case SDL_MOUSEBUTTONUP:
		a->select = SDL_TRUE;
		a->coord.x = e->button.x;
		a->coord.y = e->button.y;
		break;
	default:
		break;
	}
}

void clear_action(editor_action *a)
{
	*a = (editor_action) { quit: SDL_FALSE, start_select: SDL_FALSE, select: SDL_FALSE, coord: { x: 0, y: 0 }};
}

static void render(SDL_Renderer *r, editor_state const *s)
{
	SDL_SetRenderDrawColor(r, 20, 40, 170, 255); /* blue */
	SDL_RenderClear(r);

	SDL_SetRenderDrawColor(r, 200, 20, 7, 255); /* red */
	SDL_RenderFillRect(r, &s->selection);

	render_line(r, "fridge editor", s->font, 0);

	SDL_RenderPresent(r);
}

static SDL_Renderer *init()
{
	int i = SDL_Init(SDL_INIT_VIDEO);
	if (i < 0) {
		fprintf(stderr, "error: could not init video: %s\n", SDL_GetError());
		return 0;
	}

	SDL_Window *window;
	//window = SDL_CreateWindow("Fridge Editor", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_RESIZABLE);
	window = SDL_CreateWindow("Fridge Editor", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, 0);
	if (!window) {
		fprintf(stderr, "Could not init video: %s\n", SDL_GetError());
		return 0;
	}

	SDL_Surface *ico = IMG_Load("../icon.gif");
	if (ico) {
		puts("have icon");
		SDL_SetWindowIcon(window, ico);
		SDL_FreeSurface(ico);
	}

	return SDL_CreateRenderer(window, -1, 0);
}

static SDL_bool init_editor(editor_state *st)
{
	*st = (editor_state) { run: SDL_TRUE, selection: { 0, 0, 0, 0 }, start: { 0, 0 }, font: 0 };

	TTF_Init();
	st->font = TTF_OpenFont("debug_font.ttf", 14);

	return st->font != 0;
}

int main(void)
{
	int have_event;
	SDL_Renderer *r;
	SDL_bool ok;

	r = init();
	if (!r) { return 1; }

	editor_action act;
	editor_state st;
	ok = init_editor(&st);
	if (!ok) { return 1; }

	while (st.run) {
		SDL_Event ev;

		have_event = SDL_PollEvent(&ev);
		if (have_event) {
			clear_action(&act);
			handle_event(&ev, &act);
		}

		update_state(&act, &st);

		render(r, &st);
		SDL_Delay(TICK / 4);
	}

	return 0;
}
