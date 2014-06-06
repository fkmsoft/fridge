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

struct game_rules {
	int start_x;
	int start_y;
	int walk_dist;
	int jump_dist;
	int jump_time;
	int fall_dist;
};

enum dir { DIR_LEFT, DIR_RIGHT };
enum state { ST_IDLE, ST_WALK, ST_FALL, ST_JUMP, NSTATES };

struct animation_rule {
	unsigned len;
	unsigned *frames;
	unsigned *duration;
	SDL_Rect box;
};

struct entity_rule {
	struct {
		int x;
		int y;
	} start;
	struct animation_rule anim[NSTATES];
};

struct entity_state {
	SDL_Rect pos;
	enum dir dir;
	enum state st;
	int jump_timeout;
	struct {
		int frame;
		int remaining;
	} anim;
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
void render(session const *s, game_state const *gs);
void clear_event(game_event *ev);

SDL_bool collides_with_terrain(SDL_Rect const *r, SDL_Surface const *t);
SDL_bool stands_on_terrain(SDL_Rect const *r, SDL_Surface const *t);
void player_rect(int x, int y, SDL_Rect *r);

/* low level interactions */
char const *get_asset(json_t *a, char const *k);
SDL_bool get_int_field(json_t *o, char const *s, int *r);
SDL_Texture *load_texture(SDL_Renderer *r, char const *file);
SDL_Texture *load_asset_tex(json_t *a, char const *d, SDL_Renderer *r, char const *k);
SDL_Surface *load_asset_surf(json_t *a, char const *d, char const *k);
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

SDL_bool init_game(session *s, game_rules *r, game_state *g, char const *root)
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

	r->start_x = json_integer_value(json_array_get(start, 0));
	r->start_y = json_integer_value(json_array_get(start, 1));

	SDL_bool ok;
	ok = get_int_field(player, "walk_dist", &r->walk_dist);
	if (!ok) { return SDL_FALSE; }
	ok = get_int_field(player, "jump_dist", &r->jump_dist);
	if (!ok) { return SDL_FALSE; }
	ok = get_int_field(player, "jump_time", &r->jump_time);
	if (!ok) { return SDL_FALSE; }
	ok = get_int_field(player, "fall_dist", &r->fall_dist);
	if (!ok) { return SDL_FALSE; }

	json_t *po;
	char const *ps;
	po = json_object_get(player, "resource");
	ps = json_string_value(po);
	sprintf(buf, "%s/%s/%s", root, "conf", ps);

	json_error_t e;
	po = json_load_file(buf, 0, &e);

	sprintf(buf, "%s/%s", root, "assets");
	s->player = load_asset_tex(po, buf, s->r, "asset");
	if (!s->player) { return SDL_FALSE; }

	json_decref(po);

	json_decref(game);

	g->player.dir = DIR_LEFT;
	g->player.st = ST_IDLE;
	g->player.pos.x = r->start_x;
	g->player.pos.y = r->start_y;
	g->run = SDL_TRUE;
	g->player.anim.frame = 0;
	g->player.anim.remaining = 1;
	g->player.jump_timeout = 0;

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
	gs->player.anim.frame = (gs->player.anim.frame + 1) % 2;

	gs->player.st = gs->player.jump_timeout > 0 ? ST_FALL : ST_IDLE;

	if (ev->player.move_left || ev->player.move_right) {
		gs->player.st = ST_WALK;
		gs->player.dir = ev->player.move_left ? DIR_LEFT : DIR_RIGHT;
	}

	int old_x = gs->player.pos.x;
	int dir = (gs->player.dir == DIR_LEFT) ? -1 : 1;
	if (gs->player.st == ST_WALK) {
		gs->player.pos.x += dir * gr->walk_dist;
	}

	SDL_Rect r;
	player_rect(gs->player.pos.x, gs->player.pos.y, &r);

	if (collides_with_terrain(&r, s->collision)) {
		gs->player.pos.x = old_x;
		player_rect(gs->player.pos.x, gs->player.pos.y, &r);
		/*
		do {
			gs->player_x += dir;
			r->x += dir;
		} while (!collides_with_terrain(&r, s->collision));
		*/
	}

	if (ev->player.move_jump && gs->player.st != ST_FALL) {
		gs->player.jump_timeout = gr->jump_time;
	}

	if (gs->player.jump_timeout > 0) {
		int old_y = gs->player.pos.y;
		gs->player.st = ST_FALL;
		gs->player.jump_timeout -= 1;
		gs->player.pos.y -= gr->jump_dist;
		gs->player.anim.frame = 1;

		player_rect(gs->player.pos.x, gs->player.pos.y, &r);
		if (collides_with_terrain(&r, s->collision)) {
			gs->player.pos.y = old_y;
			gs->player.jump_timeout = 0;
		}
	} else {
		player_rect(gs->player.pos.x, gs->player.pos.y, &r);
		int f;
		for (f = gr->jump_dist; !stands_on_terrain(&r, s->collision) && f > 0; f--) {
			gs->player.st = ST_FALL;
			gs->player.anim.frame = 0;
			r.y += 1;
			gs->player.pos.y += 1;
		}
	}

	if (ev->reset) {
		gs->player.pos.x = gr->start_x;
		gs->player.pos.y = gr->start_y;
	}

	if (ev->exit) {
		gs->run = SDL_FALSE;
	}
}

void render(session const *s, game_state const *gs)
{
	SDL_RenderClear(s->r);

	draw_background(s->r, s->background, gs->player.pos.x, gs->player.pos.y);
	draw_player(s->r, s->player, gs->player.st * 2 + gs->player.anim.frame, gs->player.dir == DIR_RIGHT);

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

void player_rect(int x, int y, SDL_Rect *r)
{
	r->x = x + 400 - 9;
	r->y = y + 300 - 25;
	r->w = 18;
	r->h = 55;
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
