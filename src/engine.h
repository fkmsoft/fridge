#include <stdarg.h>

#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>

#include <jansson.h>

#define MAX_PATH 500
#define CONF_DIR  "conf"
#define ASSET_DIR "assets"

#define streq(s1, s2) !strcmp(s1, s2)
#define between(x, y1, y2) (x >= y1 && x <= y2)

enum dir { DIR_LEFT = -1, DIR_RIGHT = 1 };
enum hit { HIT_NONE = 0, HIT_TOP = 1, HIT_LEFT = 1 << 1, HIT_RIGHT = 1 << 2, HIT_BOT = 1 << 3 };
enum state { ST_IDLE, ST_WALK, ST_FALL, ST_JUMP, ST_HANG, NSTATES };
static char const * const st_names[] = { "idle", "walk", "fall", "jump", "hang" };

typedef struct {
	int p;
	int a;
	int b;
} line;

typedef struct {
	SDL_Texture *background;
	SDL_Rect dim;
	int nvertical;
	int nhorizontal;
	line *vertical;
	line *horizontal;
} level;

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
json_t *load_entities(char const *root, char const *file, SDL_Renderer *r, SDL_Texture ***textures, entity_rule **rules);
SDL_bool load_entity_resource(json_t *src, char const *n, SDL_Texture **t, SDL_Renderer *r, entity_rule *er, char const *root);
void load_state(entity_state *es);
void init_entity_state(entity_state *es, entity_rule const *er, SDL_Texture *t, enum state st);
void clear_debug(debug_state *d);

/* teardown */
void destroy_level(level *l);

/* state updates */
void clear_order(entity_event *o);
void tick_animation(entity_state *as);
int kick_entity(entity_state *e, enum hit h, SDL_Point const *v);

/* movement */
void keystate_to_movement(unsigned char const *ks, entity_event *e);
void move_entity(entity_state *e, entity_event const *ev, level const *lvl, move_log *mlog);

/* collision */
enum hit collides_with_terrain(SDL_Rect const *r, level const *lev);
SDL_bool stands_on_terrain(SDL_Rect const *r, level const *t);
void entity_hitbox(entity_state const *s, SDL_Rect *box);
int cmp_lines(void const *x, void const *y);
SDL_Point entity_feet(SDL_Rect const *r);

/* rendering */
void draw_background(SDL_Renderer *r, SDL_Texture *bg, SDL_Rect const *screen);
void draw_terrain_lines(SDL_Renderer *r, level const *lev, SDL_Rect const *screen);
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
