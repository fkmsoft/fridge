#include <stdio.h>

#include <jansson.h>

#include <SDL.h>
#include <SDL_image.h>

#define TICK 80
#define ROOTVAR "FRIDGE_ROOT"

#define ASSET_DIR "assets"
#define CONF_DIR "conf"

typedef struct {
	SDL_Renderer *r;
	SDL_Texture *player;
	unsigned nobjects;
	SDL_Texture **objs;
	SDL_Texture *background;
	SDL_Surface *collision;
	int w;
	int h;
} session;

enum dir { DIR_LEFT, DIR_RIGHT };
enum state { ST_IDLE, ST_WALK, ST_FALL, ST_JUMP, NSTATES };
char const * const st_names[] = { "idle", "walk", "fall", "jump" };

typedef struct {
	unsigned len;
	unsigned *frames;
	unsigned *duration;
	SDL_Rect box;
} animation_rule;

typedef struct {
	SDL_Rect start_dim;
	animation_rule anim[NSTATES];
} entity_rule;

typedef struct {
	int walk_dist;
	int jump_dist;
	int jump_time;
	int fall_dist;
	entity_rule player;
	entity_rule *objs;
} game_rules;

typedef struct {
	int pos;
	int frame;
	int remaining;
} animation_state;

typedef struct {
	SDL_Rect pos;
	enum dir dir;
	enum state st;
	int jump_timeout;
	animation_state anim;
} entity_state;

typedef struct {
	entity_state player;
	entity_state *objs;
	SDL_bool run;
} game_state;

typedef struct {
	SDL_bool move_left;
	SDL_bool move_right;
	SDL_bool move_jump;
} entity_event;

typedef struct {
	entity_event player;
	SDL_bool exit;
	SDL_bool reset;
} game_event;

/* high level init */
static SDL_bool init_session(session *s, char const *root);
static SDL_bool init_game(session *s, game_rules *r, game_state *g, char const *root);
static void init_entity_state(entity_state *es, entity_rule const *er);

/* high level game */
static void process_event(SDL_Event const *ev, game_event *r);
static void process_keystate(unsigned char const *ks, game_event *r);
static void update_gamestate(session const *s, game_rules const *gr, game_state *gs, game_event const *ev);
static void tick_animation(animation_state *as, animation_rule const *ar);
static void load_state(entity_state *es, animation_rule const *ar);
static void render(session const *s, game_state const *gs, game_rules *gr);
static void clear_event(game_event *ev);

static SDL_bool collides_with_terrain(SDL_Rect const *r, SDL_Surface const *t);
static SDL_bool stands_on_terrain(SDL_Rect const *r, SDL_Surface const *t);
static void player_rect(SDL_Rect const *pos, SDL_Rect *r);

/* low level interactions */
static char const *get_asset(json_t *a, char const *k);
static SDL_bool get_int_field(json_t *o, char const *s, int *r);
static SDL_Texture *load_texture(SDL_Renderer *r, char const *file);
static SDL_Texture *load_asset_tex(json_t *a, char const *d, SDL_Renderer *r, char const *k);
static SDL_Surface *load_asset_surf(json_t *a, char const *d, char const *k);
static SDL_bool load_entity_resource(json_t *src, char const *n, SDL_Texture **t, SDL_Renderer *r, entity_rule *er, char const *root);
static void load_anim(json_t *src, char const *name, char const *key, animation_rule *a);
static void draw_background(SDL_Renderer *r, SDL_Texture *bg, SDL_Rect const *screen);
static void draw_player(SDL_Renderer *r, SDL_Texture *pl, unsigned int frame, SDL_bool flip, SDL_Rect const *dim, SDL_Rect const *scr);
static void draw_entity(SDL_Renderer *r, SDL_Texture *t, SDL_Rect const *scr, entity_state const *s, entity_rule *rl);
static unsigned getpixel(SDL_Surface const *s, int x, int y);

int main(void) {
	SDL_bool ok;
	char const *root = getenv(ROOTVAR);

	session s;
	ok = init_session(&s, root);
	if (!ok) { return 1; }

	game_rules gr;
	game_state gs;
	ok = init_game(&s, &gr, &gs, root);
	if (!ok) { return 2; }

	game_event ge;
	clear_event(&ge);

	unsigned ticks;
	unsigned old_ticks = SDL_GetTicks();

	int have_ev;
	SDL_Event event;
	while (gs.run) {
		have_ev = SDL_PollEvent(&event);
		if (have_ev) {
			process_event(&event, &ge);
		}

		unsigned char const *keystate = SDL_GetKeyboardState(0);
		process_keystate(keystate, &ge);

		ticks = SDL_GetTicks();
		if (ticks - old_ticks >= TICK) {
			update_gamestate(&s, &gr, &gs, &ge);
			clear_event(&ge);
			/*
			printf("%d\n", ticks - old_ticks);
			*/
			old_ticks = ticks;
		}

		render(&s, &gs, &gr);
		SDL_Delay(TICK / 4);
	}

	SDL_Quit();

	return 0;
}

/* high level init */
static SDL_bool init_session(session *s, char const *root)
{
	SDL_Window *window;
	s->w = 800;
	s->h = 600;
	int i;

	i = SDL_Init(SDL_INIT_VIDEO);
	if (i < 0) {
		return SDL_FALSE;
	}

	window = SDL_CreateWindow("Fridge Filler", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, s->w, s->h, 0);
	if (!window) {
		fprintf(stderr, "Could not init video: %s\n", SDL_GetError());
		return SDL_FALSE;
	}

	/*SDL_SetWindowIcon(window, temp);*/
	s->r = SDL_CreateRenderer(window, -1, 0);

	char asset_dir[500];
	sprintf(asset_dir, "%s/%s/%s", root, CONF_DIR, "assets.json");

	json_t *assets;
	assets = json_load_file(asset_dir, 0, 0);

	sprintf(asset_dir, "%s/%s", root, ASSET_DIR);

	s->background = load_asset_tex(assets, asset_dir, s->r, "background");
	if (!s->background) { return SDL_FALSE; }

	s->collision = load_asset_surf(assets, asset_dir, "collision");
	if (!s->collision) { return SDL_FALSE; }

	json_decref(assets);

	return SDL_TRUE;
}

static SDL_bool init_game(session *s, game_rules *gr, game_state *gs, char const *root)
{
	json_t *game, *player, *start, *objs;

	char buf[500];
	sprintf(buf, "%s/%s/%s", root, CONF_DIR, "game.json");
	game = json_load_file(buf, 0, 0);

	objs = json_object_get(game, "objects");
	if (!objs) {
		s->nobjects = 0;
		s->objs = 0;
		gr->objs = 0;
		gs->objs = 0;
	} else {
		int k;
		k = json_object_size(objs);
		s->nobjects = k;
		s->objs = malloc(sizeof(SDL_Texture *) * k);
		gr->objs = malloc(sizeof(entity_rule) * k);
		gs->objs = malloc(sizeof(entity_state) * k);

		int i = 0;
		char const *name;
		json_t *o;
		json_object_foreach(objs, name, o) {
			start = json_object_get(o, "start");
			gr->objs[i].start_dim.x = json_integer_value(json_array_get(start, 0));
			gr->objs[i].start_dim.y = json_integer_value(json_array_get(start, 1));
			load_entity_resource(o, name, &s->objs[i], s->r, &gr->objs[i], root);
			init_entity_state(&gs->objs[i], &gr->objs[i]);
			i += 1;
		}
	}

	player = json_object_get(game, "player");
	if (!player) {
		puts("no player");
		return SDL_FALSE;
	}

	start = json_object_get(player, "start");
	if (!start) {
		puts("no start");
		return SDL_FALSE;
	}

	gr->player.start_dim.x = json_integer_value(json_array_get(start, 0));
	gr->player.start_dim.y = json_integer_value(json_array_get(start, 1));

	SDL_bool ok;
	ok = get_int_field(player, "walk_dist", &gr->walk_dist);
	if (!ok) { return SDL_FALSE; }
	ok = get_int_field(player, "jump_dist", &gr->jump_dist);
	if (!ok) { return SDL_FALSE; }
	ok = get_int_field(player, "jump_time", &gr->jump_time);
	if (!ok) { return SDL_FALSE; }
	ok = get_int_field(player, "fall_dist", &gr->fall_dist);
	if (!ok) { return SDL_FALSE; }

        ok = load_entity_resource(player, "player", &s->player, s->r, &gr->player, root);
	if (!ok) {
		fprintf(stderr, "no player resource\n");
		return SDL_FALSE;
	}

	json_decref(game);

	init_entity_state(&gs->player, &gr->player);
	gs->run = SDL_TRUE;

	return SDL_TRUE;
}

static void init_entity_state(entity_state *es, entity_rule const *er)
{
	enum state start_state = ST_IDLE;

	es->dir = DIR_LEFT;
	es->st = start_state;
	load_state(es, &er->anim[start_state]);
	es->pos.x = er->start_dim.x;
	es->pos.y = er->start_dim.y;
	es->jump_timeout = 0;
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
		switch(key) {
		case SDLK_q:
			r->exit = SDL_TRUE;
			break;
		case SDLK_SPACE:
			r->player.move_jump = SDL_TRUE;
			break;
		case SDLK_r:
			r->reset = SDL_TRUE;
			break;
		}
		break;
	}
}

static void process_keystate(unsigned char const *ks, game_event *r)
{
	if (ks[SDL_SCANCODE_LEFT]) { r->player.move_left = SDL_TRUE; }
	if (ks[SDL_SCANCODE_RIGHT]) { r->player.move_right = SDL_TRUE; }
}

static void update_gamestate(session const *s, game_rules const *gr, game_state *gs, game_event const *ev)
{
	tick_animation(&gs->player.anim, &gr->player.anim[gs->player.st]);
	int i;
	for (i = 0; i < s->nobjects; i++) {
		tick_animation(&gs->objs[i].anim, &gr->objs[i].anim[gs->objs[i].st]);
	}

	SDL_Rect r;
	if (ev->player.move_left || ev->player.move_right) {
		gs->player.dir = ev->player.move_left ? DIR_LEFT : DIR_RIGHT;

		int old_x = gs->player.pos.x;
		int dir = (gs->player.dir == DIR_LEFT) ? -1 : 1;
		gs->player.pos.x += dir * gr->walk_dist;

		player_rect(&gs->player.pos, &r);

		if (collides_with_terrain(&r, s->collision)) {
			gs->player.pos.x = old_x;
			player_rect(&gs->player.pos, &r);
			/*
			do {
				gs->player_x += dir;
				r->x += dir;
			} while (!collides_with_terrain(&r, s->collision));
			*/
		}
	}

	if (ev->player.move_jump && gs->player.st != ST_FALL) {
		gs->player.jump_timeout = gr->jump_time;
	}

	if (gs->player.jump_timeout > 0) {
		int old_y = gs->player.pos.y;
		gs->player.jump_timeout -= 1;
		gs->player.pos.y -= gr->jump_dist;

		player_rect(&gs->player.pos, &r);
		if (collides_with_terrain(&r, s->collision)) {
			gs->player.pos.y = old_y;
			gs->player.jump_timeout = 0;
		}
	} else {
		player_rect(&gs->player.pos, &r);
		int f;
		for (f = gr->jump_dist; !stands_on_terrain(&r, s->collision) && f > 0; f--) {
			r.y += 1;
			gs->player.pos.y += 1;
		}
	}

	enum state old_state = gs->player.st;
	player_rect(&gs->player.pos, &r);
	if (gs->player.jump_timeout > 0) {

		gs->player.st = ST_JUMP;

	} else if (!stands_on_terrain(&r, s->collision)) {

		gs->player.st = ST_FALL;

	} else if (ev->player.move_left || ev->player.move_right) {

		gs->player.st = ST_WALK;
	} else {

		gs->player.st = ST_IDLE;
	}

	if (old_state != gs->player.st) {
		load_state(&gs->player, &gr->player.anim[gs->player.st]);
	}

	if (ev->reset) {
		init_entity_state(&gs->player, &gr->player);
	}

	if (ev->exit) {
		gs->run = SDL_FALSE;
	}
}

static void tick_animation(animation_state *as, animation_rule const *ar)
{
	as->remaining -= 1;
	if (as->remaining < 0) {
		as->pos = (as->pos + 1) % ar->len;
		as->frame = ar->frames[as->pos];
		as->remaining = ar->duration[as->pos];
	}
}

static void load_state(entity_state *es, animation_rule const *ar)
{
	es->anim.pos = 0;
	es->anim.frame = ar->frames[0];
	es->anim.remaining = ar->duration[0];
	es->pos.w = ar->box.w;
	es->pos.h = ar->box.h;
}

static void render(session const *s, game_state const *gs, game_rules *gr)
{
	int i;
	SDL_RenderClear(s->r);

	SDL_Rect screen = { x: gs->player.pos.x, y: gs->player.pos.y, w: s->w, h: s->h };

	draw_background(s->r, s->background, &screen);
	for (i = 0; i < s->nobjects; i++) {
		draw_entity(s->r, s->objs[i], &screen, &gs->objs[i], &gr->objs[i]);
	}
	draw_player(s->r, s->player, gs->player.anim.frame, gs->player.dir == DIR_RIGHT, &gr->player.start_dim, &screen);

	SDL_RenderPresent(s->r);
}

static void clear_event(game_event *ev)
{
	ev->player.move_left = SDL_FALSE;
	ev->player.move_right = SDL_FALSE;
	ev->player.move_jump = SDL_FALSE;
	ev->exit = SDL_FALSE;
	ev->reset =SDL_FALSE;
}

static SDL_bool collides_with_terrain(SDL_Rect const *r, SDL_Surface const *t)
{
	int x = r->x;
	int y = r->y;

	/*
	if (getpixel(t, x,        y       ) == 0) { printf("left  head: %u %u\n", x,      y); }
	if (getpixel(t, x + r->w, y       ) == 0) { printf("right head: %u %u\n", x+r->w, y); }
	if (getpixel(t, x,        y + r->h) == 0) { printf("left  foot: %u %u\n", x,      y+r->h); }
	if (getpixel(t, x + r->w, y + r->h) == 0) { printf("right foot: %u %u\n", x+r->w, y+r->h); }
	*/

	return getpixel(t, x,        y       ) == 0 ||
	       getpixel(t, x + r->w, y       ) == 0 ||
	       getpixel(t, x,        y + r->h) == 0 ||
	       getpixel(t, x + r->w, y + r->h) == 0;
}

static SDL_bool stands_on_terrain(SDL_Rect const *r, SDL_Surface const *t)
{
	int x = r->x + r->w / 2;

	return getpixel(t, x, r->y + r->h + 1) == 0;
}

static void player_rect(SDL_Rect const *pos, SDL_Rect *r)
{
	r->x = pos->x + 400 - 9;
	r->y = pos->y + 300 - 25;
	r->w = /*pos->w*/ 18;
	r->h = /*pos->h*/ 55;
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

static void draw_player(SDL_Renderer *r, SDL_Texture *pl, unsigned int frame, SDL_bool flip, SDL_Rect const *dim, SDL_Rect const *scr)
{
	SDL_Rect src = { x: frame * dim->w, y: 0, w: dim->w, h: dim->h };
	SDL_Rect dst = { x: (scr->w - dim->w)/2, y: (scr->h - dim->h)/2, w: dim->w, h: dim->h };

	SDL_RenderCopyEx(r, pl, &src, &dst, 0, 0, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

static void draw_entity(SDL_Renderer *r, SDL_Texture *t, SDL_Rect const *scr, entity_state const *s, entity_rule *rl)
{
	int w = rl->start_dim.w;
	int h = rl->start_dim.h;
	SDL_Rect src = { x: s->anim.frame * 32, y: 0, w: w, h: h };
	SDL_Rect dst = { x: s->pos.x - scr->x, y: s->pos.y - scr->y, w: w, h: h };

	SDL_bool flip = s->dir == DIR_RIGHT;
	SDL_RenderCopyEx(r, t, &src, &dst, 0, 0, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

static char const *get_asset(json_t *a, char const *k)
{
	json_t *v;

	v = json_object_get(a, k);
	if (!v) {
		fprintf(stderr, "no %s\n", k);
		return 0;
	}

	char const *r = json_string_value(v);

	return r;
}

static SDL_bool get_int_field(json_t *o, char const *s, int *r)
{
	json_t *var;

	var = json_object_get(o, s);
	if (!var) {
		fprintf(stderr, "no %s\n", s);
		return SDL_FALSE;
	}
	*r = json_integer_value(var);

	return SDL_TRUE;
}

static SDL_Texture *load_asset_tex(json_t *a, char const *d, SDL_Renderer *r, char const *k)
{
	char const *f;
	char p[500];

	f = get_asset(a, k);
	if (!f) { return 0; }

	sprintf(p, "%s/%s", d, f);

	return load_texture(r, p);
}

static SDL_Surface *load_asset_surf(json_t *a, char const *d, char const *k)
{
	char const *f;
	char p[500];

	f = get_asset(a, k);
	if (!f) { return 0; }

	sprintf(p, "%s/%s", d, f);

	return IMG_Load(p);
}

static SDL_bool load_entity_resource(json_t *src, char const *n, SDL_Texture **t, SDL_Renderer *r, entity_rule *er, char const *root)
{
	json_t *o;
	char const *ps;
	o = json_object_get(src, "resource");
	ps = json_string_value(o);
	char buf[500];
	sprintf(buf, "%s/%s/%s", root, CONF_DIR, ps);

	json_error_t e;
	o = json_load_file(buf, 0, &e);

	sprintf(buf, "%s/%s", root, ASSET_DIR);
	*t = load_asset_tex(o, buf, r, "asset");
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
