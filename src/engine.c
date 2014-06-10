#include "engine.h"

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

