#include <stdio.h>

#include <SDL.h>
#include <SDL_ttf.h>

#include "engine.h"

#define TICK 40

typedef struct {
	SDL_bool quit;
	SDL_bool resized;
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
	tile platf;
	tile wall;
	tile ceil;
	unsigned ticks;
	entity_state player;
	json_t *platforms;
	json_t *rooms;
	level *cached;
	SDL_Renderer *r;
	SDL_Window *w;
} editor_state;

static void add_rect(json_t *lvl, SDL_Rect const *p)
{
	json_t *a;
	a = json_array();
	json_array_append_new(a, json_integer(p->x));
	json_array_append_new(a, json_integer(p->y));
	json_array_append_new(a, json_integer(p->w));
	json_array_append_new(a, json_integer(p->h));
	json_array_append_new(lvl, a);
}

static void draw_tile(SDL_Renderer *r, tile const *t, SDL_Point const *pos, SDL_bool end, SDL_bool flip)
{
	SDL_Rect dest = (SDL_Rect) { pos->x - (end ? flip ? -t->box.w / 2 : 0 : t->box.x), pos->y - t->box.y, t->box.w / (end ? 2 : 1), t->box.h };
	SDL_RenderCopyEx(r, end ? t->end : t->main, 0, &dest, 0, 0, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

static void draw_tiles(SDL_Renderer *rend, SDL_Rect const *r, tile const *t, SDL_bool flip_all)
{
	SDL_Point pos, end;

	pos.x = r->x - t->box.w;
	pos.y = r->y;

	end.x = r->x + r->w;
	end.y = r->y + r->h;

	if (pos.y == end.y) {
		if (t->end) {
			draw_tile(rend, t, &pos, SDL_TRUE, !flip_all);
		}
		pos.x += t->box.w;
		while (pos.x + t->box.w <= end.x) {
			draw_tile(rend, t, &pos, SDL_FALSE, flip_all);
			pos.x += t->box.w;
		}
		if (t->end) {
			draw_tile(rend, t, &pos, SDL_TRUE, flip_all);
		}
	} else {
		pos.y += t->box.h;
		while (pos.y + t->box.h <= end.y) {
			draw_tile(rend, t, &pos, SDL_FALSE, flip_all);
			pos.y += t->box.h;
		}
	}
}

static void draw_platform(SDL_Renderer *r, SDL_Rect *p, tile const *t)
{
	p->y += 24;

	draw_tiles(r, p, t, SDL_FALSE);
}

static void draw_platforms(SDL_Renderer *r, json_t const *ps, tile const *t)
{
	int i;
	json_t *m;
	json_array_foreach(ps, i, m) {
		SDL_Rect p = { x: json_integer_value(json_array_get(m, 0)),
		               y: json_integer_value(json_array_get(m, 1)),
			       w: json_integer_value(json_array_get(m, 2)),
			       h: 0 };

		draw_platform(r, &p, t);
	}
}

static void draw_room(SDL_Renderer *rend, SDL_Rect const *r, tile const *floor, tile const *wall, tile const *ceil)
{
	SDL_Rect loor, lwall, rwall, rceil;
	loor  = (SDL_Rect) { r->x,        r->y,        r->w, 0    };
	lwall = (SDL_Rect) { r->x,        r->y,        0,    r->h };
	rwall = (SDL_Rect) { r->x + r->w, r->y,        0,    r->h };
	rceil = (SDL_Rect) { r->x,        r->y + r->h, r->w, 0    };

	lwall.x += wall->box.w / 2;
	rwall.x += wall->box.w / 2;
	rceil.y += ceil->box.h / 2;
	loor.y += floor->box.h / 2;

	SDL_SetRenderDrawColor(rend, 0, 0, 0, 255);
	SDL_RenderFillRect(rend, r);

	draw_tiles(rend, &loor, floor, SDL_FALSE);
	draw_tiles(rend, &rceil, ceil, SDL_TRUE);
	draw_tiles(rend, &rwall, wall, SDL_FALSE);
	draw_tiles(rend, &lwall, wall, SDL_TRUE);
}

static void draw_rooms(SDL_Renderer *r, json_t const *ps, tile const *floor, tile const *wall, tile const *ceil)
{
	int i;
	json_t *m;
	json_array_foreach(ps, i, m) {
		SDL_Rect p = { x: json_integer_value(json_array_get(m, 0)),
		               y: json_integer_value(json_array_get(m, 1)),
		               w: json_integer_value(json_array_get(m, 2)),
		               h: json_integer_value(json_array_get(m, 3)) };

		draw_room(r, &p, floor, wall, ceil);
	}
}

static SDL_Texture *redraw_background(editor_state *s)
{
	int w, h;
	SDL_GetWindowSize(s->w, &w, &h);
	SDL_Texture *b = SDL_CreateTexture(s->r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
	SDL_SetRenderTarget(s->r, b);
	SDL_SetRenderDrawColor(s->r, 20, 40, 170, 255); /* blue */
	SDL_RenderClear(s->r);

	draw_platforms(s->r, s->platforms, &s->platf);
	draw_rooms(s->r, s->rooms, &s->floor, &s->wall, &s->ceil);
	SDL_SetRenderTarget(s->r, 0);

	return b;
}

static level *static_level(json_t *platforms, json_t *rooms, editor_state *s)
{
	level *l = malloc(sizeof(level));
	int p = json_array_size(platforms);
	int r = json_array_size(rooms);

	l->horizontal = malloc(sizeof(line) * (p + 2 * r));
	l->vertical = malloc(sizeof(line) * 2 * r);
	l->nhorizontal = 0;
	l->nvertical = 0;

	int i;
	json_t *m;
	int x, y, w, h;
	json_array_foreach(platforms, i, m) {
		x = json_integer_value(json_array_get(m, 0));
		y = json_integer_value(json_array_get(m, 1));
		w = json_integer_value(json_array_get(m, 2));
		h = json_integer_value(json_array_get(m, 3));

		l->horizontal[l->nhorizontal] = (line) { y, x, x + w };
		l->nhorizontal += 1;
	}

	json_array_foreach(rooms, i, m) {
		x = json_integer_value(json_array_get(m, 0));
		y = json_integer_value(json_array_get(m, 1));
		w = json_integer_value(json_array_get(m, 2));
		h = json_integer_value(json_array_get(m, 3));

		l->vertical[l->nvertical + 0] = (line) { x, y, y + h };
		l->vertical[l->nvertical + 1] = (line) { x + w, y, y + h };
		l->nvertical += 2;

		l->horizontal[l->nhorizontal + 0] = (line) { y, x, x + w };
		l->horizontal[l->nhorizontal + 1] = (line) { y + h, x, x + w };
		l->nhorizontal += 2;
	}

	qsort(l->vertical, l->nvertical, sizeof(line), cmp_lines);
	qsort(l->horizontal, l->nhorizontal, sizeof(line), cmp_lines);

	SDL_DestroyTexture(l->background);
	l->background = redraw_background(s);

	return l;
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
			if (s->selection.h <= s->platf.box.h) {
				s->selection.w = s->platf.box.w * (s->selection.w / s->platf.box.w);
				s->selection.h = 0;
				add_rect(s->platforms, &s->selection);
			} else {
				s->selection.w = s->floor.box.w * (s->selection.w / s->floor.box.w);
				s->selection.h = s->wall.box.h * (s->selection.h / s->wall.box.h);
				add_rect(s->rooms, &s->selection);
			}

			puts("level changed, updating");
			destroy_level(s->cached);
			free(s->cached);
			s->cached = static_level(s->platforms, s->rooms, s);
		}
	}

	if (a->resized) {
		puts("window resize, redrawing bg");
		SDL_DestroyTexture(s->cached->background);
		s->cached->background = redraw_background(s);
	}

	unsigned ticks = SDL_GetTicks();
	if (ticks - s->ticks >= TICK) {
		s->ticks = ticks;

		move_log log;
		move_entity(&s->player, &a->player, s->cached, &log);

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
	case SDL_WINDOWEVENT:
		a->resized = e->window.event == SDL_WINDOWEVENT_RESIZED;
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

static void render(editor_state const *s)
{
	SDL_SetRenderDrawColor(s->r, 20, 40, 170, 255); /* blue */
	SDL_RenderClear(s->r);

	draw_background(s->r, s->cached->background, 0);

	SDL_SetRenderDrawColor(s->r, 23, 225, 38, 255); /* lime */
	if (s->selecting) {
		SDL_RenderDrawRect(s->r, &s->selection);
		SDL_Rect roof = { x: s->selection.x, y: s->selection.y, w: s->selection.w, h: 0 };
		if (s->selection.h <= s->platf.box.h) {
			draw_platform(s->r, &roof, &s->platf);
		} else {
			draw_room(s->r, &s->selection, &s->floor, &s->wall, &s->ceil);
		}
	}

	SDL_Rect screen = { 0, 0, 0, 0 };
	draw_entity(s->r, &screen, &s->player, 0);

	draw_terrain_lines(s->r, s->cached, &screen);

	int l = 0;
	render_line(s->r, st_names[s->player.st], s->font, l++);
	if (s->selecting) { render_line(s->r, "selecting...", s->font, l++); }

	SDL_RenderPresent(s->r);
}

static SDL_Renderer *init(SDL_Window **w)
{
	int err = SDL_Init(SDL_INIT_VIDEO);
	if (err < 0) {
		fprintf(stderr, "error: could not init video: %s\n", SDL_GetError());
		return 0;
	}

	*w = SDL_CreateWindow("Fridge Editor", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_RESIZABLE);
	if (!*w) {
		fprintf(stderr, "Could not init video: %s\n", SDL_GetError());
		return 0;
	}

	SDL_Surface *ico = IMG_Load("../icon.gif");
	if (ico) {
		puts("have icon");
		SDL_SetWindowIcon(*w, ico);
		SDL_FreeSurface(ico);
	}

	return SDL_CreateRenderer(*w, -1, 0);
}

static SDL_Surface *load_tile_info(char const *res, tile *t)
{
	SDL_Surface *r = IMG_Load(res);
	if (!r) { return 0; }

	t->box = (SDL_Rect) { x: 0, y: 30, w: r->w, h: r->h };

	return r;
}

static SDL_bool init_editor(editor_state *st, SDL_Renderer *rend, SDL_Window *w)
{
	*st = (editor_state) {
		run: SDL_TRUE,
		selection: { 0, 0, 0, 0 },
		font: 0,
		selecting: SDL_FALSE,
		mouse: { 0, 0 },
		platforms: json_array(),
		rooms: json_array(),
		r: rend,
		w: w };

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
	srf = load_tile_info("../level/floor_ceiling_48x24.tga", &st->platf);
	if (!srf) { return SDL_FALSE; }

	st->platf.main = SDL_CreateTextureFromSurface(rend, srf);
	SDL_FreeSurface(srf);

	srf = IMG_Load("../level/floor_ceiling_end_24x24.tga");
	if (!srf) { return SDL_FALSE; }
	st->platf.end = SDL_CreateTextureFromSurface(rend, srf);
	SDL_FreeSurface(srf);

	srf = load_tile_info("../level/ceiling_48x24.tga", &st->ceil);
	if (!srf) { return SDL_FALSE; }

	st->ceil.main = SDL_CreateTextureFromSurface(rend, srf);
	SDL_FreeSurface(srf);

	st->ceil.end = 0;

	srf = load_tile_info("../level/floor_48x24.tga", &st->floor);
	if (!srf) { return SDL_FALSE; }
	st->floor.main = SDL_CreateTextureFromSurface(rend, srf);
	st->floor.end = 0;
	SDL_FreeSurface(srf);

	srf = load_tile_info("../level/wall_48x48.tga", &st->wall);
	if (!srf) { return SDL_FALSE; }
	st->wall.main = SDL_CreateTextureFromSurface(rend, srf);
	st->wall.end = 0;
	SDL_FreeSurface(srf);
	
	st->player.spawn = (SDL_Rect) { x: 100, y: 100 };
	character = json_object_get(json_object_get(conf, "entities"), "man");
	static entity_rule r;
	SDL_Texture *t;
	ok = load_entity_resource(character, "man", &t, rend, &r, "..");
	if (!ok) { return SDL_FALSE; }
	json_decref(conf);

	init_entity_state(&st->player, &r, t, ST_IDLE);

	SDL_Rect hb;
	SDL_Point ft;
	entity_hitbox(&st->player, &hb);
	ft = entity_feet(&hb);

	SDL_Rect plat = { x: ft.x - hb.w, y: ft.y + 100, w: 2 * hb.w, h: 0 };
	add_rect(st->platforms, &plat);

	st->cached = static_level(st->platforms, st->rooms, st);

	st->ticks = SDL_GetTicks();

	return SDL_TRUE;
}

static void destroy_tile(tile const *t)
{
	SDL_DestroyTexture(t->main);
	if (t->end) {
		SDL_DestroyTexture(t->end);
	}
}

static void destroy_state(editor_state const *st)
{
	json_decref(st->platforms);
	json_decref(st->rooms);
	TTF_CloseFont(st->font);
	destroy_level(st->cached);
	destroy_tile(&st->wall);
	destroy_tile(&st->floor);
	destroy_tile(&st->platf);
	SDL_DestroyRenderer(st->r);
	SDL_DestroyWindow(st->w);
}

int main(void)
{
	int have_event;
	SDL_Renderer *r;
	SDL_Window *w;
	SDL_bool ok;

	r = init(&w);
	if (!r) { return 1; }

	editor_action act;
	editor_state st;
	ok = init_editor(&st, r, w);
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

		render(&st);
		SDL_Delay(TICK / 4);
	}

	destroy_state(&st);
	SDL_Quit();

	return 0;
}
