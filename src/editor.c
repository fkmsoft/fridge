#include <stdio.h>

#include <SDL.h>
#include <SDL_ttf.h>

#include "engine.h"

#define TICK 40
#define EDITOR_CONF "editor.json"
#define LEVEL_DIR "level"

enum edit_mode { ED_TERRAIN, ED_OBJECTS, ED_DELETE, NMODES };
static char const * const mode_names[] = { "terrain", "objects", "delete" };

typedef struct {
	SDL_bool quit;
	SDL_bool resized;
	SDL_bool start_select;
	SDL_bool select;
	SDL_bool move_mouse;
	SDL_Point coord;
	SDL_Point movement;
	entity_event player;
	SDL_bool set_spawn;
	SDL_bool respawn;
	SDL_bool toggle_terrain;
	SDL_bool toggle_pan;
	SDL_bool next_mode;
} editor_action;

typedef struct {
	SDL_Rect box;
	SDL_Texture *main;
	SDL_Texture *end;
} tile;

typedef struct {
	enum edit_mode md;
	SDL_bool run;
	SDL_Point mouse;
	SDL_Rect selection;
	SDL_bool selecting;
	SDL_bool panning;
	SDL_Point view;
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
	debug_state debug;
	SDL_Texture *scenery;
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
		pos.y += t->box.h / 2;
		while (pos.y < end.y) {
			draw_tile(rend, t, &pos, SDL_FALSE, flip_all);
			pos.y += t->box.h;
		}
	}
}

static void draw_platform(SDL_Renderer *r, SDL_Rect *p, tile const *t)
{
	p->y += t->box.h;

	draw_tiles(r, p, t, SDL_FALSE);
}

static void draw_platforms(SDL_Renderer *r, json_t const *ps, tile const *t, SDL_Rect const *off)
{
	int i;
	json_t *m;
	json_array_foreach(ps, i, m) {
		SDL_Rect p = { x: json_integer_value(json_array_get(m, 0)) - off->x,
		               y: json_integer_value(json_array_get(m, 1)) - off->y,
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
	lwall.y += floor->box.h;
	rwall.y += floor->box.h;
	rceil.y += ceil->box.h;
	loor.y += floor->box.h;

	SDL_SetRenderDrawColor(rend, 0, 0, 0, 255);
	SDL_RenderFillRect(rend, r);

	draw_tiles(rend, &rwall, wall, SDL_FALSE);
	draw_tiles(rend, &lwall, wall, SDL_TRUE);
	draw_tiles(rend, &rceil, ceil, SDL_TRUE);
	draw_tiles(rend, &loor, floor, SDL_FALSE);
}

static void draw_rooms(SDL_Renderer *r, json_t const *ps, tile const *floor, tile const *wall, tile const *ceil, SDL_Rect const *off)
{
	int i;
	json_t *m;
	json_array_foreach(ps, i, m) {
		SDL_Rect p = { x: json_integer_value(json_array_get(m, 0)) - off->x,
		               y: json_integer_value(json_array_get(m, 1)) - off->y,
		               w: json_integer_value(json_array_get(m, 2)),
		               h: json_integer_value(json_array_get(m, 3)) };

		draw_room(r, &p, floor, wall, ceil);
	}
}

static SDL_Texture *redraw_background(editor_state *s)
{
	SDL_Texture *b = SDL_CreateTexture(s->r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, s->cached->dim.w, s->cached->dim.h);
	SDL_SetRenderTarget(s->r, b);
	SDL_SetTextureBlendMode(b, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(s->r, 0, 0, 100, 100); /* transparent */
	SDL_RenderClear(s->r);

	draw_platforms(s->r, s->platforms, &s->platf, &s->cached->dim);
	draw_rooms(s->r, s->rooms, &s->floor, &s->wall, &s->ceil, &s->cached->dim);
	SDL_SetRenderTarget(s->r, 0);

	return b;
}

static level *static_level(json_t *platforms, json_t *rooms)
{
	level *l = malloc(sizeof(level));
	int p = json_array_size(platforms);
	int r = json_array_size(rooms);
	l->dim = (SDL_Rect) { 0, 0, 0, 0 };
	l->background = 0;

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

		if (x < l->dim.x) { l->dim.x = x; }
		if (x + w > l->dim.w) { l->dim.w = x + w; }
		if (y > l->dim.h) { l->dim.h = y; }
		if (y < l->dim.y) { l->dim.y = y; }
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

		if (x < l->dim.x) { l->dim.x = x; }
		if (x + w > l->dim.w) { l->dim.w = x + w; }
		if (y < l->dim.y) { l->dim.y = y; }
		if (y + h > l->dim.h) { l->dim.h = y + h; }
	}

	qsort(l->vertical, l->nvertical, sizeof(line), cmp_lines);
	qsort(l->horizontal, l->nhorizontal, sizeof(line), cmp_lines);

	l->dim.x -= 24;
	l->dim.y -= 24;
	l->dim.w += 24;
	l->dim.h += 24;

	l->dim.w += -l->dim.x;
	l->dim.h += -l->dim.y;

	return l;
}

static void update_terrain(editor_action const *a, editor_state *s)
{
	int new_x = s->floor.box.w * ((a->coord.x - s->view.x) / s->floor.box.w);
	int dx = new_x - s->selection.x;
	if (dx > 0) {
		s->selection.w = dx;
	} else {
		s->selection.x = new_x;
		s->selection.w += -dx;
	}

	int new_y = s->floor.box.h * ((a->coord.y - s->view.y) / s->floor.box.h);
	int dy = new_y - s->selection.y;
	if (dy > 0) {
		s->selection.h = dy;
	} else {
		s->selection.y = new_y;
		s->selection.h += -dy;
	}

	if (a->select) {
		s->selecting = SDL_FALSE;
		if (s->selection.h <= s->platf.box.h) {
			s->selection.h = 0;
			add_rect(s->platforms, &s->selection);
		} else {
			add_rect(s->rooms, &s->selection);
		}

		puts("level changed, updating");
		destroy_level(s->cached);
		free(s->cached);
		s->cached = static_level(s->platforms, s->rooms);
		SDL_DestroyTexture(s->cached->background);
		s->cached->background = redraw_background(s);
	}
}

static void update_state(editor_action const *a, editor_state *s)
{
	s->run = !a->quit;

	if (a->toggle_terrain) {
		s->debug.show_terrain_collision = !s->debug.show_terrain_collision;
	}

	if (a->toggle_pan) {
		s->panning = !s->panning;
	}

	if (a->next_mode) {
		s->md = (s->md + 1) % NMODES;
	}

	if (s->panning && a->move_mouse) {
		s->view.x += a->movement.x;
		s->view.y += a->movement.y;
	}

	if (a->start_select || a->select || a->move_mouse) {
		s->mouse = a->coord;
	}

	if (a->respawn) {
		s->player.pos.x = s->player.spawn.x;
		s->player.pos.y = s->player.spawn.y;
	}

	if (a->set_spawn) {
		s->player.spawn.x = s->player.pos.x;
		s->player.spawn.y = s->player.pos.y;
	}

	if (a->start_select) {
		if (a->coord.x < 0) { s->view.x += a->coord.x; }
		if (a->coord.y < 0) { s->view.y += a->coord.y; }

		s->selection.x = s->floor.box.w * ((a->coord.x - s->view.x) / s->floor.box.w);
		s->selection.y = s->floor.box.h * ((a->coord.y - s->view.y) / s->floor.box.h);
		s->selection.w = 0;
		s->selection.h = 0;
		s->selecting = SDL_TRUE;
	}

	switch (s->md) {
	case ED_TERRAIN:
		if (a->select || (s->selecting && a->move_mouse)) {
			update_terrain(a, s);
		}
		break;
	case ED_OBJECTS:
		break;
	case ED_DELETE:
		break;
	case NMODES:
		fprintf(stderr, "line %d: can never happen\n", __LINE__);
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
		case SDLK_t:
			a->toggle_terrain = SDL_TRUE;
			break;
		case SDLK_s:
			a->set_spawn = SDL_TRUE;
			break;
		case SDLK_m:
			a->next_mode = SDL_TRUE;
			break;
		case SDLK_r:
			a->respawn = SDL_TRUE;
			break;
		default:
			break;
		}
		break;
	case SDL_MOUSEBUTTONDOWN:
		switch (e->button.button) {
		case SDL_BUTTON_LEFT:
			a->start_select = SDL_TRUE;
			break;
		case SDL_BUTTON_RIGHT:
			a->toggle_pan = SDL_TRUE;
			break;
		}
		a->coord.x = e->button.x;
		a->coord.y = e->button.y;
		break;
	case SDL_MOUSEBUTTONUP:
		switch (e->button.button) {
		case SDL_BUTTON_LEFT:
			a->select = SDL_TRUE;
			break;
		case SDL_BUTTON_RIGHT:
			a->toggle_pan = SDL_TRUE;
			break;
		}
		a->coord.x = e->button.x;
		a->coord.y = e->button.y;
		break;
	case SDL_MOUSEMOTION:
		a->move_mouse = SDL_TRUE;
		a->coord.x = e->motion.x;
		a->coord.y = e->motion.y;
		a->movement.x = e->motion.xrel;
		a->movement.y = e->motion.yrel;
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
	*a = (editor_action) {
		quit: SDL_FALSE,
		start_select: SDL_FALSE,
		select: SDL_FALSE,
		coord: { x: 0, y: 0 },
		movement: { x: 0, y: 0 },
		resized: SDL_FALSE,
		move_mouse: SDL_FALSE,
		toggle_terrain: SDL_FALSE,
		set_spawn: SDL_FALSE,
		respawn: SDL_FALSE,
		next_mode: SDL_FALSE,
		toggle_pan: SDL_FALSE };
	clear_order(&a->player);
}

static void render(editor_state const *s)
{
	SDL_SetRenderDrawColor(s->r, 20, 40, 170, 255); /* blue */
	SDL_RenderClear(s->r);

	if (s->scenery) {
		SDL_RenderCopy(s->r, s->scenery, 0, 0);
	}

	SDL_Rect r = { s->view.x + s->cached->dim.x, s->view.y + s->cached->dim.y, s->cached->dim.w, s->cached->dim.h };
	SDL_RenderCopy(s->r, s->cached->background, 0, &r);

	int w, h;
	SDL_GetWindowSize(s->w, &w, &h);
	SDL_Rect screen = { -s->view.x, -s->view.y, w, h };
	SDL_SetRenderDrawColor(s->r, 23, 225, 38, 255); /* lime */
	if (s->md == ED_TERRAIN && s->selecting) {
		SDL_Rect sel = { x: s->view.x + s->selection.x, y: s->view.y + s->selection.y, w: s->selection.w, h: s->selection.h };
		SDL_RenderDrawRect(s->r, &sel);
		if (s->selection.h <= s->platf.box.h) {
			sel.h = 0;
			draw_platform(s->r, &sel, &s->platf);
		} else {
			draw_room(s->r, &sel, &s->floor, &s->wall, &s->ceil);
		}
	}

	draw_entity(s->r, &screen, &s->player, 0);

	if (s->debug.show_terrain_collision) {
		draw_terrain_lines(s->r, s->cached, &screen);
	}

	if (s->font) {
		int l = 0;
		render_line(s->r, mode_names[s->md], s->font, l++);
		render_line(s->r, st_names[s->player.st], s->font, l++);
		if (s->selecting) { render_line(s->r, "selecting...", s->font, l++); }
	}

	SDL_RenderPresent(s->r);
}

static SDL_Renderer *init(SDL_Window **w, char const *icon_file)
{
	int err = SDL_Init(SDL_INIT_VIDEO);
	if (err < 0) {
		fprintf(stderr, "Error: Could not init video: %s\n", SDL_GetError());
		return 0;
	}

	*w = SDL_CreateWindow("Fridge Editor", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_RESIZABLE);
	if (!*w) {
		fprintf(stderr, "Error: Could not create window: %s\n", SDL_GetError());
		return 0;
	}

	SDL_Surface *ico = IMG_Load(icon_file);
	if (ico) {
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

static SDL_bool load_tile(SDL_Renderer *r, json_t *tiles, char const *key, tile *t)
{
	json_t *o;
	char const *file, *path;
	SDL_Surface *srf;

	o = json_object_get(tiles, key);

	file = json_string_value(json_object_get(o, "main"));
	path = set_path("../%s/%s", LEVEL_DIR, file);
	srf = load_tile_info(path, t);
	if (!srf) {
		fprintf(stderr, "Error: No main piece defined for tile `%s'\n", key);
		return SDL_FALSE;
	}

	t->main = SDL_CreateTextureFromSurface(r, srf);
	SDL_FreeSurface(srf);

	file = json_string_value(json_object_get(o, "end"));
	if (!file) {
		fprintf(stderr, "Warning: No end piece defined for tile `%s'\n", key);
		t->end = 0;
		return SDL_TRUE;
	}
	path = set_path("../%s/%s", LEVEL_DIR, file);
	srf = IMG_Load(path);
	t->end = SDL_CreateTextureFromSurface(r, srf);
	SDL_FreeSurface(srf);

	return SDL_TRUE;
}

static SDL_bool init_editor(editor_state *st, SDL_Renderer *rend, SDL_Window *w)
{
	*st = (editor_state) {
		md: ED_TERRAIN,
		run: SDL_TRUE,
		selection: { 0, 0, 0, 0 },
		font: 0,
		selecting: SDL_FALSE,
		mouse: { 0, 0 },
		platforms: json_array(),
		rooms: json_array(),
		r: rend,
		w: w };

	json_t *conf;
	char const *path;
	path = set_path("%s/%s/%s", "..", CONF_DIR, EDITOR_CONF);
	json_error_t err;
	conf = json_load_file(path, 0, &err);
	if (*err.text != 0) {
		fprintf(stderr, "Error at %s:%d: %s\n", path, err.line, err.text);
		return SDL_FALSE;
	}

	TTF_Init();
	char const *file = json_string_value(json_object_get(conf, "font"));
	st->font = TTF_OpenFont(file, 14);
	if (!st->font) {
		fprintf(stderr, "Warning: Could not open font file `%s' (%s)\n" \
				"Warning: There will be no on-screen text\n",
			file, TTF_GetError());
	}

	SDL_Surface *srf;
	file = json_string_value(json_object_get(conf, "scenery"));
	path = set_path("../%s/%s", LEVEL_DIR, file);
	srf = IMG_Load(path);
	if (!srf) {
		fprintf(stderr, "Warning: Could not open scenery `%s'\n", path);
		st->scenery = 0;
	} else {
		st->scenery = SDL_CreateTextureFromSurface(rend, srf);
	}

	SDL_bool ok;
	json_t *til = json_object_get(conf, "tiles");
	ok = load_tile(rend, til, "platform", &st->platf);
	if (!ok) { return SDL_FALSE; }

	ok = load_tile(rend, til, "floor", &st->floor);
	if (!ok) { return SDL_FALSE; }

	ok = load_tile(rend, til, "ceiling", &st->ceil);
	if (!ok) { return SDL_FALSE; }

	ok = load_tile(rend, til, "wall", &st->wall);
	if (!ok) { return SDL_FALSE; }

	SDL_Texture **e_texs;
	entity_rule *e_rules;
	json_t *entities;
	file = json_string_value(json_object_get(conf, "entities"));
	path = set_path("%s/%s/%s", "..", CONF_DIR, file);
	entities = load_entities("..", path, rend, &e_texs, &e_rules);
	if (!entities) { return SDL_FALSE; }

	int pi;
	json_t *character;
	character = json_object_get(entities, "man");
	pi = json_integer_value(json_object_get(character, "index"));

	st->player.spawn = (SDL_Rect) { x: 100, y: 100 };
	init_entity_state(&st->player, &e_rules[pi], e_texs[pi], ST_IDLE);

	SDL_Rect hb;
	SDL_Point ft;
	entity_hitbox(&st->player, &hb);
	ft = entity_feet(&hb);

	SDL_Rect plat = { x: ft.x - hb.w, y: ft.y + 100, w: 2 * hb.w, h: 0 };
	add_rect(st->platforms, &plat);

	st->cached = static_level(st->platforms, st->rooms);
	st->cached->background = redraw_background(st);
	clear_debug(&st->debug);
	json_decref(conf);

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
	if (st->font) { TTF_CloseFont(st->font); }
	destroy_level(st->cached);
	destroy_tile(&st->wall);
	destroy_tile(&st->floor);
	destroy_tile(&st->platf);
	if (st->scenery) { SDL_DestroyTexture(st->scenery); }
	SDL_DestroyRenderer(st->r);
	SDL_DestroyWindow(st->w);
}

int main(void)
{
	int have_event;
	SDL_Renderer *r;
	SDL_Window *w;
	SDL_bool ok;

	r = init(&w, "../icon.gif");
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
