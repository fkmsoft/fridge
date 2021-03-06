#include "engine.h"

SDL_bool pt_on_line(SDL_Point const *p, line const *l);
static enum hit intersects_x(line const *l, SDL_Rect const *r);
static enum hit intersects_y(line const *l, SDL_Rect const *r);

static int entity_jump(entity_state *e, level const *terrain, SDL_bool walk, SDL_bool jump);

/* loading */
void load_anim(json_t *src, char const *name, char const *key, animation_rule *a)
{
	json_t *o, *frames, *dur, *box;
	o = json_object_get(src, key);
	if (!o) {
		#ifdef VERBOSE
		fprintf(stderr, "Warning: No %s animation for %s\n", key, name);
		#endif
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


json_t *load_entities(char const *root, char const *file, SDL_Renderer *r, SDL_Texture ***textures, entity_rule **rules)
{
	json_t *ent;
	json_error_t e;
	ent = json_load_file(file, 0, &e);
	if (*e.text != 0) {
		fprintf(stderr, "Error at %s:%d: %s\n", file, e.line, e.text);
		return 0;
	}

	int k;
	k = json_object_size(ent);
	*textures = malloc(sizeof(SDL_Texture *) * k);
	*rules = malloc(sizeof(entity_rule) * k);

	json_t *o;
	int i = 0;
	SDL_bool ok;
	char const *name;
	entity_rule *rule = *rules;
	SDL_Texture **tex = *textures;
	json_object_foreach(ent, name, o) {

		ok = load_entity_resource(o, name, tex, r, rule, root);
		if (!ok) {
			return 0;
		}

		/* save the index in the buffer, so the items in groups can
		 * link directly to their rules and textures later: */
		json_object_set_new(o, "index", json_integer(i));
		i += 1;
		rule += 1;
		tex += 1;
	}

	return ent;
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
		fprintf(stderr, "Error at %s:%d: %s\n", path, e.line, e.text);
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

void clear_debug(debug_state *d)
{
	d->active = SDL_FALSE;
	d->pause = SDL_FALSE;
	d->frames = SDL_TRUE;
	d->hitboxes = SDL_TRUE;
	d->show_terrain_collision = SDL_FALSE;
	d->message_positions = SDL_TRUE;
}

/* teardown */
void destroy_level(level *l)
{
	free(l->vertical);
	free(l->horizontal);
}

/* state updates */
void clear_order(entity_event *o)
{
	o->move_left = SDL_FALSE;
	o->move_right = SDL_FALSE;
	o->move_jump = SDL_FALSE;
	o->walk = SDL_FALSE;
}

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

int kick_entity(entity_state *e, enum hit h, SDL_Point const *v)
{
	if (h == HIT_NONE || h & HIT_TOP) { return 0; }
	if ((h & HIT_RIGHT && h & HIT_LEFT)) { return 0; }

	e->pos.x += v->x * (h & HIT_RIGHT ? -1 : 1);
	e->pos.y += v->y;

	return 0;
}

/* movement */
static SDL_Point entity_vector_move(entity_state *e, SDL_Point const *v, level const *terrain, SDL_bool grav)
{
	SDL_Rect r, n;
	entity_hitbox(e, &r);
	entity_hitbox(e, &n);

	int dirx = v->x < 0 ? -1 : 1;
	int diry = v->y < 0 ? -1 : 1;

	int vx = v->x < 0 ? -v->x : v->x;
	int vy = v->y < 0 ? -v->y : v->y;

	int v_max = vx > vy ? vx : vy;
	int i;
	SDL_Point out = { x: 0, y: 0 };
	enum hit h;

	for (i = 1; i < v_max + 1; i++) {
		int dx = dirx * (i * vx) / v_max;
		int dy = diry * (i * vy) / v_max;
		r.x = n.x + dx;
		r.y = n.y + dy;
		h = collides_with_terrain(&r, terrain);
		if (h != HIT_NONE) { break; }
		if (grav && !stands_on_terrain(&r, terrain)) {
			break; // or kick
			r.y += 1;
			h = collides_with_terrain(&r, terrain);
			SDL_Point v = { .x = e->hitbox.w / 2, .y = e->hitbox.h / 5 };
			kick_entity(e, h, &v);
		}
		out.x = dx;
		out.y = dy;
	}

	e->pos.x += out.x;
	e->pos.y += out.y;

	return out;
}

static int entity_walk(entity_state *e, level const *terrain)
{
	SDL_Point v = { x: e->dir * e->rule->walk_dist, y: 0 };
	SDL_Point r = entity_vector_move(e, &v, terrain, e->rule->has_gravity);
	return r.x < 0 ? -r.x : r.x;
}

static int entity_start_jump(entity_state *e, level const *terrain, enum jump_type t)
{
	e->jump_timeout = e->rule->jump_time;
	e->jump_type = t;

	return entity_jump(e, terrain, t == JUMP_WIDE, SDL_TRUE);
}

static int entity_jump(entity_state *e, level const *terrain, SDL_bool walk, SDL_bool jump)
{
	entity_rule const *r = e->rule;
	if (e->jump_timeout == 0) {
		return r->has_gravity ? 0 : jump ? entity_start_jump(e, terrain, JUMP_HIGH) : 0;
	}

	SDL_Point v = { x: e->jump_type == JUMP_WIDE ? e->dir * r->jump_dist_x : r->has_gravity ? 0 : walk ? e->dir * r->walk_dist : 0,
	                y: r->jump_dist_y };
	if (r->has_gravity) { v.y += e->jump_timeout; }
	v.y *= -1;
	SDL_Point w = entity_vector_move(e, &v, terrain, SDL_FALSE);
	if (w.y != v.y) {
		e->jump_timeout = 0;
	} else {
		e->jump_timeout -= 1;
	}
	return -w.y;
}

static int entity_fall(entity_state *e, level const *terrain, SDL_bool walk)
{
	e->fall_time += 1;
	SDL_Point v, w;
	if (e->rule->has_gravity) {
		v = (SDL_Point) { x: 0, y: 1 };
		w = entity_vector_move(e, &v, terrain, SDL_FALSE);
		if (v.y != w.y) { e->fall_time = 0; return 0; }
	}

	entity_rule const *r = e->rule;
	v = (SDL_Point) { x: walk ? e->dir * r->walk_dist : 0,
	                  y: r->fall_dist };
	if (r->has_gravity) { v.y += e->fall_time; }
	w = entity_vector_move(e, &v, terrain, SDL_FALSE);
	if (v.y != w.y) { e->fall_time = 0; }
	return w.y;
}

void keystate_to_movement(unsigned char const *ks, entity_event *e)
{
	if (ks[SDL_SCANCODE_LEFT ]) { e->move_left  = SDL_TRUE; e->walk = SDL_TRUE; }
	if (ks[SDL_SCANCODE_RIGHT]) { e->move_right = SDL_TRUE; e->walk = SDL_TRUE; }
	if (ks[SDL_SCANCODE_SPACE]) { e->move_jump = SDL_TRUE; }
}

void move_entity(entity_state *e, entity_event const *ev, level const *lvl, move_log *mlog)
{
	enum state st_begin = e->st;
	*mlog = (move_log) { walked: 0, jumped: 0, fallen: 0, turned: SDL_FALSE, hang: SDL_FALSE };

	switch (st_begin) {
	case ST_IDLE:
	case ST_WALK:
		e->dir = ev->move_left ? DIR_LEFT : ev->move_right ? DIR_RIGHT : e->dir;
		if (ev->move_jump) {
			mlog->jumped = entity_start_jump(e, lvl, ev->walk ? e->rule->has_gravity ? JUMP_WIDE : JUMP_HIGH : JUMP_HIGH);
		} else if (ev->walk) {
			mlog->walked = entity_walk(e, lvl);
		}
		break;
	case ST_HANG:
		break;
	case ST_JUMP:
		e->dir = ev->move_left ? DIR_LEFT : ev->move_right ? DIR_RIGHT : e->dir;
		mlog->jumped = entity_jump(e, lvl, ev->walk, ev->move_jump);
		break;
	case ST_FALL:
		e->dir = ev->move_left ? DIR_LEFT : ev->move_right ? DIR_RIGHT : e->dir;
		if (!e->rule->has_gravity) {
			if (ev->move_jump) {
				mlog->jumped = entity_start_jump(e, lvl, JUMP_HIGH);
			} else if (ev->walk) {
				mlog->walked = entity_walk(e, lvl);
				mlog->fallen = entity_fall(e, lvl, SDL_FALSE);
			} else {
				mlog->fallen = entity_fall(e, lvl, SDL_FALSE);
			}
		} else {
			mlog->fallen = entity_fall(e, lvl, ev->walk);
			SDL_Rect h;
			entity_hitbox(e, &h);
			if (mlog->fallen == 0 && !stands_on_terrain(&h, lvl)) {
				h.y += 1;
				enum hit where = collides_with_terrain(&h, lvl);
				SDL_Point v = { .x = -1, .y = 0 };
				kick_entity(e, where, &v);
			}
		}
		break;
	case NSTATES:
		break;
	}

	SDL_Rect h;
	entity_hitbox(e, &h);
	e->st = mlog->jumped > 0 ? ST_JUMP : stands_on_terrain(&h, lvl) ? mlog->walked > 0 ? ST_WALK : ST_IDLE : ST_FALL;
}

/* collision */
static int first_idx(line const *a, int n, int x)
{
	int r, l, i;
	l = 0;
	r = n;
	while (l + 1 < r) {
		i = l + (r - l) / 2;
		if (x < a[i].p) {
			r = i - 1;
		} else if (x > a[i].p) {
			l = i + 1;
		} else {
			r = i;
		}
	}

	return l;
}

enum hit collides_with_terrain(SDL_Rect const *r, level const *lev)
{
	enum hit a = HIT_NONE;

	SDL_Rect hb = *r;
	hb.h -= 1;

	int i;
	for (i = first_idx(lev->horizontal, lev->nhorizontal, r->y); i < lev->nhorizontal && lev->horizontal[i].p <= r->y + r->h; i++) {
		a = intersects_x(&lev->horizontal[i], &hb);
		if (a != HIT_NONE) { return a; }
	}
	for (i = first_idx(lev->vertical, lev->nvertical, r->x); i < lev->nvertical && lev->vertical[i].p <= r->x + r->w; i++) {
		a = intersects_y(&lev->vertical[i], &hb);
		if (a != HIT_NONE) { return a; }
	}

	return a;
}

SDL_bool stands_on_terrain(SDL_Rect const *r, level const *t)
{
	SDL_Point mid = entity_feet(r);

	int i;
	for (i = first_idx(t->horizontal, t->nhorizontal, mid.y); i < t->nhorizontal && t->horizontal[i].p <= mid.y; i++) {
		if (pt_on_line(&mid, &t->horizontal[i])) { return SDL_TRUE; }
	}

	return SDL_FALSE;
}

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

int cmp_lines(void const *x, void const *y)
{
	line const *a = (line const *) x;
	line const *b = (line const *) y;
	if (a->p < b->p) { return -1; }
	return a->p > b->p;
}

SDL_Point entity_feet(SDL_Rect const *r)
{
	return (SDL_Point) { x: r->x + r->w / 2, y: r->y + r->h };
}

SDL_bool pt_on_line(SDL_Point const *p, line const *l)
{
	return p->y == l->p && between(p->x, l->a, l->b);
}

static enum hit intersects_x(line const *l, SDL_Rect const *r)
{
	int rx1 = r->x;
	int rxm = r->x + r->w / 2;
	int rx2 = r->x + r->w;
	int ry1 = r->y;
	int ry2 = r->y + r->h;

	if (between(l->p, ry1, ry2) &&
		(between(rx1, l->a, l->b) || (between(rxm, l->a, l->b)) ||
		 between(l->a, rx1, rxm) || between(l->b, rx1, rxm))) {
		return HIT_LEFT;
	}

	if (between(l->p, ry1, ry2) &&
		(between(rxm, l->a, l->b) || (between(rx2, l->a, l->b)) ||
		 between(l->a, rx1, rx2) || between(l->b, rx1, rx2))) {
		return HIT_RIGHT;
	}

	return HIT_NONE;
}

static enum hit intersects_y(line const *l, SDL_Rect const *r)
{
	int rx1 = r->x;
	int rxm = r->x + r->w / 2;
	int rx2 = r->x + r->w;
	int ry1 = r->y;
	int ry2 = r->y + r->h;

	if (between(l->p, rx1, rxm) &&
		(between(ry1, l->a, l->b) || (between(ry2, l->a, l->b)) ||
		 between(l->a, ry1, ry2) || between(l->b, ry1, ry2))) {
		return HIT_LEFT;
	}

	if (between(l->p, rxm, rx2) &&
		(between(ry1, l->a, l->b) || (between(ry2, l->a, l->b)) ||
		 between(l->a, ry1, ry2) || between(l->b, ry1, ry2))) {
		return HIT_RIGHT;
	}

	return HIT_NONE;
}

/* rendering */
void draw_background(SDL_Renderer *r, SDL_Texture *bg, SDL_Rect const *screen)
{
	SDL_RenderCopy(r, bg, screen, 0);
}

void draw_terrain_lines(SDL_Renderer *r, level const *lev, SDL_Rect const *screen)
{
	SDL_SetRenderDrawColor(r, 200, 20, 7, 255); /* red */
	int i;
	for (i = 0; i < lev->nhorizontal; i++) {
		SDL_RenderDrawLine(r,
				   lev->horizontal[i].a - screen->x,
				   lev->horizontal[i].p - screen->y,
				   lev->horizontal[i].b - screen->x,
				   lev->horizontal[i].p - screen->y);
	}
	for (i = 0; i < lev->nvertical; i++) {
		SDL_RenderDrawLine(r,
				   lev->vertical[i].p - screen->x,
				   lev->vertical[i].a - screen->y,
				   lev->vertical[i].p - screen->x,
				   lev->vertical[i].b - screen->y);
	}
}

void render_line(SDL_Renderer *r, char const *s, TTF_Font *font, int l)
{
	if (!font) { return; }

	SDL_Surface *text;
	SDL_Texture *tex;

	SDL_Color col = {200, 20, 7, 255}; /* red */
	text = TTF_RenderText_Blended(font, s, col);
	tex = SDL_CreateTextureFromSurface(r, text);
	SDL_Rect dest = { x: 0, y: l * text->h, w: text->w, h: text->h };
	SDL_FreeSurface(text);

	SDL_SetRenderDrawColor(r, 0, 0, 0, 180);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	SDL_RenderFillRect(r, &dest);
	SDL_RenderCopy(r, tex, 0, &dest);
	SDL_DestroyTexture(tex);
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
		#ifdef VERBOSE
		fprintf(stderr, "Warning: No %s for %s\n", s, n);
		#endif
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
		#ifdef VERBOSE
		fprintf(stderr, "Warning: No %s for %s\n", s, n);
		#endif
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
