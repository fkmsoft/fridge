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
	entity_event player;
} editor_action;

typedef struct {
	SDL_Rect box;
	SDL_Texture *main;
	SDL_Texture *end;
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
	json_t *platforms;
} editor_state;

static void add_platform(json_t *lvl, SDL_Rect const *p)
{
	json_t *a;
	a = json_array();
	json_array_append_new(a, json_integer(p->x));
	json_array_append_new(a, json_integer(p->y));
	json_array_append_new(a, json_integer(p->x + p->w));
	json_array_append_new(a, json_integer(p->y));
	json_array_append_new(lvl, a);
}

static void static_level(json_t *dyn, level *stat)
{
	if (stat->nlines != json_array_size(dyn)) {
		fprintf(stderr, "no space allocated for static level\n");
		return;
	}

	SDL_Point a, b;

	int i;
	json_t *m;
	json_array_foreach(dyn, i, m) {
		a.x = json_integer_value(json_array_get(m, 0));
		a.y = json_integer_value(json_array_get(m, 1));
		b.x = json_integer_value(json_array_get(m, 2));
		b.y = json_integer_value(json_array_get(m, 3));

		stat->l[i] = (line) { a: a, b: b };
	}
}

static void update_state(editor_action const *a, editor_state *s)
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

		if (a->select) {
			if (s->selection.w < 0) {
				s->selection.x += s->selection.w;
				s->selection.w *= -1;
			}

			if (s->selection.h < 0) {
				s->selection.y = s->selection.h;
				s->selection.h *= -1;
			}

			s->selecting = SDL_FALSE;
			add_platform(s->platforms, &s->selection);
		}
	}

	level lv = { l: 0, background: 0, nlines: json_array_size(s->platforms) };
	lv.l = alloca(sizeof(line) * lv.nlines);
	static_level(s->platforms, &lv);

	unsigned ticks = SDL_GetTicks();
	if (ticks - s->ticks >= TICK) {
		s->ticks = ticks;

		move_log log;
		move_entity(&s->player, &a->player, &lv, &log);

		tick_animation(&s->player);
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
	clear_order(&a->player);
}

static void draw_tile(SDL_Renderer *r, tile const *t, SDL_Point const *pos, SDL_bool end, SDL_bool flip)
{
	SDL_Rect dest = (SDL_Rect) { pos->x - (end ? 0 : t->box.x), pos->y - t->box.y, t->box.w, t->box.h };
	SDL_RenderCopyEx(r, end ? t->end : t->main, 0, &dest, 0, 0, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

static void draw_tiles(SDL_Renderer *rend, SDL_Rect const *r, tile const *t)
{
	SDL_Point pos, end;

	pos.x = r->x - t->box.w;
	pos.y = r->y;

	end.x = r->x + r->w;
	end.y = r->y + r->h;

	draw_tile(rend, t, &pos, SDL_TRUE, SDL_TRUE);
	pos.x += t->box.w;
	while (pos.x + t->box.w < end.x) {
		draw_tile(rend, t, &pos, SDL_FALSE, SDL_FALSE);
		pos.x += t->box.w;
	}
	draw_tile(rend, t, &pos, SDL_TRUE, SDL_FALSE);
}

static void draw_platforms(SDL_Renderer *r, json_t const *ps, tile const *t)
{
	int i;
	json_t *m;
	json_array_foreach(ps, i, m) {
		SDL_Rect p = { x: json_integer_value(json_array_get(m, 0)),
		               y: json_integer_value(json_array_get(m, 1)),
			       h: 0 };
		p.w = json_integer_value(json_array_get(m, 2)) - p.x;

		draw_tiles(r, &p, t);
	}
}

static void render(SDL_Renderer *r, editor_state const *s)
{
	SDL_SetRenderDrawColor(r, 20, 40, 170, 255); /* blue */
	SDL_RenderClear(r);

	draw_platforms(r, s->platforms, &s->floor);

	SDL_SetRenderDrawColor(r, 23, 225, 38, 255); /* lime */
	if (s->selecting) {
		SDL_RenderDrawRect(r, &s->selection);
		draw_tiles(r, &s->selection, &s->floor);
	} else {
		//SDL_RenderFillRect(r, &s->selection);
	}

	SDL_Rect screen = { 0, 0, 0, 0 };
	draw_entity(r, &screen, &s->player, 0);

	level lv = { l: 0, background: 0, nlines: json_array_size(s->platforms) };
	lv.l = alloca(sizeof(line) * lv.nlines);
	static_level(s->platforms, &lv);

	draw_terrain_lines(r, &lv, &screen);

	int l = 0;
	render_line(r, st_names[s->player.st], s->font, l++);
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

	st->platforms = json_array();

	st->floor.main = SDL_CreateTextureFromSurface(rend, srf);
	srf = IMG_Load("../level/floor_ceiling_end.tga");
	st->floor.end = SDL_CreateTextureFromSurface(rend, srf);
	
	st->player.spawn = (SDL_Rect) { x: 100, y: 100 };
	character = json_object_get(json_object_get(conf, "entities"), "man");
	static entity_rule r;
	SDL_Texture *t;
	ok = load_entity_resource(character, "man", &t, rend, &r, "..");
	if (!ok) { return SDL_FALSE; }

	init_entity_state(&st->player, &r, t, ST_IDLE);

	SDL_Rect hb;
	SDL_Point ft;
	entity_hitbox(&st->player, &hb);
	ft = entity_feet(&hb);

	SDL_Rect plat = { x: ft.x - hb.w, y: ft.y + 100, w: ft.x + hb.w, h: 0 };
	add_platform(st->platforms, &plat);

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

		clear_action(&act);
		have_event = SDL_PollEvent(&ev);
		if (have_event) {
			handle_event(&ev, &act);
		}

		unsigned char const *keystate = SDL_GetKeyboardState(0);
		keystate_to_movement(keystate, &act.player);

		update_state(&act, &st);

		render(r, &st);
		SDL_Delay(TICK / 4);
	}

	SDL_Quit();
	json_decref(st.platforms);

	return 0;
}
