#include <stdarg.h>

#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>

#include <jansson.h>

#define MAX_PATH 500
#define CONF_DIR  "conf"
#define ASSET_DIR "assets"

#define streq(s1, s2) !strcmp(s1, s2)

enum dir { DIR_LEFT = -1, DIR_RIGHT = 1 };
enum state { ST_IDLE, ST_WALK, ST_FALL, ST_JUMP, ST_HANG, NSTATES };
static char const * const st_names[] = { "idle", "walk", "fall", "jump", "hang" };

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
	SDL_bool has_gravity;
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
	SDL_bool active;
	SDL_bool frames;
	SDL_bool hitboxes;
	SDL_bool pause;
	SDL_bool show_terrain_collision;
	SDL_Texture *terrain_collision;
	SDL_bool message_positions;
	TTF_Font *font;
} debug_state;

/* loading */
void load_anim(json_t *src, char const *name, char const *key, animation_rule *a);
SDL_Texture *load_asset_tex(json_t *a, char const *d, SDL_Renderer *r, char const *k);
void load_entity_rule(json_t *src, entity_rule *er, char const *n);
SDL_bool load_entity_resource(json_t *src, char const *n, SDL_Texture **t, SDL_Renderer *r, entity_rule *er, char const *root);
void load_state(entity_state *es);
void init_entity_state(entity_state *es, entity_rule const *er, SDL_Texture *t, enum state st);

/* state updates */
void tick_animation(entity_state *as);

/* collision */
void entity_hitbox(entity_state const *s, SDL_Rect *box);
SDL_Point entity_feet(SDL_Rect const *r);

/* rendering */
void render_line(SDL_Renderer *r, char const *s, TTF_Font *font, int l);
void draw_entity(SDL_Renderer *r, SDL_Rect const *scr, entity_state const *s, debug_state const *debug);

/* low-level json */
char const *get_asset(json_t *a, char const *k);
SDL_bool get_int_field(json_t *o, char const *n, char const *s, int *r);
SDL_bool get_float_field(json_t *o, char const *n, char const *s, double *r);

/* low-level SDL */
SDL_Texture *load_texture(SDL_Renderer *r, char const *file);

/* general low-level */
char const *set_path(char const *fmt, ...);
