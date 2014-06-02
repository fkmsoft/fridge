#include <stdio.h>

#include <json.h>

#include <SDL.h>
#include <SDL_image.h>

#define TICK 150

enum entity_state { ST_IDLE, ST_WALK, ST_FALL };
enum dir { DIR_LEFT, DIR_RIGHT };

struct session {
	SDL_Renderer *r;
	SDL_Texture *player;
};

struct game_state {
	enum dir player_dir;
	enum entity_state player_state;
	int player_x;
	SDL_bool run;
	int animation_frame;
};

struct game_event {
	SDL_bool player_turned;
	enum dir player_dir;
	SDL_bool player_moved;
	enum entity_state player_state;
	SDL_bool exit;
};

typedef struct session session;
typedef struct game_state game_state;
typedef struct game_event game_event;

SDL_bool init_session(session *s);
void init_game(game_state *g);
void draw_player(SDL_Renderer *rend, SDL_Texture *pl, unsigned int frame, int x, SDL_bool flip);

void process_event(SDL_Event *ev, game_event *r);
void update_gamestate(game_state *gs, game_event *ev);
void render(session *s, game_state *gs);
void clear_event(game_event *ev);

int main(void) {
	SDL_bool ok;
	struct session s;
	ok = init_session(&s);
	if (!ok) {
		return 1;
	}

	json_object *obj;
	obj = json_object_from_file("game.json");
	printf("%s\n", json_object_to_json_string(obj));
	json_object_put(obj);

	struct game_event ge;
	clear_event(&ge);

	struct game_state gs;
	init_game(&gs);

	unsigned ticks;
	unsigned old_ticks = SDL_GetTicks();

	int have_ev;
	SDL_Event event;
	while (gs.run) {
		have_ev = SDL_WaitEventTimeout(&event, TICK / 4);
		if (have_ev) {
			process_event(&event, &ge);
		}

		ticks = SDL_GetTicks();
		if (ticks - old_ticks >= TICK) {
			update_gamestate(&gs, &ge);
			clear_event(&ge);
			/*printf("%d\n", ticks - old_ticks);*/
			old_ticks = ticks;
		}

		render(&s, &gs);
	}

	SDL_Quit();

	return 0;
}

SDL_bool init_session(session *s)
{
	SDL_Window *window;
	int w = 800;
	int h = 600;
	int i;

	i = SDL_Init(SDL_INIT_VIDEO);
	if (i < 0) {
		return SDL_FALSE;
	}

	window = SDL_CreateWindow("Fridge Filler", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, 0);
	if (!window) {
		fprintf(stderr, "Could not init video: %s\n", SDL_GetError());
		return SDL_FALSE;
	}

	/*SDL_SetWindowIcon(window, temp);*/
	s->r = SDL_CreateRenderer(window, -1, 0);

	SDL_Surface *tmp;

	char const *file = "../assets/mc_idle.gif";
	tmp = IMG_Load(file);
	if (!tmp) {
		fprintf(stderr, "Could not load image `%s': %s\n", file, IMG_GetError());
		return SDL_FALSE;
	}
	s->player = SDL_CreateTextureFromSurface(s->r, tmp);
	SDL_FreeSurface(tmp);

	return SDL_TRUE;
}

void init_game(game_state *g)
{
	g->player_dir = DIR_LEFT;
	g->player_state = ST_IDLE;
	g->player_x = 100;
	g->run = SDL_TRUE;
	g->animation_frame = 0;
}

void draw_player(SDL_Renderer *rend, SDL_Texture *pl, unsigned int frame, int x, SDL_bool flip)
{
	SDL_Rect r = { x: x, y: 500, w: 32, h: 64 };
	SDL_Rect s = { x: frame * 32, y: 0, w: 32, h: 64 };

	SDL_RenderCopyEx(rend, pl, &s, &r, 0, 0, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

void process_event(SDL_Event *ev, game_event *r)
{
	int key = ev->key.keysym.sym;

	switch (ev->type) {
	case SDL_QUIT:
		r->exit = SDL_TRUE;
		break;
	case SDL_KEYUP:
		if (key == SDLK_q) {
			r->exit = SDL_TRUE;
		}
		r->player_moved = SDL_TRUE;
		r->player_state = ST_IDLE;
		break;
	case SDL_KEYDOWN:
		switch(key) {
		case SDLK_LEFT:
			r->player_moved = SDL_TRUE;
			r->player_turned = SDL_TRUE;
			r->player_state = ST_WALK;
			r->player_dir = DIR_LEFT;
			break;
		case SDLK_RIGHT:
			r->player_moved = SDL_TRUE;
			r->player_turned = SDL_TRUE;
			r->player_state = ST_WALK;
			r->player_dir = DIR_RIGHT;
			break;
		default:
			r->player_moved = SDL_TRUE;
			r->player_state = ST_IDLE;
		}
		break;
	}
}

void update_gamestate(game_state *gs, game_event *ev)
{
	gs->animation_frame = (gs->animation_frame + 1) % 2;

	if (ev->player_moved) {
		gs->player_state = ev->player_state;
	}

	if (ev->player_turned) {
		gs->player_dir = ev->player_dir;
	}

	if (gs->player_state == ST_WALK) {
		gs->player_x += (gs->player_dir == DIR_LEFT ? -1 : 1) * 4;
	}

	if (ev->exit) {
		gs->run = SDL_FALSE;
	}
}

void render(session *s, game_state *gs)
{
		SDL_RenderClear(s->r);

		draw_player(s->r, s->player, gs->player_state * 2 + gs->animation_frame, gs->player_x, gs->player_dir == DIR_RIGHT);

		SDL_RenderPresent(s->r);
}

void clear_event(game_event *ev)
{
	ev->player_turned = SDL_FALSE;
	ev->player_moved = SDL_FALSE;
	ev->exit = SDL_FALSE;
}
