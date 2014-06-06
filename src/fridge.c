#include <stdio.h>

#include <jansson.h>

#include <SDL.h>
#include <SDL_image.h>

#define TICK 80
#define ROOTVAR "FRIDGE_ROOT"

struct session {
	SDL_Renderer *r;
	SDL_Texture *player;
	SDL_Texture *background;
	SDL_Surface *collision;
	int w;
	int h;
};

enum dir { DIR_LEFT, DIR_RIGHT };
enum state { ST_IDLE, ST_WALK, ST_FALL, ST_JUMP, NSTATES };
char const * const st_names[] = { "idle", "walk", "fall", "jump" };

struct animation_rule {
	unsigned len;
	unsigned *frames;
	unsigned *duration;
	SDL_Rect box;
};

struct entity_rule {
	SDL_Rect start_dim;
	struct animation_rule anim[NSTATES];
};

struct game_rules {
	int start_x;
	int start_y;
	int walk_dist;
	int jump_dist;
	int jump_time;
	int fall_dist;
	struct entity_rule player;
};

struct animation_state {
		int pos;
		int frame;
		int remaining;
};

struct entity_state {
	SDL_Rect pos;
	enum dir dir;
	enum state st;
	int jump_timeout;
	struct animation_state anim;
};

struct game_state {
	struct entity_state player;
	SDL_bool run;
};

struct entity_event {
	SDL_bool move_left;
	SDL_bool move_right;
	SDL_bool move_jump;
};

struct game_event {
	struct entity_event player;
	SDL_bool exit;
	SDL_bool reset;
};

typedef struct session session;
typedef struct game_rules game_rules;
typedef struct game_state game_state;
typedef struct game_event game_event;

/* high level init */
SDL_bool init_session(session *s, char const *root);
SDL_bool init_game(session *s, game_rules *r, game_state *g, char const *root);

/* high level game */
void process_event(SDL_Event const *ev, game_event *r);
void process_keystate(unsigned char const *ks, game_event *r);
void update_gamestate(session const *s, game_rules const *gr, game_state *gs, game_event const *ev);
void tick_animation(struct animation_state *as, struct animation_rule const *ar);
void load_state(struct entity_state *es, struct animation_rule const *ar);
void render(session const *s, game_state const *gs);
void clear_event(game_event *ev);

SDL_bool collides_with_terrain(SDL_Rect const *r, SDL_Surface const *t);
SDL_bool stands_on_terrain(SDL_Rect const *r, SDL_Surface const *t);
void player_rect(SDL_Rect const *pos, SDL_Rect *r);

/* low level interactions */
char const *get_asset(json_t *a, char const *k);
SDL_bool get_int_field(json_t *o, char const *s, int *r);
SDL_Texture *load_texture(SDL_Renderer *r, char const *file);
SDL_Texture *load_asset_tex(json_t *a, char const *d, SDL_Renderer *r, char const *k);
SDL_Surface *load_asset_surf(json_t *a, char const *d, char const *k);
SDL_bool load_entity_resource(json_t *src, SDL_Texture **t, SDL_Renderer *r, struct entity_rule *er, char const *root);
void load_anim(json_t *src, char const *key, struct animation_rule *a);
void draw_background(SDL_Renderer *r, SDL_Texture *bg, int x, int y);
void draw_player(SDL_Renderer *r, SDL_Texture *pl, unsigned int frame, SDL_bool flip);
unsigned getpixel(SDL_Surface const *s, int x, int y);

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

		render(&s, &gs);
		SDL_Delay(TICK / 4);
	}

	SDL_Quit();

	return 0;
}

/* high level init */
SDL_bool init_session(session *s, char const *root)
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
	sprintf(asset_dir, "%s/%s/%s", root, "conf", "assets.json");

	json_t *assets;
	assets = json_load_file(asset_dir, 0, 0);

	sprintf(asset_dir, "%s/%s", root, "assets");

	/*
	s->player = load_asset_tex(assets, asset_dir, s->r, "player");
	if (!s->player) { return SDL_FALSE; }
	*/

	s->background = load_asset_tex(assets, asset_dir, s->r, "background");
	if (!s->background) { return SDL_FALSE; }

	s->collision = load_asset_surf(assets, asset_dir, "collision");
	if (!s->collision) { return SDL_FALSE; }

	json_decref(assets);

	return SDL_TRUE;
}

SDL_bool init_game(session *s, game_rules *gr, game_state *gs, char const *root)
{
	json_t *game, *player, *start;

	char buf[500];
	sprintf(buf, "%s/%s/%s", root, "conf", "game.json");
	game = json_load_file(buf, 0, 0);

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

	gr->start_x = json_integer_value(json_array_get(start, 0));
	gr->start_y = json_integer_value(json_array_get(start, 1));

	SDL_bool ok;
	ok = get_int_field(player, "walk_dist", &gr->walk_dist);
	if (!ok) { return SDL_FALSE; }
	ok = get_int_field(player, "jump_dist", &gr->jump_dist);
	if (!ok) { return SDL_FALSE; }
	ok = get_int_field(player, "jump_time", &gr->jump_time);
	if (!ok) { return SDL_FALSE; }
	ok = get_int_field(player, "fall_dist", &gr->fall_dist);
	if (!ok) { return SDL_FALSE; }

        ok = load_entity_resource(player, &s->player, s->r, &gr->player, root);
	if (!ok) {
		fprintf(stderr, "no player resource\n");
		return SDL_FALSE;
	}

	json_decref(game);

	gs->player.dir = DIR_LEFT;
	gs->player.st = ST_IDLE;
	gs->player.pos.x = gr->start_x;
	gs->player.pos.y = gr->start_y;
	gs->run = SDL_TRUE;
	gs->player.anim.frame = 0;
	gs->player.anim.remaining = 1;
	gs->player.jump_timeout = 0;

	return SDL_TRUE;
}


/* high level game */
void process_event(SDL_Event const *ev, game_event *r)
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

void process_keystate(unsigned char const *ks, game_event *r)
{
	if (ks[SDL_SCANCODE_LEFT]) { r->player.move_left = SDL_TRUE; }
	if (ks[SDL_SCANCODE_RIGHT]) { r->player.move_right = SDL_TRUE; }
}

void update_gamestate(session const *s, game_rules const *gr, game_state *gs, game_event const *ev)
{
	tick_animation(&gs->player.anim, &gr->player.anim[gs->player.st]);

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
	if (!stands_on_terrain(&r, s->collision)) {

		gs->player.st = ST_FALL;

	} else if (gs->player.jump_timeout > 0) {

		gs->player.st = ST_JUMP;

	} else if (ev->player.move_left || ev->player.move_right) {

		gs->player.st = ST_WALK;
	} else {

		gs->player.st = ST_IDLE;
	}

	if (old_state != gs->player.st) {
		load_state(&gs->player, &gr->player.anim[gs->player.st]);
	}

	if (ev->reset) {
		gs->player.pos.x = gr->start_x;
		gs->player.pos.y = gr->start_y;
	}

	if (ev->exit) {
		gs->run = SDL_FALSE;
	}
}

void tick_animation(struct animation_state *as, struct animation_rule const *ar)
{
	as->remaining -= 1;
	if (as->remaining < 0) {
		as->pos = (as->pos + 1) % ar->len;
		as->frame = ar->frames[as->pos];
		as->remaining = ar->duration[as->pos];
	}
}

void load_state(struct entity_state *es, struct animation_rule const *ar)
{
	es->anim.pos = 0;
	es->anim.frame = ar->frames[0];
	es->anim.remaining = ar->duration[0];
	es->pos.w = ar->box.w;
	es->pos.h = ar->box.h;
}

void render(session const *s, game_state const *gs)
{
	SDL_RenderClear(s->r);

	draw_background(s->r, s->background, gs->player.pos.x, gs->player.pos.y);
	draw_player(s->r, s->player, gs->player.anim.frame, gs->player.dir == DIR_RIGHT);

	SDL_RenderPresent(s->r);
}

void clear_event(game_event *ev)
{
	ev->player.move_left = SDL_FALSE;
	ev->player.move_right = SDL_FALSE;
	ev->player.move_jump = SDL_FALSE;
	ev->exit = SDL_FALSE;
	ev->reset =SDL_FALSE;
}

SDL_bool collides_with_terrain(SDL_Rect const *r, SDL_Surface const *t)
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

SDL_bool stands_on_terrain(SDL_Rect const *r, SDL_Surface const *t)
{
	int x = r->x + r->w / 2;

	return getpixel(t, x, r->y + r->h + 1) == 0;
}

void player_rect(SDL_Rect const *pos, SDL_Rect *r)
{
	r->x = pos->x + 400 - 9;
	r->y = pos->y + 300 - 25;
	r->w = /*pos->w*/ 18;
	r->h = /*pos->h*/ 55;
}

/* low level interactions */
SDL_Texture *load_texture(SDL_Renderer *r, char const *file)
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

void draw_background(SDL_Renderer *r, SDL_Texture *bg, int x, int y)
{
	SDL_Rect src = { x: x, y: y, w: 800, h: 600 };

	SDL_RenderCopy(r, bg, &src, 0);
}

void draw_player(SDL_Renderer *r, SDL_Texture *pl, unsigned int frame, SDL_bool flip)
{
	SDL_Rect src = { x: frame * 32, y: 0, w: 32, h: 64 };
	/*SDL_Rect dst = { x: 400 - 16, y: 300 - 32, w: 32, h: 64 };*/
	SDL_Rect dst = { 400 - 16, 300 - 32, 32, 64 };

	SDL_RenderCopyEx(r, pl, &src, &dst, 0, 0, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

char const *get_asset(json_t *a, char const *k)
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

SDL_bool get_int_field(json_t *o, char const *s, int *r)
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

SDL_Texture *load_asset_tex(json_t *a, char const *d, SDL_Renderer *r, char const *k)
{
	char const *f;
	char p[500];

	f = get_asset(a, k);
	if (!f) { return 0; }

	sprintf(p, "%s/%s", d, f);

	return load_texture(r, p);
}

SDL_Surface *load_asset_surf(json_t *a, char const *d, char const *k)
{
	char const *f;
	char p[500];

	f = get_asset(a, k);
	if (!f) { return 0; }

	sprintf(p, "%s/%s", d, f);

	return IMG_Load(p);
}

SDL_bool load_entity_resource(json_t *src, SDL_Texture **t, SDL_Renderer *r, struct entity_rule *er, char const *root)
{
	json_t *o;
	char const *ps;
	o = json_object_get(src, "resource");
	ps = json_string_value(o);
	char buf[500];
	sprintf(buf, "%s/%s/%s", root, "conf", ps);

	json_error_t e;
	o = json_load_file(buf, 0, &e);

	sprintf(buf, "%s/%s", root, "assets");
	*t = load_asset_tex(o, buf, r, "asset");
	if (!t) { return SDL_FALSE; }

	int i;
	for (i = 0; i < NSTATES; i++) {
		load_anim(o, st_names[i], &er->anim[i]);
	}

	json_decref(o);

	return SDL_TRUE;
}

void load_anim(json_t *src, char const *key, struct animation_rule *a)
{
	json_t *o, *frames, *dur, *box;
	o = json_object_get(src, key);
	if (!o) {
		fprintf(stderr, "warning: no %s animation\n", key);
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

unsigned getpixel(SDL_Surface const *s, int x, int y)
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
