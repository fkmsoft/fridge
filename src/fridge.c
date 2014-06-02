#include <stdio.h>

#include <json.h>

#include <SDL.h>
#include <SDL_image.h>

#define TICK 80
#define ROOTVAR "FRIDGE_ROOT"

enum entity_state { ST_IDLE, ST_WALK, ST_FALL };
enum dir { DIR_LEFT, DIR_RIGHT };

struct session {
	SDL_Renderer *r;
	SDL_Texture *player;
	SDL_Texture *background;
	int w;
	int h;
};

struct game_state {
	enum dir player_dir;
	enum entity_state player_state;
	int player_x;
	int player_y;
	SDL_bool run;
	int animation_frame;
	int jump_timeout;
};

struct game_event {
	SDL_bool move_left;
	SDL_bool move_right;
	SDL_bool move_jump;
	SDL_bool exit;
};

typedef struct session session;
typedef struct game_state game_state;
typedef struct game_event game_event;

/* high level init */
SDL_bool init_session(session *s, char const *root);
SDL_bool init_game(game_state *g, char const *root);

/* high level game */
void process_event(SDL_Event *ev, game_event *r);
void process_keystate(Uint8 const *ks, game_event *r);
void update_gamestate(game_state *gs, game_event *ev);
void render(session *s, game_state *gs);
void clear_event(game_event *ev);

/* low level interactions */
char const *get_asset(json_object *a, char const *k);
SDL_Texture *load_texture(SDL_Renderer *r, char const *file);
SDL_Texture *load_asset(json_object *a, char const *d, SDL_Renderer *r, char const *k);
void draw_background(SDL_Renderer *r, SDL_Texture *bg, int x, int y);
void draw_player(SDL_Renderer *r, SDL_Texture *pl, unsigned int frame, SDL_bool flip);

int main(void) {
	SDL_bool ok;
	char const *root = getenv(ROOTVAR);

	session s;
	ok = init_session(&s, root);
	if (!ok) { return 1; }

	game_state gs;
	ok = init_game(&gs, root);
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

		Uint8 const *keystate = SDL_GetKeyboardState(0);
		process_keystate(keystate, &ge);

		ticks = SDL_GetTicks();
		if (ticks - old_ticks >= TICK) {
			update_gamestate(&gs, &ge);
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
	sprintf(asset_dir, "%s/%s", root, "assets.json");

	json_object *assets;
	assets = json_object_from_file(asset_dir);

	sprintf(asset_dir, "%s/%s", root, "assets");

	s->player = load_asset(assets, asset_dir, s->r, "player");
	if (!s->player) { return SDL_FALSE; }

	s->background = load_asset(assets, asset_dir, s->r, "background");
	if (!s->background) { return SDL_FALSE; }

	json_object_put(assets);

	return SDL_TRUE;
}

SDL_bool init_game(game_state *g, char const *root)
{
	json_object *game, *start;
	json_bool ok;

	char buf[500];
	sprintf(buf, "%s/%s", root, "game.json");
	game = json_object_from_file(buf);

	g->player_dir = DIR_LEFT;
	g->player_state = ST_IDLE;
	ok = json_object_object_get_ex(game, "start", &start);
	if (!ok) {
		puts("no start");
		return;
	}
	g->player_x = json_object_get_int(json_object_array_get_idx(start, 0));
	g->player_y = json_object_get_int(json_object_array_get_idx(start, 1));
	g->run = SDL_TRUE;
	g->animation_frame = 0;
	g->jump_timeout = 0;
}


/* high level game */
void process_event(SDL_Event *ev, game_event *r)
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
			r->move_jump = SDL_TRUE;
			break;
		}
		break;
	}
}

void process_keystate(Uint8 const *ks, game_event *r)
{
	if (ks[SDL_SCANCODE_LEFT]) { r->move_left = SDL_TRUE; }
	if (ks[SDL_SCANCODE_RIGHT]) { r->move_right = SDL_TRUE; }
}

void update_gamestate(game_state *gs, game_event *ev)
{
	gs->animation_frame = (gs->animation_frame + 1) % 2;

	gs->player_state = gs->jump_timeout > 0 ? ST_FALL : ST_IDLE;

	if (ev->move_left || ev->move_right) {
		gs->player_state = ST_WALK;
		gs->player_dir = ev->move_left ? DIR_LEFT : DIR_RIGHT;
	}

	if (gs->player_state == ST_WALK) {
		gs->player_x += (gs->player_dir == DIR_LEFT ? -1 : 1) * 4;
	}

	if (ev->move_jump) {
		gs->jump_timeout = 9;
	}

	if (gs->jump_timeout > 0) {
		gs->player_state = ST_FALL;
		gs->jump_timeout -= 1;
		gs->player_y -= 10;
		gs->animation_frame = 1;
	}

	if (ev->exit) {
		gs->run = SDL_FALSE;
	}
}

void render(session *s, game_state *gs)
{
	SDL_RenderClear(s->r);

	draw_background(s->r, s->background, gs->player_x, gs->player_y);
	draw_player(s->r, s->player, gs->player_state * 2 + gs->animation_frame, gs->player_dir == DIR_RIGHT);

	SDL_RenderPresent(s->r);
}

void clear_event(game_event *ev)
{
	ev->move_left = SDL_FALSE;
	ev->move_right = SDL_FALSE;
	ev->move_jump = SDL_FALSE;
	ev->exit = SDL_FALSE;
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
	SDL_Rect dst = { x: 400 - 16, y: 300 - 32, w: 32, h: 64 };

	SDL_RenderCopyEx(r, pl, &src, &dst, 0, 0, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

char const *get_asset(json_object *a, char const *k)
{
	json_object *str;
	json_bool ok;

	ok = json_object_object_get_ex(a, k, &str);

	return ok ? json_object_get_string(str) : 0;
}

SDL_Texture *load_asset(json_object *a, char const *d, SDL_Renderer *r, char const *k)
{
	char const *f;
	char p[500];

	f = get_asset(a, k);
	if (!f) { return 0; }

	sprintf(p, "%s/%s", d, f);

	return load_texture(r, p);
}
