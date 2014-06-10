#include "engine.h"

/* loading */
void load_anim(json_t *src, char const *name, char const *key, animation_rule *a)
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

SDL_Texture *load_asset_tex(json_t *a, char const *root, SDL_Renderer *r, char const *k)
{
	char const *f, *p;

	f = get_asset(a, k);
	if (!f) { return 0; }

	p = set_path("%s/%s/%s", root, ASSET_DIR, f);

	return load_texture(r, p);
}

void load_entity_rule(json_t *src, entity_rule *er, char const *n)
{
	get_int_field(src, n, "walk-dist", &er->walk_dist);
	get_int_field(src, n, "jump-dist-y", &er->jump_dist_y);
	get_int_field(src, n, "jump-dist-x", &er->jump_dist_x);
	get_int_field(src, n, "jump-time", &er->jump_time);
	get_int_field(src, n, "fall-dist", &er->fall_dist);
	get_float_field(src, n, "wide-jump-factor", &er->a_wide);
	get_float_field(src, n, "high-jump-factor", &er->a_high);
	json_t *o = json_object_get(src, "has-gravity");
        if (o || !streq(n, "custom-rule")) {
                er->has_gravity = o ? streq(json_string_value(json_object_get(src, "has-gravity")), "yes") : SDL_TRUE;
        }
}

SDL_bool load_entity_resource(json_t *src, char const *n, SDL_Texture **t, SDL_Renderer *r, entity_rule *er, char const *root)
{
	json_t *o;
	char const *ps;
	o = json_object_get(src, "resource");
	ps = json_string_value(o);
	char const *path;
	path = set_path("%s/%s/%s", root, CONF_DIR, ps);

        load_entity_rule(src, er, n);

	json_error_t e;
	o = json_load_file(path, 0, &e);
	if (*e.text != 0) {
		fprintf(stderr, "error: in %s:%d: %s\n", path, e.line, e.text);
		return SDL_FALSE;
	}

        if (t) {
                *t = load_asset_tex(o, root, r, "asset");
                if (!t) { return SDL_FALSE; }
        }

	json_t *siz;
	siz = json_object_get(o, "frame_size");
	er->start_dim.w = json_integer_value(json_array_get(siz, 0));
	er->start_dim.h = json_integer_value(json_array_get(siz, 1));

	int i;
	for (i = 0; i < NSTATES; i++) {
		er->anim[i].frames = 0;
		load_anim(o, n, st_names[i], &er->anim[i]);
		if (!er->anim[i].frames) {
			er->anim[i] = er->anim[ST_IDLE];
		}
	}

	json_decref(o);

	return SDL_TRUE;
}

void load_state(entity_state *es)
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

void init_entity_state(entity_state *es, entity_rule const *er, SDL_Texture *t, enum state st)
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

/* state updates */
void tick_animation(entity_state *es)
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

/* collision */
void entity_hitbox(entity_state const *s, SDL_Rect *box)
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

SDL_Point entity_feet(SDL_Rect const *r)
{
	return (SDL_Point) { x: r->x + r->w / 2, y: r->y + r->h };
}

/* rendering */
void render_line(SDL_Renderer *r, char const *s, TTF_Font *font, int l)
{
	if (!font) { return; }

	SDL_Surface *text;
	SDL_Texture *tex;

	SDL_Color col = {200, 20, 7, 255}; /* red */
	text = TTF_RenderText_Blended(font, s, col);
	tex = SDL_CreateTextureFromSurface(r, text);
	SDL_FreeSurface(text);
	SDL_Rect dest = { x: 0, y: l * text->h, w: text->w, h: text->h };
	SDL_SetRenderDrawColor(r, 0, 0, 0, 180);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	SDL_RenderFillRect(r, &dest);
	SDL_RenderCopy(r, tex, 0, &dest);
}

void draw_entity(SDL_Renderer *r, SDL_Rect const *scr, entity_state const *s, debug_state const *debug)
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
			SDL_SetRenderDrawColor(r, 255, 105, 180, 255); /* pink */
			SDL_RenderDrawRect(r, &dst);
		}

		if (debug->hitboxes) {
			SDL_Rect box;
			entity_hitbox(s, &box);
			box.x -= scr->x;
			box.y -= scr->y;
			SDL_SetRenderDrawColor(r, 23, 225, 38, 255); /* lime */
			SDL_RenderDrawRect(r, &box);
		}
	}
}

/* low level json */
char const *get_asset(json_t *a, char const *k)
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

SDL_bool get_int_field(json_t *o, char const *n, char const *s, int *r)
{
	json_t *var;

	var = json_object_get(o, s);
	if (!var) {
		fprintf(stderr, "warning: no %s for %s\n", s, n);
                if (!streq(n, "custom-rule")) {
                        *r = 0;
                }
		return SDL_FALSE;
	}
	*r = json_integer_value(var);

	return SDL_TRUE;
}

SDL_bool get_float_field(json_t *o, char const *n, char const *s, double *r)
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

/* low-level SDL */
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


/* general low-level */
char const *set_path(char const *fmt, ...)
{
	static char buf[MAX_PATH];

	va_list ap;
	va_start(ap, fmt);

	vsnprintf(buf, MAX_PATH - 1, fmt, ap);
	va_end(ap);

	return buf;
}

