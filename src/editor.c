#include <stdio.h>

#include <SDL.h>
#include <SDL_ttf.h>

#include "engine.h"

#define TICK 40

typedef struct {
	SDL_bool quit;
	SDL_bool start_select;
	SDL_bool select;
	SDL_bool move_mouse;
	SDL_Point coord;
} editor_action;

typedef struct {
	SDL_Rect box;
	SDL_Texture *tex;
} tile;

typedef struct {
	SDL_bool run;
	SDL_Point mouse;
	SDL_Rect selection;
	SDL_bool selecting;
	TTF_Font *font;
	tile floor;
	unsigned ticks;
	entity_state player;
} editor_state;

void update_state(editor_action const *a, editor_state *s)
{
	s->run = !a->quit;

	if (a->start_select || a->select || a->move_mouse) {
		s->mouse = a->coord;
	}

	if (a->start_select) {
		s->selection.x = a->coord.x;
		s->selection.y = a->coord.y;
		s->selection.w = 0;
		s->selection.h = 0;
		s->selecting = SDL_TRUE;
	}

	if (a->select || (s->selecting && a->move_mouse)) {
		s->selection.w = a->coord.x - s->selection.x;
		s->selection.h = a->coord.y - s->selection.y;
		s->selecting = !a->select;
	}

	unsigned ticks = SDL_GetTicks();
	if (ticks - s->ticks >= TICK) {
		tick_animation(&s->player);
		s->ticks = ticks;
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
	case SDL_MOUSEMOTION:
		a->move_mouse = SDL_TRUE;
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

static void draw_tile(SDL_Renderer *r, tile const *t, SDL_Point const *pos)
{
	SDL_Rect dest = (SDL_Rect) { pos->x - t->box.x, pos->y - t->box.y, t->box.w, t->box.h };
	SDL_RenderCopy(r, t->tex, 0, &dest);
}

static void draw_tiles(SDL_Renderer *rend, SDL_Rect const *r, tile const *t)
{
	SDL_Point pos, end;

	pos.x = r->x;
	pos.y = r->y;

	end.x = r->x + r->w;
	end.y = r->y + r->h;

	while (pos.x + t->box.w < end.x) {
		draw_tile(rend, t, &pos);
		pos.x += t->box.w;
	}
}

static void render(SDL_Renderer *r, editor_state const *s)
{
	SDL_SetRenderDrawColor(r, 20, 40, 170, 255); /* blue */
	SDL_RenderClear(r);

	SDL_SetRenderDrawColor(r, 200, 20, 7, 255); /* red */
	if (s->selecting) {
		SDL_RenderDrawRect(r, &s->selection);
		draw_tiles(r, &s->selection, &s->floor);
	} else {
		SDL_RenderFillRect(r, &s->selection);
		draw_tiles(r, &s->selection, &s->floor);
	}

	SDL_Rect screen;
	entity_hitbox(&s->player, &screen);
	SDL_Point ft;
	ft = entity_feet(&screen);
	draw_tile(r, &s->floor, &ft);

	screen = (SDL_Rect) { 0, 0, 0, 0 };
	draw_entity(r, &screen, &s->player, 0);

	int l = 0;
	if (s->selecting) { render_line(r, "selecting...", s->font, l++); }

	SDL_RenderPresent(r);
}

static SDL_Renderer *init()
{
	int err = SDL_Init(SDL_INIT_VIDEO);
	if (err < 0) {
		fprintf(stderr, "error: could not init video: %s\n", SDL_GetError());
		return 0;
	}

	SDL_Window *w;
	w = SDL_CreateWindow("Fridge Editor", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_RESIZABLE);
	if (!w) {
		fprintf(stderr, "Could not init video: %s\n", SDL_GetError());
		return 0;
	}

	SDL_Surface *ico = IMG_Load("../icon.gif");
	if (ico) {
		puts("have icon");
		SDL_SetWindowIcon(w, ico);
		SDL_FreeSurface(ico);
	}

	return SDL_CreateRenderer(w, -1, 0);
}

static SDL_Surface *load_tile_info(char const *res, tile *t)
{
	SDL_Surface *r = IMG_Load(res);
	if (!r) { return 0; }

	t->box = (SDL_Rect) { x: 0, y: 30, w: r->w, h: r->h };

	return r;
}

static SDL_bool init_editor(editor_state *st, SDL_Renderer *rend)
{
	*st = (editor_state) { run: SDL_TRUE, selection: { 0, 0, 0, 0 }, font: 0, selecting: SDL_FALSE, mouse: { 0, 0 } };

	TTF_Init();
	st->font = TTF_OpenFont("debug_font.ttf", 14);
	if (!st->font) { return SDL_FALSE; }

	json_t *conf, *character;
	json_error_t err;
	char const *path = "../conf/game.json";
	conf = json_load_file(path, 0, &err);
	if (*err.text != 0) {
		fprintf(stderr, "error: in %s:%d: %s\n", path, err.line, err.text);
		return SDL_FALSE;
	}

	SDL_bool ok;
	SDL_Surface *srf;
	srf = load_tile_info("../level/floor_ceiling.tga", &st->floor);
	if (!srf) { return SDL_FALSE; }

	st->floor.tex = SDL_CreateTextureFromSurface(rend, srf);
	
	st->player.spawn = (SDL_Rect) { x: 100, y: 100 };
	character = json_object_get(json_object_get(conf, "entities"), "man");
	static entity_rule r;
	SDL_Texture *t;
	ok = load_entity_resource(character, "man", &t, rend, &r, "..");
	if (!ok) { return SDL_FALSE; }
	
	init_entity_state(&st->player, &r, t, ST_IDLE);
	st->ticks = SDL_GetTicks();

	return SDL_TRUE;
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
	ok = init_editor(&st, r);
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
