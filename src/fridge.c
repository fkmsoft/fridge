#include <stdio.h>
#include <string.h>

#include <jansson.h>

#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>

#include "engine.h"

#define TICK 40

#define MSG_LINES 2

#define ROOTVAR "FRIDGE_ROOT"
#define GAME_CONF "game.json"

enum mode { MODE_LOGO, MODE_INTRO, MODE_GAME, MODE_EXIT };

enum msg_frequency { MSG_NEVER, MSG_ONCE, MSG_ALWAYS };
static char const * const frq_names[] = { "never", "once", "always" };

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
	SDL_Window *w;
	SDL_Renderer *r;
	level level;
	msg_info msg;
	finish finish;
	SDL_Point screen;
} session;

typedef struct {
	unsigned n;
	entity_state *e;
} group;

enum group { GROUP_PLAYER, GROUP_OBJECTS, GROUP_ENEMIES, NGROUPS };

typedef struct {
	int need_to_collect;
	entity_state logo;
	entity_state intro;
	group entities[NGROUPS];
	message const *msg;
	unsigned msg_timeout;
	enum mode run;
	debug_state debug;
} game_state;

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
void init_group(group *g, json_t const *game, json_t const *entities, char const *key, SDL_Texture **e_texs, entity_rule const *e_rules, enum state st);

/* high level game */
static void process_event(SDL_Event const *ev, game_event *r);
static void update_gamestate(session *s, game_state *gs, game_event const *ev);
static void set_group_state(group *g, enum state st);
static void enemy_movement(level const *terrain, group *nmi, SDL_Rect const *player);
static void render(session const *s, game_state const *gs);
static void clear_game(game_state *gs);
static void clear_event(game_event *ev);

/* collisions */
static SDL_bool in_rect(SDL_Point const *p, SDL_Rect const *r);
static SDL_bool have_collision(SDL_Rect const *r1, SDL_Rect const *r2);

/* low level interactions */
static SDL_bool load_finish(session *s, json_t *game, TTF_Font *font, int fontsize);
static SDL_bool load_messages(session *s, json_t *game, TTF_Font *font, int fontsize, char const *root);
static int load_collisions(level *level, json_t const *o);
static SDL_Surface *load_asset_surf(json_t *a, char const *d, char const *k);
static void render_message(message *ms, SDL_Renderer *r, TTF_Font *font, json_t *m, unsigned offset);
static void draw_message_boxes(SDL_Renderer *r, msg_info const *msgs, SDL_Rect const *screen);
static void render_entity_info(SDL_Renderer *r, TTF_Font *font, entity_state const *e);
static void draw_message(SDL_Renderer *r, SDL_Texture *t, message const *m, SDL_Rect const *box, SDL_Rect const *line);
#if 0
static void print_hit(enum hit h);
#endif

static void print_event(FILE *fd, game_event *e)
{
	if (e->player.walk)       { fputs("walk\n",  fd); }
	if (e->player.move_left)  { fputs("left\n",  fd); }
	if (e->player.move_right) { fputs("right\n", fd); }
	if (e->player.move_jump)  { fputs("jump\n",  fd); }

	if (e->toggle_pause)   { fputs("pause\n",    fd); }
	if (e->toggle_debug)   { fputs("debug\n",    fd); }
	if (e->toggle_terrain) { fputs("hits\n",     fd); }
	if (e->reload_conf)    { fputs("conf\n",     fd); }
	if (e->exit)           { fputs("exit\n",     fd); }
	if (e->keyboard)       { fputs("keyboard\n", fd); }
	if (e->reset)          { fputs("spawn\n",    fd); }

	fputs("tick\n", fd);
}

static void read_event(FILE *fd, game_event *e)
{
	char buf[MAX_PATH];
	while (1) {
	fgets(buf, MAX_PATH - 1, fd);
	switch (*buf) {
	case 'w': e->player.walk = SDL_TRUE; break;
	case 'l': e->player.move_left = SDL_TRUE; break;
	case 'r': e->player.move_right = SDL_TRUE; break;
	case 'j': e->player.move_jump = SDL_TRUE; break;
	case 'p': e->toggle_pause = SDL_TRUE; break;
	case 'd': e->toggle_debug = SDL_TRUE; break;
	case 'h': e->toggle_terrain = SDL_TRUE; break;
	case 'c': e->reload_conf = SDL_TRUE; break;
	case 'e': e->exit = SDL_TRUE; break;
	case 'k': e->keyboard = SDL_TRUE; break;
	case 's': e->reset = SDL_TRUE; break;
	case 't': return;
	default: break;
	}
	}
}

int main(int argc, char **argv) {
	SDL_bool ok;
	char const *root = getenv(ROOTVAR);
	if (!root || !*root) {
		fprintf(stderr, "error: environment undefined\n");
		fprintf(stderr, "set %s to the installation directory of Fridge Filler\n", ROOTVAR);
		return 1;
	}

	FILE *rp = 0;
	SDL_bool rp_play = SDL_FALSE;
	SDL_bool rp_save = SDL_FALSE;
	if (argc > 1) {
		if (streq(argv[1], "--save-replay") || streq(argv[1], "-s")) {
			char const *fname = "replay.txt";
			if (argc == 3) { fname = argv[2]; }
			printf("saving replay to `%s'\n", fname);
			rp = fopen(fname, "w");
			rp_save = SDL_TRUE;
		}

		if (streq(argv[1], "--replay") || streq(argv[1], "-r")) {
			char const *fname = "replay.txt";
			if (argc == 3) { fname = argv[2]; }
			printf("loading replay `%s'\n", fname);
			rp = fopen(fname, "r");
			rp_play = SDL_TRUE;
		}
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
		if (!rp_play) {
			have_ev = SDL_PollEvent(&event);
			if (have_ev) {
				process_event(&event, &ge);
			}

			unsigned char const *keystate = SDL_GetKeyboardState(0);
			keystate_to_movement(keystate, &ge.player);
		} else {
			have_ev = SDL_PollEvent(&event);
			if (have_ev) {
				process_event(&event, &ge);
				if (ge.exit) { break; }
				clear_event(&ge);
			}
		}

		ticks = SDL_GetTicks();
		if (ticks - old_ticks >= TICK) {
			if (rp_save) { print_event(rp, &ge); }
			else if (rp_play) { read_event(rp, &ge); }
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

	int i;
	for (i = 0; i < NGROUPS; i++) {
		free(gs.entities[i].e);
	}

	destroy_level(&s.level);
	SDL_DestroyTexture(s.level.background);

	if (gs.debug.font) {
		TTF_CloseFont(gs.debug.font);
	};

	if (rp) { fclose(rp); }
	free(s.msg.msgs);

	SDL_DestroyRenderer(s.r);
	SDL_DestroyWindow(s.w);
	SDL_Quit();

	return 0;
}

/* high level init */
static SDL_bool init_game(session *s, game_state *gs, char const *root)
{
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

	s->w = SDL_CreateWindow("Fridge Filler", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, s->screen.x, s->screen.y, 0);
	if (!s->w) {
		fprintf(stderr, "Could not init video: %s\n", SDL_GetError());
		return SDL_FALSE;
	}

	path = set_path("%s/%s", root, "icon.gif");
	SDL_Surface *ico = IMG_Load(path);
	if (ico) {
		puts("have icon");
		SDL_SetWindowIcon(s->w, ico);
		SDL_FreeSurface(ico);
	}

	s->r = SDL_CreateRenderer(s->w, -1, 0);

	gs->debug.font = 0;
	SDL_bool ok = load_config(s, gs, game, root);
	if (!ok) { return SDL_FALSE; }

	gs->run = gs->logo.active ? MODE_LOGO : gs->intro.active ? MODE_INTRO : MODE_GAME;
	clear_game(gs);

	return SDL_TRUE;
}

static SDL_bool load_config(session *s, game_state *gs, json_t *game, char const *root)
{
	json_t *entities, *fnt, *level;
	char const *file, *path;

	/* load config */
	fnt = json_object_get(game, "font");
	path = set_path("%s/%s/%s", root, ASSET_DIR, json_string_value(json_object_get(fnt, "resource")));
	int fnt_siz = json_integer_value(json_object_get(fnt, "size"));
	TTF_Font *font = TTF_OpenFont(path, fnt_siz);
	if (!font) {
		fprintf(stderr, "error: could not load font %s: %s\n", path, TTF_GetError());
		return SDL_FALSE;
	}

	if (!gs->debug.font) {
		gs->debug.font = TTF_OpenFont("debug_font.ttf", 14);
	}

	SDL_bool ok;
	ok = load_finish(s, game, font, fnt_siz);
	if (!ok) { return SDL_FALSE; }

	ok = load_messages(s, game, font, fnt_siz, root);
	if (!ok) { return SDL_FALSE; }
	TTF_CloseFont(font);

	level = json_object_get(game, "level");
	if (!ok) { return SDL_FALSE; }
	path = set_path("%s/%s/%s", root, CONF_DIR, json_string_value(level));
	json_error_t e;
	level = json_load_file(path, 0, &e);
	if (*e.text != 0) {
		fprintf(stderr, "Error at %s:%d: %s\n", path, e.line, e.text);
		return SDL_FALSE;
	}

	s->level.background = load_asset_tex(level, root, s->r, "resource");
	if (!s->level.background) { return SDL_FALSE; }

	load_collisions(&s->level, level);
	json_decref(level);

	entity_rule *e_rules;
	SDL_Texture **e_texs;
	entities = json_object_get(game, "entities");
	if (!entities) {
		fprintf(stderr, "Error: No entities defined, need player\n");
		return SDL_FALSE;
	}

	file = json_string_value(json_object_get(entities, "resource"));
	path = set_path("%s/%s/%s", root, CONF_DIR, file);
	entities = load_entities(root, path, s->r, &e_texs, &e_rules);
	if (!entities) {
		fprintf(stderr, "Error: Could not load entities\n");
		return SDL_FALSE;
	}

	init_group(&gs->entities[GROUP_PLAYER ], game, entities, "players", e_texs, e_rules, ST_IDLE);
	init_group(&gs->entities[GROUP_OBJECTS], game, entities, "objects", e_texs, e_rules, ST_IDLE);
	init_group(&gs->entities[GROUP_ENEMIES], game, entities, "enemies", e_texs, e_rules, ST_WALK);

	load_intro(&gs->logo, s, entities, "logo", e_rules, e_texs);
	load_intro(&gs->intro, s, entities, "intro", e_rules, e_texs);

	/* all texture pointers are copied by value, no need to hold onto the
	 * e_texs buffer */
	free(e_texs);

	json_decref(entities);
	json_decref(game);

	return SDL_TRUE;
}

static void load_intro(entity_state *intro, session const *s, json_t *o, char const *k, entity_rule const *e_rules, SDL_Texture **e_texs)
{
	json_t *io;
	io = json_object_get(o, k);
	if (!io) {
		fprintf(stderr, "Warning: No intro found for `%s'\n", k);
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

void init_group(group *g, json_t const *game, json_t const *entities, char const *key, SDL_Texture **e_texs, entity_rule const *e_rules, enum state st)
{
	json_t *objs;
	entity_state *a;

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
		json_t *entity, *rules;
		entity = json_object_get(entities, name);
		ei = json_integer_value(json_object_get(entity, "index"));
		json_array_foreach(o, j, spawn) {
			a[i].spawn.x = json_integer_value(json_array_get(spawn, 0));
			a[i].spawn.y = json_integer_value(json_array_get(spawn, 1));
                        init_entity_state(&a[i], &e_rules[ei], e_texs[ei], st);
                        rules = json_array_get(spawn, 2);
                        if (rules) {
                                entity_rule *custom;
                                custom = malloc(sizeof(entity_rule));
                                *custom = *a[i].rule;
                                load_entity_rule(rules, custom, "custom-rule");
                                a[i].rule = custom;
                        }
			i += 1;
		}
	}
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

static void update_gamestate(session *s, game_state *gs, game_event const *ev)
{
	if (gs->run == MODE_LOGO) {

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
				SDL_Point ps = gs->entities[GROUP_PLAYER].e[0].pos;
				enum dir dr = gs->entities[GROUP_PLAYER].e[0].dir;
				fprintf(stderr, "info: re-loading config\n");
				load_config(s, gs, g, r);
				gs->entities[GROUP_PLAYER].e[0].pos = ps;
				gs->entities[GROUP_PLAYER].e[0].dir = dr;
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
		SDL_Rect h;
		entity_hitbox(&gs->entities[GROUP_PLAYER].e[0], &h);
		enemy_movement(&s->level, &gs->entities[GROUP_ENEMIES], &h);
	}

	enum state old_state = gs->entities[GROUP_PLAYER].e[0].st;
	move_log log;
	move_entity(&gs->entities[GROUP_PLAYER].e[0], &ev->player, &s->level, &log);

	if (old_state != gs->entities[GROUP_PLAYER].e[0].st) {
		/* print_state
		printf("%s -> %s\n", st_names[old_state], st_names[gs->entities[GROUP_PLAYER].e[0].st]);
		*/
		load_state(&gs->entities[GROUP_PLAYER].e[0]);
	}

	if (ev->reset) {
		init_entity_state(&gs->entities[GROUP_PLAYER].e[0], 0, 0, ST_IDLE);
	}

	if (gs->msg_timeout > 0) {
		gs->msg_timeout -= 1;
	} else {
		gs->msg = 0;
	}
	SDL_Rect r;
	entity_hitbox(&gs->entities[GROUP_PLAYER].e[0], &r);
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
					init_entity_state(&gs->entities[GROUP_PLAYER].e[0], 0, 0, ST_IDLE);
					break;
				case GROUP_PLAYER:
					break;
				case NGROUPS:
					fprintf(stderr, "line %d: can never happen\n", __LINE__);
				}
			}
		}
	}

	entity_hitbox(&gs->entities[GROUP_PLAYER].e[0], &r);
	if (in_rect(&s->finish.pos,  &r)) {
		if (gs->need_to_collect <= 0) {
			gs->msg = &s->finish.win;
		} else {
			gs->msg = &s->finish.loss;
		}
	}

	if (ev->exit) {
		gs->run = MODE_EXIT;
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

static void enemy_movement(level const *terrain, group *nmi, SDL_Rect const *player)
{
	int i;
	for (i = 0; i < nmi->n; i++) {
		entity_state *e = &nmi->e[i];
		entity_event order;
		clear_order(&order);
		SDL_Rect h;
		entity_hitbox(e, &h);
		SDL_bool track = SDL_FALSE;
		if (between(player->y, h.y, h.y + h.h) || between(h.y, player->y, player->y + player->h)) {
			e->dir = (e->pos.x < player->x) ? DIR_RIGHT : DIR_LEFT;
			track = SDL_TRUE;
		}
		if (between(player->x, h.x, h.x + h.w) && e->pos.y > player->y) {
			order.move_jump = SDL_TRUE;
			track = SDL_TRUE;
		}
		h.x += e->dir * e->rule->walk_dist;
		if (collides_with_terrain(&h, terrain) == HIT_NONE && (!e->rule->has_gravity || stands_on_terrain(&h, terrain))) {
			order.walk = SDL_TRUE;
		}
		move_log log;
		move_entity(e, &order, terrain, &log);
		if (!track && !order.walk) {
			e->dir *= -1;
		}
	}
}

static void render(session const *s, game_state const *gs)
{
	int i;
	SDL_RenderClear(s->r);

	SDL_Rect screen = { x: gs->entities[GROUP_PLAYER].e[0].pos.x - (s->screen.x - gs->entities[GROUP_PLAYER].e[0].spawn.w) / 2,
	                    y: gs->entities[GROUP_PLAYER].e[0].pos.y - (s->screen.y - gs->entities[GROUP_PLAYER].e[0].spawn.h) / 2,
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

		draw_entity(s->r, &screen, &gs->entities[GROUP_PLAYER].e[0], &gs->debug);
		if (gs->debug.active) {
			render_entity_info(s->r, gs->debug.font, &gs->entities[GROUP_PLAYER].e[0]);
		}

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
	clear_order(&ev->player);
	ev->exit = SDL_FALSE;
	ev->toggle_debug = SDL_FALSE;
	ev->toggle_pause = SDL_FALSE;
	ev->toggle_terrain = SDL_FALSE;
	ev->reload_conf = SDL_FALSE;
	ev->keyboard = SDL_FALSE;
	ev->reset = SDL_FALSE;
}

/* collisions */
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

	return (between(lf1, lf2, rt2) || between(rt1, lf2, rt2) || between(lf2, lf1, rt1) || between(rt2, lf1, rt1)) &&
	       (between(tp1, tp2, bt2) || between(bt1, tp2, bt2) || between(tp2, tp1, bt1) || between(bt2, tp1, bt1));
}

/* low level interactions */
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

static void render_entity_info(SDL_Renderer *r, TTF_Font *font, entity_state const *e)
{
	char const *s;
	s = set_path("pos:  %04d %04d, state: %s", e->pos.x, e->pos.y, st_names[e->st]);
	int l = 0;
	render_line(r, s, font, l++);

	SDL_Rect hb;
	entity_hitbox(e, &hb);

	SDL_Point ft = entity_feet(&hb);
	s = set_path("feet: %04d %04d", ft.x, ft.y);
	render_line(r, s, font, l++);

	if (e->fall_time > 0) {
		s = set_path("fall time: %03d", e->fall_time);
		render_line(r, s, font, l++);
	}

	if (e->jump_timeout > 0) {
		s = set_path("jump timeout: %03d", e->jump_timeout);
		render_line(r, s, font, l++);
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
		fprintf(stderr, "Warning: Message texture missing\n");
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
	level->vertical = malloc(sizeof(line) * k);
	level->horizontal = malloc(sizeof(line) * k);
	level->nvertical = 0;
	level->nhorizontal = 0;

	int i;
	json_t *l;
	json_array_foreach(lines_o, i, l) {
		if (json_array_size(l) != 4) {
			puts("incomplete line");
			continue;
		}
		int ax, ay, bx, by;
		ax = json_integer_value(json_array_get(l, 0));
		ay = json_integer_value(json_array_get(l, 1));
		bx = json_integer_value(json_array_get(l, 2));
		by = json_integer_value(json_array_get(l, 3));

		if (ax == bx) {
			level->vertical[level->nvertical] = (line) { ax, ay, by };
			level->nvertical += 1;
		} else if (ay == by) {
			level->horizontal[level->nhorizontal] = (line) { ay, ax, bx };
			level->nhorizontal += 1;
		} else {
			fprintf(stderr, "Warning: Ignoring diagonal line %d %d - %d %d\n",
					ax, ay, bx, by);
		}
	}

	/* sort by p component */
	qsort(level->vertical, level->nvertical, sizeof(line), cmp_lines);
	qsort(level->horizontal, level->nhorizontal, sizeof(line), cmp_lines);

	return k;
}

static SDL_Surface *load_asset_surf(json_t *a, char const *root, char const *k)
{
	char const *f, *p;

	f = get_asset(a, k);
	if (!f) { return 0; }

	p = set_path("%s/%s/%s", root, ASSET_DIR, f);

	return IMG_Load(p);
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

#if 0
static void print_hit(enum hit h)
{
	printf("hit:");
	if (h & HIT_LEFT) { printf(" left"); }
	if (h & HIT_RIGHT) { printf(" right"); }
	if (h & HIT_TOP) { printf(" top"); }
	if (h & HIT_BOT) { printf(" bot"); }
	puts("");
}
#endif
