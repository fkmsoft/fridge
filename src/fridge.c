#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <jansson.h>

#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>

#define TICK 40
#define MAX_PATH 500

#define MSG_LINES 2

#define ROOTVAR "FRIDGE_ROOT"
#define GAME_CONF "game.json"
#define ASSET_DIR "assets"
#define CONF_DIR  "conf"

#define streq(s1, s2) !strcmp(s1, s2)

enum dir { DIR_LEFT = -1, DIR_RIGHT = 1 };
enum hit { HIT_NONE = 0, HIT_TOP = 1, HIT_LEFT = 1 << 1, HIT_RIGHT = 1 << 2, HIT_BOT = 1 << 3 };
enum mode { MODE_LOGO, MODE_INTRO, MODE_GAME, MODE_EXIT };

enum msg_frequency { MSG_NEVER, MSG_ONCE, MSG_ALWAYS };
static char const * const frq_names[] = { "never", "once", "always" };

enum state { ST_IDLE, ST_WALK, ST_FALL, ST_JUMP, ST_HANG, NSTATES };
static char const * const st_names[] = { "idle", "walk", "fall", "jump", "hang" };

typedef struct {
	SDL_Point a;
	SDL_Point b;
} line;

typedef struct {
	enum msg_frequency when;
	SDL_Point pos;
	struct {
		SDL_Point size;
		SDL_Texture *tex;
	} lines[MSG_LINES];
} message;

typedef struct {
	unsigned n;
	int timeout;
	SDL_Texture *tex;
	message *msgs;
	SDL_Rect box;
	SDL_Rect line;
} msg_info;

typedef struct {
	SDL_Point pos;
	message win;
	message loss;
} finish;

typedef struct {
	SDL_Texture *background;
	SDL_Surface *collision;
	int nlines;
	line *l;
} level;

typedef struct {
	SDL_Renderer *r;
	level level;
	msg_info msg;
	finish finish;
	SDL_Point screen;
} session;

typedef struct {
	unsigned len;
	unsigned *frames;
	unsigned *duration;
	SDL_Rect box;
} animation_rule;

typedef struct {
	SDL_Rect start_dim;
	int walk_dist;
	int jump_dist_x;
	int jump_dist_y;
	int jump_time;
	int fall_dist;
	double a_wide;
	double a_high;
	animation_rule anim[NSTATES];
} entity_rule;

typedef struct {
	int pos;
	int frame;
	int remaining;
} animation_state;

enum jump_type { JUMP_WIDE, JUMP_HIGH, JUMP_HANG };

typedef struct {
	SDL_bool active;
	SDL_Point pos;
	SDL_Rect hitbox;
	SDL_Rect spawn;
	enum dir dir;
	enum state st;
	int jump_timeout;
	enum jump_type jump_type;
	int fall_time;
	animation_state anim;
	entity_rule const *rule;
	SDL_Texture *tex;
} entity_state;

typedef struct {
	unsigned n;
	entity_state *e;
} group;

enum group { GROUP_OBJECTS, GROUP_ENEMIES, NGROUPS };

typedef struct {
	SDL_bool active;
	SDL_bool frames;
	SDL_bool hitboxes;
	SDL_bool pause;
	SDL_bool show_terrain_collision;
	SDL_Texture *terrain_collision;
	SDL_bool message_positions;
} debug_state;

typedef struct {
	int need_to_collect;
	entity_state player;
	entity_state logo;
	entity_state intro;
	group entities[NGROUPS];
	message const *msg;
	unsigned msg_timeout;
	enum mode run;
	debug_state debug;
} game_state;

typedef struct {
	SDL_bool walk;
	SDL_bool move_left;
	SDL_bool move_right;
	SDL_bool move_jump;
} entity_event;

typedef struct {
	int walked;
	int jumped;
	int fallen;
	SDL_bool turned;
	SDL_bool hang;
} move_log;

typedef struct {
	entity_event player;
	SDL_bool toggle_pause;
	SDL_bool toggle_debug;
	SDL_bool toggle_terrain;
	SDL_bool reload_conf;
	SDL_bool exit;
	SDL_bool keyboard;
	SDL_bool reset;
} game_event;

/* high level init */
static SDL_bool init_game(session *s, game_state *g, char const *root);
static SDL_bool load_config(session *s, game_state *gs, json_t *game, char const *root);
static void load_intro(entity_state *intro, session const *s, json_t *o, char const *k, entity_rule const *e_rules, SDL_Texture **e_texs);
void init_group(group *g, json_t const *game, char const *key, SDL_Texture **e_texs, entity_rule const *e_rules, enum state st);
static void init_entity_state(entity_state *es, entity_rule const *er, SDL_Texture *t, enum state st);

/* high level game */
static void process_event(SDL_Event const *ev, game_event *r);
static void process_keystate(unsigned char const *ks, game_event *r);
static void update_gamestate(session *s, game_state *gs, game_event const *ev);
static void tick_animation(entity_state *as);
static void set_group_state(group *g, enum state st);
static SDL_Point entity_vector_move(entity_state *e, SDL_Point const *v, level const *terrain, SDL_bool grav);
static int entity_walk(entity_state *e, level const *terrain);
static int entity_jump(entity_state *e, level const *terrain);
static void move_entity(entity_state *e, entity_event const *ev, level const *lvl);
static void enemy_movement(level const *terrain, group *nmi);
static void load_state(entity_state *es);
static void render(session const *s, game_state const *gs);
static void clear_game(game_state *gs);
static void clear_event(game_event *ev);
static void clear_debug(debug_state *d);

/* collisions */
static enum hit collides_with_terrain(SDL_Rect const *r, level const *lev);
static SDL_bool hang_hit(enum hit h);
static int kick_entity(entity_state *e, enum hit h);
static SDL_bool stands_on_terrain(SDL_Rect const *r, level const *t);
static SDL_bool in_rect(SDL_Point const *p, SDL_Rect const *r);
static SDL_bool have_collision(SDL_Rect const *r1, SDL_Rect const *r2);
static enum hit intersects(line const *l, SDL_Rect const *r);
static void entity_hitbox(entity_state const *s, SDL_Rect *box);

/* low level interactions */
static char const *get_asset(json_t *a, char const *k);
static SDL_bool get_int_field(json_t *o, char const *n, char const *s, int *r);
static SDL_bool get_float_field(json_t *o, char const *n, char const *s, double *r);
static SDL_bool load_finish(session *s, json_t *game, TTF_Font *font, int fontsize);
static SDL_bool load_messages(session *s, json_t *game, TTF_Font *font, int fontsize, char const *root);
static int load_collisions(level *level, json_t const *o);
static SDL_Texture *load_texture(SDL_Renderer *r, char const *file);
static SDL_Texture *load_asset_tex(json_t *a, char const *d, SDL_Renderer *r, char const *k);
static SDL_Surface *load_asset_surf(json_t *a, char const *d, char const *k);
static SDL_bool load_entity_resource(json_t *src, char const *n, SDL_Texture **t, SDL_Renderer *r, entity_rule *er, char const *root);
static void render_message(message *ms, SDL_Renderer *r, TTF_Font *font, json_t *m, unsigned offset);
static void load_anim(json_t *src, char const *name, char const *key, animation_rule *a);
static void draw_background(SDL_Renderer *r, SDL_Texture *bg, SDL_Rect const *screen);
static void draw_terrain_lines(SDL_Renderer *r, level const *lev, SDL_Rect const *screen);
static void draw_message_boxes(SDL_Renderer *r, msg_info const *msgs, SDL_Rect const *screen);
static void draw_message(SDL_Renderer *r, SDL_Texture *t, message const *m, SDL_Rect const *box, SDL_Rect const *line);
static void draw_entity(SDL_Renderer *r, SDL_Rect const *scr, entity_state const *s, debug_state const *debug);
static unsigned getpixel(SDL_Surface const *s, int x, int y);
static char const *set_path(char const *fmt, ...);
static void print_hit(enum hit h);

int main(void) {
	SDL_bool ok;
	char const *root = getenv(ROOTVAR);
	if (!root || !*root) {
		fprintf(stderr, "error: environment undefined\n");
		fprintf(stderr, "set %s to the installation directory of Fridge Filler\n", ROOTVAR);
		return 1;
	}

	session s;
	game_state gs;
	ok = init_game(&s, &gs, root);
	if (!ok) { return 1; }

	game_event ge;
	clear_event(&ge);

	unsigned ticks;
	unsigned old_ticks = SDL_GetTicks();

	int have_ev;
	SDL_Event event;
	while (gs.run != MODE_EXIT) {
		have_ev = SDL_PollEvent(&event);
		if (have_ev) {
			process_event(&event, &ge);
		}

		unsigned char const *keystate = SDL_GetKeyboardState(0);
		process_keystate(keystate, &ge);

		ticks = SDL_GetTicks();
		if (ticks - old_ticks >= TICK) {
			update_gamestate(&s, &gs, &ge);
			clear_event(&ge);
			/*
			printf("%d\n", ticks - old_ticks);
			*/
			old_ticks = ticks;
		}

		render(&s, &gs);
		SDL_Delay(TICK / 4);
	}

	SDL_Quit();

	return 0;
}

/* high level init */
static SDL_bool init_game(session *s, game_state *gs, char const *root)
{
	SDL_Window *window;
	int i;
	json_t *game, *res;

	char const *path;
	path = set_path("%s/%s/%s", root, CONF_DIR, GAME_CONF);
	json_error_t err;
	game = json_load_file(path, 0, &err);
	if (*err.text != 0) {
		fprintf(stderr, "error: in %s:%d: %s\n", path, err.line, err.text);
		return SDL_FALSE;
	}

	i = TTF_Init();
	if (i < 0) {
		fprintf(stderr, "error: could not init font library: %s\n", TTF_GetError());
		return SDL_FALSE;
	}

	res = json_object_get(game, "resolution");
	s->screen.x = json_integer_value(json_array_get(res, 0));
	s->screen.y = json_integer_value(json_array_get(res, 1));

	i = SDL_Init(SDL_INIT_VIDEO);
	if (i < 0) {
		fprintf(stderr, "error: could not init video: %s\n", SDL_GetError());
		return SDL_FALSE;
	}

	window = SDL_CreateWindow("Fridge Filler", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, s->screen.x, s->screen.y, 0);
	if (!window) {
		fprintf(stderr, "Could not init video: %s\n", SDL_GetError());
		return SDL_FALSE;
	}

	path = set_path("%s/%s", root, "icon.gif");
	SDL_Surface *ico = IMG_Load(path);
	if (ico) {
		puts("have icon");
		SDL_SetWindowIcon(window, ico);
		SDL_FreeSurface(ico);
	}

	s->r = SDL_CreateRenderer(window, -1, 0);

	SDL_bool ok = load_config(s, gs, game, root);
	if (!ok) { return SDL_FALSE; }

	gs->run = gs->logo.active ? MODE_LOGO : gs->intro.active ? MODE_INTRO : MODE_GAME;
	clear_game(gs);

	return SDL_TRUE;
}

static SDL_bool load_config(session *s, game_state *gs, json_t *game, char const *root)
{
	json_t *player, *spawn, *entities, *fnt, *level;
	char const *path;

	/* load config */
	fnt = json_object_get(game, "font");
	path = set_path("%s/%s/%s", root, ASSET_DIR, json_string_value(json_object_get(fnt, "resource")));
	int fnt_siz = json_integer_value(json_object_get(fnt, "size"));
	TTF_Font *font = TTF_OpenFont(path, fnt_siz);
	if (!font) {
		fprintf(stderr, "error: could not load font %s: %s\n", path, TTF_GetError());
		return SDL_FALSE;
	}

	SDL_bool ok;
	ok = load_finish(s, game, font, fnt_siz);
	if (!ok) { return SDL_FALSE; }

	ok = load_messages(s, game, font, fnt_siz, root);
	if (!ok) { return SDL_FALSE; }

	level = json_object_get(game, "level");
	if (!ok) { return SDL_FALSE; }

	s->level.background = load_asset_tex(level, root, s->r, "resource");
	if (!s->level.background) { return SDL_FALSE; }

	s->level.collision = load_asset_surf(level, root, "collision-map");
	if (!s->level.collision) { return SDL_FALSE; }

	load_collisions(&s->level, level);

	entity_rule *e_rules;
	SDL_Texture **e_texs;
	entities = json_object_get(game, "entities");
	if (!entities) {
		fprintf(stderr, "error: no entities defined, need player\n");
		return SDL_FALSE;
	}

	int k;
	k = json_object_size(entities);
	e_texs = malloc(sizeof(SDL_Texture *) * k);
	e_rules = malloc(sizeof(entity_rule) * k);

	int i = 0;
	char const *name;
	json_t *o;
	json_object_foreach(entities, name, o) {

		ok = load_entity_resource(o, name, &e_texs[i], s->r, &e_rules[i], root);
		if (!ok) {
			return SDL_FALSE;
		}

		/* save the index in the buffer, so the items in groups can
		 * link directly to their rules and textures later: */
		json_object_set_new(o, "index", json_integer(i));
		i += 1;
	}

	init_group(&gs->entities[GROUP_OBJECTS], game, "objects", e_texs, e_rules, ST_IDLE);
	init_group(&gs->entities[GROUP_ENEMIES], game, "enemies", e_texs, e_rules, ST_WALK);

	int pi;
	player = json_object_get(entities, "player");
	if (!player) {
		fprintf(stderr, "error: no player entity defined\n");
		return SDL_FALSE;
	}
	pi = json_integer_value(json_object_get(player, "index"));

	spawn = json_object_get(game, "spawn");
	if (!spawn) {
		fprintf(stderr, "error: no spawn coordinates defined\n");
		return SDL_FALSE;
	}

	gs->player.spawn.x = json_integer_value(json_array_get(spawn, 0));
	gs->player.spawn.y = json_integer_value(json_array_get(spawn, 1));
	init_entity_state(&gs->player, &e_rules[pi], e_texs[pi], ST_IDLE);

	load_intro(&gs->logo, s, entities, "logo", e_rules, e_texs);
	load_intro(&gs->intro, s, entities, "intro", e_rules, e_texs);

	/* all texture pointers are copied by value, no need to hold onto the
	 * e_texs buffer */
	free(e_texs);

	/* init game_state flags */
	gs->debug.terrain_collision = SDL_CreateTextureFromSurface(s->r, s->level.collision);
	SDL_SetTextureAlphaMod(gs->debug.terrain_collision, 100);

	json_decref(game);

	return SDL_TRUE;
}

static void load_intro(entity_state *intro, session const *s, json_t *o, char const *k, entity_rule const *e_rules, SDL_Texture **e_texs)
{
	json_t *io;
	io = json_object_get(o, k);
	if (!io) {
		fprintf(stderr, "warning: no intro found for `%s'\n", k);
		intro->active = SDL_FALSE;
	} else {
		int i = json_integer_value(json_object_get(io, "index"));
		// TODO
		intro->spawn.w = 640;
		intro->spawn.h = 480;
		intro->spawn.x = (s->screen.x - 640) / 2;
		intro->spawn.y = (s->screen.y - 480) / 2;
		init_entity_state(intro, &e_rules[i], e_texs[i], ST_IDLE);
	}
}

void init_group(group *g, json_t const *game, char const *key, SDL_Texture **e_texs, entity_rule const *e_rules, enum state st)
{
	json_t *objs, *entities;
	entity_state *a;

	entities = json_object_get(game, "entities");
	objs = json_object_get(game, key);
	if (!objs) {
		g->n = 0;
		g->e = 0;
		return;
	}

	int i, k;
	i = 0;
	k = 0;
	json_t *o;
	char const *name;
	json_object_foreach(objs, name, o) {
		k += json_array_size(o);
	}

	g->n = k;
	a = malloc(sizeof(entity_state) * k);
	g->e = a;

	i = 0;
	json_t *spawn;
	json_object_foreach(objs, name, o) {
		int ei, j;
		json_t *entity;
		entity = json_object_get(entities, name);
		ei = json_integer_value(json_object_get(entity, "index"));
		json_array_foreach(o, j, spawn) {
			a[i].spawn.x = json_integer_value(json_array_get(spawn, 0));
			a[i].spawn.y = json_integer_value(json_array_get(spawn, 1));
			init_entity_state(&a[i], &e_rules[ei], e_texs[ei], st);
			i += 1;
		}
	}
}

static void init_entity_state(entity_state *es, entity_rule const *er, SDL_Texture *t, enum state st)
{
	if (!er) {
		er = es->rule;
	} else {
		es->rule = er;
		es->tex = t;
	}

	es->active = SDL_TRUE;
	es->dir = DIR_LEFT;
	es->st = st;
	load_state(es);
	es->pos.x = es->spawn.x;
	es->pos.y = es->spawn.y;
	es->spawn.w = er->start_dim.w;
	es->spawn.h = er->start_dim.h;
	es->jump_timeout = 0;
	es->fall_time = 0;
}

/* high level game */
static void process_event(SDL_Event const *ev, game_event *r)
{
	int key = ev->key.keysym.sym;

	switch (ev->type) {
	case SDL_QUIT:
		r->exit = SDL_TRUE;
		break;
	case SDL_KEYUP:
		r->keyboard = SDL_TRUE;
		switch(key) {
		case SDLK_q:
			r->exit = SDL_TRUE;
			break;
		case SDLK_b:
			r->toggle_terrain = SDL_TRUE;
			break;
		case SDLK_u:
			r->reload_conf = SDL_TRUE;
			break;
		case SDLK_p:
			r->toggle_pause = SDL_TRUE;
			break;
		case SDLK_SPACE:
			r->player.move_jump = SDL_TRUE;
			break;
		case SDLK_r:
			r->reset = SDL_TRUE;
			break;
		case SDLK_d:
			r->toggle_debug = SDL_TRUE;
			break;
		}
		break;
	}
}

static void process_keystate(unsigned char const *ks, game_event *r)
{
	if (ks[SDL_SCANCODE_LEFT ]) { r->player.move_left  = SDL_TRUE; r->player.walk = SDL_TRUE; }
	if (ks[SDL_SCANCODE_RIGHT]) { r->player.move_right = SDL_TRUE; r->player.walk = SDL_TRUE; }
}

static void update_gamestate(session *s, game_state *gs, game_event const *ev)
{
	if (gs->run == MODE_LOGO ) {

		tick_animation(&gs->logo );

		if (gs->logo.anim.pos == gs->logo.rule->anim[ST_IDLE].len - 1 ||
		    ev->keyboard) {
			gs->run = MODE_INTRO;
		}
	}

	if (gs->run == MODE_INTRO) {

		tick_animation(&gs->intro);

		if (ev->keyboard) {
			gs->run = MODE_GAME;
		}
	}

	if (gs->run != MODE_GAME) {
		return;
	}

	if (ev->toggle_debug) {
		gs->debug.active = !gs->debug.active;
		set_group_state(&gs->entities[GROUP_ENEMIES], gs->debug.pause && gs->debug.active ? ST_IDLE : ST_WALK);
	}

	if (ev->reload_conf && gs->debug.active) {
		char const *r, *p;
		r = getenv(ROOTVAR);
		if (r && *r) {
			p = set_path("%s/%s/%s", r, CONF_DIR, GAME_CONF);
			json_t *g;
			json_error_t e;
			g = json_load_file(p, 0, &e);
			if (*e.text != 0) {
				fprintf(stderr, "error: in %s:%d: %s\n", p, e.line, e.text);
			} else {
				SDL_Point ps = gs->player.pos;
				enum dir dr = gs->player.dir;
				fprintf(stderr, "info: re-loading config\n");
				load_config(s, gs, g, r);
				gs->player.pos = ps;
				gs->player.dir = dr;
			}
		}
	}

	if (ev->toggle_pause && gs->debug.active) {
		gs->debug.pause = !gs->debug.pause;
		set_group_state(&gs->entities[GROUP_ENEMIES], gs->debug.pause ? ST_IDLE : ST_WALK);
	}

	if (ev->toggle_terrain && gs->debug.active) {
		gs->debug.show_terrain_collision = !gs->debug.show_terrain_collision;
	}

	tick_animation(&gs->player);
	int i;
	enum group g;
	for (g = 0; g < NGROUPS; g++) {
		for (i = 0; i < gs->entities[g].n; i++) {
			if (gs->entities[g].e[i].active) {
				tick_animation(&gs->entities[g].e[i]);
			}
		}
	}

	if (!gs->debug.active || !gs->debug.pause) {
		enemy_movement(&s->level, &gs->entities[GROUP_ENEMIES]);
	}

	enum state old_state = gs->player.st;
	move_entity(&gs->player, &ev->player, &s->level);

	if (old_state != gs->player.st) {
		printf("%s -> %s\n", st_names[old_state], st_names[gs->player.st]);
		load_state(&gs->player);
	}

	if (ev->reset) {
		init_entity_state(&gs->player, 0, 0, ST_IDLE);
	}

	if (gs->msg_timeout > 0) {
		gs->msg_timeout -= 1;
	} else {
		gs->msg = 0;
	}
	SDL_Rect r;
	entity_hitbox(&gs->player, &r);
	for (i = 0; i < s->msg.n; i++) {
		if (s->msg.msgs[i].when == MSG_NEVER) {
			continue;
		}

		if (in_rect(&s->msg.msgs[i].pos, &r)) {
			gs->msg = &s->msg.msgs[i];
			gs->msg_timeout = s->msg.timeout;
			if (s->msg.msgs[i].when == MSG_ONCE) {
				s->msg.msgs[i].when = MSG_NEVER;
			}
		}
	}

	for (g = 0; g < NGROUPS; g++) {
		for (i = 0; i < gs->entities[g].n; i++) {
			if (!gs->entities[g].e[i].active) {
				continue;
			}

			SDL_Rect hb;
			entity_hitbox(&gs->entities[g].e[i], &hb);
			if (have_collision(&r, &hb)) {
				switch (g) {
				case GROUP_OBJECTS:
					gs->entities[g].e[i].active = SDL_FALSE;
					gs->need_to_collect -= 1;
					break;
				case GROUP_ENEMIES:
					init_entity_state(&gs->player, 0, 0, ST_IDLE);
					break;
				case NGROUPS:
					fprintf(stderr,"line %d: can never happen\n", __LINE__);
				}
			}
		}
	}

	entity_hitbox(&gs->player, &r);
	if (in_rect(&s->finish.pos,  &r)) {
		if (gs->need_to_collect == 0) {
			gs->msg = &s->finish.win;
		} else {
			gs->msg = &s->finish.loss;
		}
	}

	if (ev->exit) {
		gs->run = MODE_EXIT;
	}
}

static void tick_animation(entity_state *es)
{
	animation_state *as = &es->anim;
	animation_rule ar = es->rule->anim[es->st];
	as->remaining -= 1;
	int i;
	if (as->remaining < 0) {
		i = (as->pos + 1) % ar.len;
		as->pos = i;
		as->frame = ar.frames[i];
		as->remaining = ar.duration[i];
	}
}

static void set_group_state(group *g, enum state st)
{
	int i;
	for (i = 0; i < g->n; i++) {
		g->e[i].st = st;
		load_state(&g->e[i]);
	}
}

static SDL_Point entity_vector_move(entity_state *e, SDL_Point const *v, level const *terrain, SDL_bool grav)
{
	SDL_Rect r;
	entity_hitbox(e, &r);

	int dirx = v->x < 0 ? -1 : 1;
	int diry = v->y < 0 ? -1 : 1;

	int vx = v->x < 0 ? -v->x : v->x;
	int vy = v->y < 0 ? -v->y : v->y;

	int v_max = vx > vy ? vx : vy;
	int i;
	SDL_Point out = { x: 0, y: 0 };
	enum hit h;

	for (i = 1; i < v_max + 1; i++) {
		int dx = dirx * (i * vx - (i - 1) * vx) / v_max;
		int dy = diry * (i * vy - (i - 1) * vy) / v_max;
		r.x += dx;
		r.y += dy;
		h = collides_with_terrain(&r, terrain);
		if (h != HIT_NONE) { break; }
		if (grav && !stands_on_terrain(&r, terrain)) { break; }
		out.x += dx;
		out.y += dy;
	}

	e->pos.x += out.x;
	e->pos.y += out.y;

	return out;
}

static int entity_walk(entity_state *e, level const *terrain)
{
	SDL_Point v = { x: e->dir * e->rule->walk_dist, y: 0 };
	SDL_Point r = entity_vector_move(e, &v, terrain, SDL_TRUE);
	return r.x < 0 ? -r.x : r.x;
}

static int entity_start_jump(entity_state *e, level const *terrain, enum jump_type t)
{
	e->jump_timeout = e->rule->jump_time;
	e->jump_type = t;

	return entity_jump(e, terrain);
}

static int entity_jump(entity_state *e, level const *terrain)
{
	if (e->jump_timeout == 0) { return 0; }

	entity_rule const *r = e->rule;
	SDL_Point v = { x: e->jump_type == JUMP_WIDE ? e->dir * r->jump_dist_x : 0,
	                y: r->jump_dist_y + e->jump_timeout }; // +
			   //e->jump_timeout * e->jump_type == JUMP_WIDE ? r->a_wide : r->a_high };
	v.y *= -1;
	SDL_Point w = entity_vector_move(e, &v, terrain, SDL_FALSE);
	if (w.y != v.y) {
		printf("cancel: %d != %d\n", w.y, v.y);
		e->jump_timeout = 0;
	} else {
		e->jump_timeout -= 1;
	}
	return -w.y;
}

static int entity_fall(entity_state *e, level const *terrain, SDL_bool walk)
{
	e->fall_time += 1;
	SDL_Point v = { x: walk ? e->dir * e->rule->walk_dist : 0, y: e->rule->fall_dist + e->fall_time };
	SDL_Point w = entity_vector_move(e, &v, terrain, SDL_FALSE);
	if (v.y != w.y) { e->fall_time = 0; }
	return w.y;
}

static void move_entity(entity_state *e, entity_event const *ev, level const *lvl)
{
	enum state st_begin = e->st;
	move_log mlog = { walked: 0, jumped: 0, fallen: 0, turned: SDL_FALSE, hang: SDL_FALSE };

	hang_hit(HIT_NONE);
	kick_entity(e, HIT_NONE);

	if (ev->move_jump) { puts("jump"); }
	switch (st_begin) {
	case ST_IDLE:
	case ST_WALK:
		e->dir = ev->move_left ? DIR_LEFT : ev->move_right ? DIR_RIGHT : e->dir;
		if (ev->move_jump) {
			mlog.jumped = entity_start_jump(e, lvl, ev->walk ? JUMP_WIDE : JUMP_HIGH);
		} else if (ev->walk) {
			mlog.walked = entity_walk(e, lvl);
		}
		break;
	case ST_HANG:
		break;
	case ST_JUMP:
		e->dir = ev->move_left ? DIR_LEFT : ev->move_right ? DIR_RIGHT : e->dir;
		mlog.jumped = entity_jump(e, lvl);
		break;
	case ST_FALL:
		e->dir = ev->move_left ? DIR_LEFT : ev->move_right ? DIR_RIGHT : e->dir;
		mlog.fallen = entity_fall(e, lvl, ev->walk);
		break;
	case NSTATES:
		break;
	}

	SDL_Rect h;
	entity_hitbox(e, &h);
	e->st = mlog.jumped > 0 ? ST_JUMP : stands_on_terrain(&h, lvl) ? mlog.walked > 0 ? ST_WALK : ST_IDLE : ST_FALL;
}

static void enemy_movement(level const *terrain, group *nmi)
{
	int i, k;
	for (i = 0; i < nmi->n; i++) {
		k = entity_walk(&nmi->e[i], terrain);
		if (k < 1) {
			nmi->e[i].dir *= -1;
			entity_walk(&nmi->e[i], terrain);
		}
	}
}

static void load_state(entity_state *es)
{
	animation_rule ar = es->rule->anim[es->st];
	es->anim.pos = 0;
	es->anim.frame = ar.frames[0];
	es->anim.remaining = ar.duration[0];
	es->hitbox.x = ar.box.x;
	es->hitbox.y = ar.box.y;
	es->hitbox.w = ar.box.w;
	es->hitbox.h = ar.box.h;
}

static void render(session const *s, game_state const *gs)
{
	int i;
	SDL_RenderClear(s->r);

	//SDL_Rect screen = { x: gs->player.pos.x, y: gs->player.pos.y, w: s->screen.x, h: s->screen.y };
	SDL_Rect screen = { x: gs->player.pos.x - (s->screen.x - gs->player.spawn.w) / 2,
	                    y: gs->player.pos.y - (s->screen.y - gs->player.spawn.h) / 2,
			    w: s->screen.x, h: s->screen.y };

	switch (gs->run) {
	case MODE_LOGO:
		screen.x = screen.y = 0;
		draw_entity(s->r, &screen, &gs->logo, 0);
		break;
	case MODE_INTRO:
		screen.x = screen.y = 0;
		draw_entity(s->r, &screen, &gs->intro, 0);
		break;
	case MODE_GAME:
		draw_background(s->r, s->level.background, &screen);
		if (gs->debug.active && gs->debug.show_terrain_collision) {
			draw_background(s->r, gs->debug.terrain_collision, &screen);
			draw_terrain_lines(s->r, &s->level, &screen);
		}
		int g;
		for (g = 0; g < NGROUPS; g++) {
			for (i = 0; i < gs->entities[g].n; i++) {
				if (gs->entities[g].e[i].active) {
					draw_entity(s->r, &screen, &gs->entities[g].e[i], &gs->debug);
				}
			}
		}

		if (gs->debug.active && gs->debug.message_positions) {
			draw_message_boxes(s->r, &s->msg, &screen);
		}

		draw_entity(s->r, &screen, &gs->player, &gs->debug);

		if (gs->msg) {
			draw_message(s->r, s->msg.tex, gs->msg, &s->msg.box, &s->msg.line);
		}
		break;
	case MODE_EXIT:
		puts("bye");
	}

	SDL_RenderPresent(s->r);
}

static void clear_game(game_state *gs)
{
	gs->msg = 0;

	gs->need_to_collect = gs->entities[GROUP_OBJECTS].n;
	clear_debug(&gs->debug);
}

static void clear_event(game_event *ev)
{
	ev->player.move_left = SDL_FALSE;
	ev->player.move_right = SDL_FALSE;
	ev->player.move_jump = SDL_FALSE;
	ev->player.walk = SDL_FALSE;
	ev->exit = SDL_FALSE;
	ev->toggle_debug = SDL_FALSE;
	ev->toggle_pause = SDL_FALSE;
	ev->toggle_terrain = SDL_FALSE;
	ev->reload_conf = SDL_FALSE;
	ev->keyboard = SDL_FALSE;
	ev->reset =SDL_FALSE;
}

static void clear_debug(debug_state *d)
{
	d->active = SDL_FALSE;
	d->pause = SDL_FALSE;
	d->frames = SDL_TRUE;
	d->hitboxes = SDL_TRUE;
	d->show_terrain_collision = SDL_FALSE;
	d->message_positions = SDL_TRUE;
}

/* collisions */
static enum hit collides_with_terrain(SDL_Rect const *r, level const *t)
{
	int x = r->x;
	int y = r->y;
	int h = r->h - 1;

	enum hit a = HIT_NONE;

	SDL_Surface const *m = t->collision;
	a |= getpixel(m, x,        y    ) == 0 ? (HIT_TOP | HIT_LEFT ) : 0;
	a |= getpixel(m, x + r->w, y    ) == 0 ? (HIT_TOP | HIT_RIGHT) : 0;
	a |= getpixel(m, x,        y + h) == 0 ? (HIT_BOT | HIT_LEFT ) : 0;
	a |= getpixel(m, x + r->w, y + h) == 0 ? (HIT_BOT | HIT_RIGHT) : 0;

	SDL_Rect top = { x: r->x, y: r->y, w: r->w, h: r->h / 2 };
	SDL_Rect bot = { x: r->x, y: r->y + r->h / 2, w: r->w, h: r->h / 2 - 1 };
	int i;
	for (i = 0; i < t->nlines; i++) {
		enum hit ht, hb;
		ht = intersects(&t->l[i], &top);
		hb = intersects(&t->l[i], &bot);
		if (ht != HIT_NONE) { a |= (ht | HIT_TOP); }
		if (hb != HIT_NONE) { a |= (hb | HIT_BOT); }
	}

	return a;
}

static SDL_bool hang_hit(enum hit h)
{
	return !(h & HIT_BOT) && h & HIT_TOP && !(h & HIT_LEFT && h & HIT_RIGHT);
}

static int kick_entity(entity_state *e, enum hit h)
{
	if (h == HIT_NONE || h & HIT_TOP) { return 0; }
	if ((h & HIT_RIGHT && h & HIT_LEFT)) { return 0; }

	int dx = e->hitbox.w * (h & HIT_RIGHT ? -1 : 1);
	printf("%s kick %d ", st_names[e->st], dx);
	print_hit(h);
	e->pos.x += dx;

	return dx;
}

static SDL_bool stands_on_terrain(SDL_Rect const *r, level const *t)
{
	int x = r->x + r->w / 2;

	return getpixel(t->collision, x, r->y + r->h) == 0;
}

#define between(x, y1, y2) (x >= y1 && x <= y2)
static SDL_bool in_rect(SDL_Point const *p, SDL_Rect const *r)
{
	return between(p->x, r->x, r->x + r->w) &&
	       between(p->y, r->y, r->y + r->h);
}

static SDL_bool have_collision(SDL_Rect const *r1, SDL_Rect const *r2)
{
	int lf1 = r1->x;
	int rt1 = lf1 + r1->w;
	int tp1 = r1->y;
	int bt1 = tp1 + r1->h;

	int lf2 = r2->x;
	int rt2 = lf2 + r2->w;
	int tp2 = r2->y;
	int bt2 = tp2 + r2->h;

	return (between(lf1, lf2, rt2) || between(rt1, lf2, rt2)) &&
	       (between(tp1, tp2, bt2) || between(bt1, tp2, bt2));
}

static enum hit intersects(line const *l, SDL_Rect const *r)
{
	if (l->a.x == l->b.x) {
		int rx1 = r->x;
		int rxm = r->x + r->w / 2;
		int rx2 = r->x + r->w;
		int ry1 = r->y;
		int ry2 = r->y + r->h;
		if (between(l->a.x, rx1, rxm) && (between(ry1, l->a.y, l->b.y) ||
		                    (between(ry2, l->a.y, l->b.y)))) {
			return HIT_LEFT;
		}

		if (between(l->a.x, rxm, rx2) && (between(ry1, l->a.y, l->b.y) ||
		                    (between(ry2, l->a.y, l->b.y)))) {
			return HIT_RIGHT;
		}
	} else if (l->a.y == l->b.y) {
		// TODO
	} else {
		// TODO
	}

	return HIT_NONE;
}
#undef between

static void entity_hitbox(entity_state const *s, SDL_Rect *box)
{
	*box = (SDL_Rect) { x: s->pos.x,
			    y: s->pos.y + s->hitbox.y,
			    w: s->hitbox.w,
			    h: s->hitbox.h };
	switch (s->dir) {
	case DIR_LEFT:
		box->x += s->hitbox.x;
		break;
	case DIR_RIGHT:
		box->x += s->spawn.w - s->hitbox.x - s->hitbox.w;
		break;
	}
}

/* low level interactions */
static SDL_Texture *load_texture(SDL_Renderer *r, char const *file)
{
	SDL_Surface *tmp;
	SDL_Texture *tex;

	tmp = IMG_Load(file);
	if (!tmp) {
		fprintf(stderr, "Could not load image `%s': %s\n", file, IMG_GetError());
		return 0;
	}
	tex = SDL_CreateTextureFromSurface(r, tmp);
	SDL_FreeSurface(tmp);

	return tex;
}

static void draw_background(SDL_Renderer *r, SDL_Texture *bg, SDL_Rect const *screen)
{
	SDL_RenderCopy(r, bg, screen, 0);
}

static void draw_terrain_lines(SDL_Renderer *r, level const *lev, SDL_Rect const *screen)
{
	SDL_SetRenderDrawColor(r, 200, 20, 7, 255);
	int i;
	for (i = 0; i < lev->nlines; i++) {
		SDL_RenderDrawLine(r,
				   lev->l[i].a.x - screen->x,
				   lev->l[i].a.y - screen->y,
				   lev->l[i].b.x - screen->x,
				   lev->l[i].b.y - screen->y);
	}
}

static void draw_message_boxes(SDL_Renderer *r, msg_info const *msgs, SDL_Rect const *screen)
{
	int i;
	for (i = 0; i < msgs->n; i++) {
		SDL_bool fill = SDL_TRUE;
		switch(msgs->msgs[i].when) {
		case MSG_NEVER:
			fill = SDL_FALSE;
			/* fallthrough */
		case MSG_ONCE:
			SDL_SetRenderDrawColor(r, 0, 100, 0, 255);
			break;
		case MSG_ALWAYS:
			SDL_SetRenderDrawColor(r, 23, 225, 38, 255);
			break;
		}

		int const A = 4;
		SDL_Rect b = { x: msgs->msgs[i].pos.x - A / 2 - screen->x,
		               y: msgs->msgs[i].pos.y - A / 2 - screen->y,
			       w: A, h: A };
		if (fill) {
			SDL_RenderFillRect(r, &b);
		} else {
			SDL_RenderDrawRect(r, &b);
		}
	}
}

static void draw_message(SDL_Renderer *r, SDL_Texture *t, message const *m, SDL_Rect const *box, SDL_Rect const *line)
{
	SDL_RenderCopy(r, t, 0, box);

	SDL_Rect dst = { x: box->x + line->x, y: box->y + line->y };

	int i;
	for (i = 0; i < MSG_LINES; i++) {
		if (m->lines[i].tex) {
			dst.w = m->lines[i].size.x;
			dst.h = m->lines[i].size.y;
			SDL_RenderCopy(r, m->lines[i].tex, 0, &dst);
			dst.y += line->h;
		}
	}
}

static void draw_entity(SDL_Renderer *r, SDL_Rect const *scr, entity_state const *s, debug_state const *debug)
{
	entity_rule const *rl = s->rule;
	int w = rl->start_dim.w;
	int h = rl->start_dim.h;
	SDL_Rect src = { x: s->anim.frame * s->spawn.w, y: 0, w: w, h: h };
	SDL_Rect dst = { x: s->pos.x - scr->x, y: s->pos.y - scr->y, w: w, h: h };

	SDL_bool flip = s->dir == DIR_RIGHT;
	SDL_RenderCopyEx(r, s->tex, &src, &dst, 0, 0, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
	if (debug && debug->active) {

		if (debug->frames) {
			SDL_SetRenderDrawColor(r, 255, 105, 180, 255);
			SDL_RenderDrawRect(r, &dst);
		}

		if (debug->hitboxes) {
			SDL_Rect box;
			entity_hitbox(s, &box);
			box.x -= scr->x;
			box.y -= scr->y;
			SDL_SetRenderDrawColor(r, 23, 225, 38, 255);
			SDL_RenderDrawRect(r, &box);
		}
	}
}

static char const *get_asset(json_t *a, char const *k)
{
	json_t *v;

	v = json_object_get(a, k);
	if (!v) {
		fprintf(stderr, "no %s in %p\n", k, a);
		json_dumpf(a, stderr, 0);
		fprintf(stderr, "no %s\n", k);
		return 0;
	}

	char const *r = json_string_value(v);

	return r;
}

static SDL_bool get_int_field(json_t *o, char const *n, char const *s, int *r)
{
	json_t *var;

	var = json_object_get(o, s);
	if (!var) {
		fprintf(stderr, "warning: no %s for %s\n", s, n);
		*r = 0;
		return SDL_FALSE;
	}
	*r = json_integer_value(var);

	return SDL_TRUE;
}

static SDL_bool get_float_field(json_t *o, char const *n, char const *s, double *r)
{
	json_t *var;

	var = json_object_get(o, s);
	if (!var) {
		fprintf(stderr, "warning: no %s for %s\n", s, n);
		*r = 0;
		return SDL_FALSE;
	}
	*r = json_real_value(var);

	return SDL_TRUE;
}

static SDL_bool load_finish(session *s, json_t *game, TTF_Font *font, int fontsize)
{
	finish *f = &s->finish;
	json_t *fin = json_object_get(game, "finish");
	if (!fin) { return SDL_FALSE; }

	json_t *o = json_object_get(fin, "pos");
	f->pos.x = json_integer_value(json_array_get(o, 0));
	f->pos.y = json_integer_value(json_array_get(o, 1));

	o = json_object_get(fin, "win");
	f->win = (message) { pos: { x: f->pos.x,
	                            y: f->pos.y },
		             when: MSG_NEVER };
	render_message(&f->win, s->r, font, o, 0);

	o = json_object_get(fin, "loss");
	f->loss = (message) { pos: { x: f->pos.x,
	                             y: f->pos.y },
		              when: MSG_NEVER };
	render_message(&f->loss, s->r, font, o, 0);

	return SDL_TRUE;
}

static SDL_bool load_messages(session *s, json_t *game, TTF_Font *font, int fontsize, char const *root)
{
	json_t *o = json_object_get(game, "message");
	if (!o) { return SDL_FALSE; }

	msg_info *mi = &s->msg;

	SDL_Surface *msg_srf;
	msg_srf = load_asset_surf(o, root, "resource");
	if (!msg_srf) {
		fprintf(stderr, "warning: message texture missing\n");
		mi->n = 0;
		return SDL_FALSE;
	}

	mi->tex = SDL_CreateTextureFromSurface(s->r, msg_srf);
	mi->box = (SDL_Rect) { x: (s->screen.x - msg_srf->w) / 2,
	                       y: s->screen.y - msg_srf->h,
			       w: msg_srf->w,
			       h: msg_srf->h };
	SDL_FreeSurface(msg_srf);

	json_t *pos = json_object_get(o, "text-pos");
	mi->line = (SDL_Rect) { x: json_integer_value(json_array_get(pos, 0)),
	                        y: json_integer_value(json_array_get(pos, 1)),
				h: fontsize };
	mi->timeout = json_integer_value(json_object_get(o, "timeout"));

	o = json_object_get(game, "messages");
	int k = json_array_size(o);
	mi->n = k;
	message *ms = malloc(sizeof(message) * k);
	mi->msgs = ms;

	int i;
	json_t *m;
	json_array_foreach(o, i, m) {
		ms[i] = (message) {
			pos: { x: json_integer_value(json_array_get(m, 0)),
		               y: json_integer_value(json_array_get(m, 1)) },
		        when: streq(frq_names[MSG_ONCE], json_string_value(json_array_get(m, 2))) ?
				MSG_ONCE : MSG_ALWAYS };

		render_message(&ms[i], s->r, font, m, 3);
	}

	return SDL_TRUE;
}

static int load_collisions(level *level, json_t const *o)
{
	json_t *lines_o = json_object_get(o, "collision-lines");

	int k = json_array_size(lines_o);
	level->l = malloc(sizeof(line) * k);
	level->nlines = k;

	int i;
	json_t *l;
	json_array_foreach(lines_o, i, l) {
		if (json_array_size(l) != 4) {
			level->l[i] = (line) { a: { 0, 0 }, b: { 0, 0 }};
			continue;
		}
		level->l[i].a.x = json_integer_value(json_array_get(l, 0));
		level->l[i].a.y = json_integer_value(json_array_get(l, 1));
		level->l[i].b.x = json_integer_value(json_array_get(l, 2));
		level->l[i].b.y = json_integer_value(json_array_get(l, 3));
	}

	return k;
}

static SDL_Texture *load_asset_tex(json_t *a, char const *root, SDL_Renderer *r, char const *k)
{
	char const *f, *p;

	f = get_asset(a, k);
	if (!f) { return 0; }

	p = set_path("%s/%s/%s", root, ASSET_DIR, f);

	return load_texture(r, p);
}

static SDL_Surface *load_asset_surf(json_t *a, char const *root, char const *k)
{
	char const *f, *p;

	f = get_asset(a, k);
	if (!f) { return 0; }

	p = set_path("%s/%s/%s", root, ASSET_DIR, f);

	return IMG_Load(p);
}

static SDL_bool load_entity_resource(json_t *src, char const *n, SDL_Texture **t, SDL_Renderer *r, entity_rule *er, char const *root)
{
	json_t *o;
	char const *ps;
	o = json_object_get(src, "resource");
	ps = json_string_value(o);
	char const *path;
	path = set_path("%s/%s/%s", root, CONF_DIR, ps);

	get_int_field(src, n, "walk-dist", &er->walk_dist);
	get_int_field(src, n, "jump-dist-y", &er->jump_dist_y);
	get_int_field(src, n, "jump-dist-x", &er->jump_dist_x);
	get_int_field(src, n, "jump-time", &er->jump_time);
	get_int_field(src, n, "fall-dist", &er->fall_dist);
	get_float_field(src, n, "wide-jump-factor", &er->a_wide);
	get_float_field(src, n, "high-jump-factor", &er->a_high);

	json_error_t e;
	o = json_load_file(path, 0, &e);
	if (*e.text != 0) {
		fprintf(stderr, "error: in %s:%d: %s\n", path, e.line, e.text);
		return SDL_FALSE;
	}

	*t = load_asset_tex(o, root, r, "asset");
	if (!t) { return SDL_FALSE; }

	json_t *siz;
	siz = json_object_get(o, "frame_size");
	er->start_dim.w = json_integer_value(json_array_get(siz, 0));
	er->start_dim.h = json_integer_value(json_array_get(siz, 1));

	int i;
	for (i = 0; i < NSTATES; i++) {
		load_anim(o, n, st_names[i], &er->anim[i]);
	}

	json_decref(o);

	return SDL_TRUE;
}

static void render_message(message *ms, SDL_Renderer *r, TTF_Font *font, json_t *m, unsigned offset)
{
	SDL_Surface *text;
	SDL_Color col = {0, 0, 0, 255};

	int j;
	for (j = 0; j < MSG_LINES; j++) {
		char const *str = json_string_value(json_array_get(m, offset + j));
		if (str && *str) {
			text = TTF_RenderText_Blended(font, str, col);
			ms->lines[j].size.x = text->w;
			ms->lines[j].size.y = text->h;
			ms->lines[j].tex = SDL_CreateTextureFromSurface(r, text);
			SDL_FreeSurface(text);
		} else {
			ms->lines[j].tex = 0;
		}
	}
}

static void load_anim(json_t *src, char const *name, char const *key, animation_rule *a)
{
	json_t *o, *frames, *dur, *box;
	o = json_object_get(src, key);
	if (!o) {
		fprintf(stderr, "warning: no %s animation for %s\n", key, name);
		return;
	}

	frames = json_object_get(o, "frames");
	dur = json_object_get(o, "duration");

	int k, l;
	l = json_array_size(frames);
	k = json_array_size(dur);
	if (k != l) {
		fprintf(stderr, "error: have %d frames but %d durations\n", l, k);
		return;
	}
	a->len = l;
	a->frames   = malloc(sizeof(unsigned) * l);
	a->duration = malloc(sizeof(unsigned) * l);
	int i;
	for (i = 0; i < l; i++) {
		a->frames[i] = json_integer_value(json_array_get(frames, i));
		a->duration[i] = json_integer_value(json_array_get(dur, i));
	}

	box = json_object_get(o, "box");
	a->box.x = json_integer_value(json_array_get(box, 0));
	a->box.y = json_integer_value(json_array_get(box, 1));
	a->box.w = json_integer_value(json_array_get(box, 2));
	a->box.h = json_integer_value(json_array_get(box, 3));
}

static unsigned getpixel(SDL_Surface const *s, int x, int y)
{
	int bpp = s->format->BytesPerPixel;
	unsigned char *p = (unsigned char *)s->pixels + y * s->pitch + x * bpp;

	switch (bpp) {
	case 1:
		return *p;
		break;
	case 2:
		return *(short *)p;
		break;
	case 3:
		if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
			return p[0] << 16 | p[1] << 8 | p[2];
		} else {
			return p[0] | p[1] << 8 | p[2] << 16;
		}
		break;
	case 4:
		return *(unsigned *)p;
		break;
	default:
		fprintf(stderr, "getpixel: unsupported image format: %d bytes per pixel\n", bpp);
		return 0;
	}
}

static char const *set_path(char const *fmt, ...)
{
	static char buf[MAX_PATH];

	va_list ap;
	va_start(ap, fmt);

	vsnprintf(buf, MAX_PATH - 1, fmt, ap);
	va_end(ap);

	return buf;
}

static void print_hit(enum hit h)
{
	printf("hit:");
	if (h & HIT_LEFT) { printf(" left"); }
	if (h & HIT_RIGHT) { printf(" right"); }
	if (h & HIT_TOP) { printf(" top"); }
	if (h & HIT_BOT) { printf(" bot"); }
	puts("");
}
