// Language picker. Used by Tools/Language.pak and, with --first, by MinUI.pak/launch.sh on the first start
// (no <SHARED_USERDATA_PATH>/language yet). Every language is listed with its own name so it can be read
// without knowing the current language. The choice is applied when MinUI is restarted (launch.sh loop).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(USE_SDL2)
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#else
#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#endif

#include <msettings.h>

#include "defines.h"
#include "utils.h"
#include "api.h"

#define MAX_LANGS 32

int main(int argc, char* argv[]) {
	int first = (argc >= 2 && !strcmp(argv[1], "--first"));

	PWR_setCPUSpeed(CPU_SPEED_MENU);
	SDL_Surface* screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	InitSettings();

	I18N_Lang langs[MAX_LANGS];
	int count = I18N_listLanguages(langs, MAX_LANGS);

	int selected = 0;
	for (int i = 0; i < count; i++) if (!strcmp(langs[i].code, I18N_getLanguage())) selected = i;

	int rows = (screen->h - SCALE1(PADDING * 2 + PILL_SIZE * 2)) / SCALE1(PILL_SIZE);
	if (rows < 3) rows = 3;
	int start = 0;

	int quit = 0;
	int save = 0;
	int dirty = 1;
	int show_setting = 0;

	while (!quit) {
		GFX_startFrame();
		PAD_poll();

		if (PAD_justRepeated(BTN_UP) || PAD_justPressed(BTN_UP)) {
			selected = (selected - 1 + count) % count;
			dirty = 1;
		}
		else if (PAD_justRepeated(BTN_DOWN) || PAD_justPressed(BTN_DOWN)) {
			selected = (selected + 1) % count;
			dirty = 1;
		}
		else if (PAD_justPressed(BTN_A)) {
			save = 1;
			quit = 1;
		}
		else if (PAD_justPressed(BTN_B) && !first) { // the first start must end with a choice
			quit = 1;
		}

		if (selected < start) start = selected;
		if (selected >= start + rows) start = selected - rows + 1;

		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (dirty) {
			GFX_clear(screen);
			GFX_blitHardwareGroup(screen, show_setting, 0);

			SDL_Surface* title = TTF_RenderUTF8_Blended(font.large, "Language / 语言", COLOR_GOLD);
			if (title) {
				SDL_BlitSurface(title, NULL, screen, &(SDL_Rect){SCALE1(PADDING + BUTTON_PADDING), SCALE1(PADDING)});
				SDL_FreeSurface(title);
			}

			for (int i = start, j = 0; i < count && j < rows; i++, j++) {
				int y = SCALE1(PADDING + (j + 1) * PILL_SIZE);
				int avail_w = screen->w - SCALE1(PADDING * 2);
				char disp[I18N_NAME_MAX * 2];
				int tw = GFX_truncateText(font.large, langs[i].name, disp, avail_w, SCALE1(BUTTON_PADDING * 2));
				int mw = MIN(avail_w, tw);
				SDL_Color col = COLOR_WHITE;
				if (i == selected) {
					GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){SCALE1(PADDING), y, mw, SCALE1(PILL_SIZE)});
					col = COLOR_BLACK;
				}
				SDL_Surface* txt = TTF_RenderUTF8_Blended(font.large, disp, col);
				if (txt) {
					SDL_BlitSurface(txt, NULL, screen, &(SDL_Rect){SCALE1(PADDING + BUTTON_PADDING), y + (SCALE1(PILL_SIZE) - txt->h) / 2});
					SDL_FreeSurface(txt);
				}
			}

			if (show_setting && !GetHDMI()) GFX_blitHardwareHints(screen, show_setting, 0);
			if (first) GFX_blitButtonGroup((char*[]){ "A", "OK / 确定", NULL }, 0, screen, 1, 0);
			else GFX_blitButtonGroup((char*[]){ "B", "BACK", "A", "OKAY", NULL }, 1, screen, 1, 0);

			GFX_flip(screen);
			dirty = 0;
		}
		else GFX_sync();
	}

	if (save) I18N_setLanguage(langs[selected].code);

	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();

	return EXIT_SUCCESS;
}
