#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

int prefixMatch(char* pre, char* str);
int suffixMatch(char* suf, char* str);
int exactMatch(char* str1, char* str2);
int hide(char* file_name);

void getDisplayName(const char* in_name, char* out_name);
void getEmuName(const char* in_name, char* out_name);
void getEmuPath(char* emu_name, char* pak_path);

//boxarts
enum Aspect { ASPECT, NATIVE, FULL};

typedef struct _BoxartData {
    int sW;
    int sH;
    int bX;
    int bY;
    int bW;
    int bH;
    enum Aspect aspect;
    char gradient[256];
} myBoxartData;

void getDisplayNameParens(const char* in_name, char* out_name);
void getParentFolderName(const char* in_name, char* out_name);
void getDisplayParentFolderName(const char* in_name, char* out_name);
int readBoxartcfg(char *, myBoxartData *);
void getStatePath(char * gamepath, char* statepath);
int canResume(char * rompath, int last_known_slot);
// end boxarts

void normalizeNewline(char* line);
void trimTrailingNewlines(char* line);
void trimSortingMeta(char** str);
char *trim(char *s);
int exists(char* path);
void touch(char* path);
void putFile(char* path, char* contents);
char* allocFile(char* path); // caller must free
void getFile(char* path, char* buffer, size_t buffer_size);
void putInt(char* path, int value);
int getInt(char* path);
void bmp2png(char * filename);
int getHdmiModeValues(char * hdmimode, int *w, int *h, int *hz);

uint64_t getMicroseconds(void);

//add cheat support
void truncateString(char *string, size_t max_len);
void wrapString(char *string, size_t max_len, size_t max_lines);
//end of add cheat support

// ---- i18n -----------------------------------------------------------------------------------
// Strings in the code are English; TR("English") returns the translation of the current language or the
// same string when there is none. Catalogs are UTF-8 text files, one "English<TAB>translation" per line
// (\n and \\ are escapes, '#' starts a comment, "#name: ..." gives the name shown in the language list):
//   <SDCARD>/.system/res/lang/<code>.txt   shared by every program (menus, buttons, minui, minarch)
//   $PAK_DIR/lang/<code>.txt (or ./lang/)  extra strings of one pak, shipped inside that pak
// The current language code lives in <SHARED_USERDATA_PATH>/language ("en" = built in, no file needed).
#define I18N_CODE_MAX 16
#define I18N_NAME_MAX 64
typedef struct { char code[I18N_CODE_MAX]; char name[I18N_NAME_MAX]; } I18N_Lang;

void I18N_init(void);                                  // load catalogs for the current language (call once, GFX_init does it)
const char* I18N_get(const char* s);                   // translation or s itself
const char* I18N_getLanguage(void);                    // current code, "en" when none was chosen yet
const char* I18N_getPakName(const char* pak_path, const char* name); // list name of a tool: pak/lang/<code>.txt first, then the shared catalog
int I18N_isLanguageChosen(void);                       // 0 on first run (no language file)
int I18N_listLanguages(I18N_Lang* out, int max);       // "en" first, then every <code>.txt found in res/lang
int I18N_setLanguage(const char* code);                // persist the choice; takes effect at the next I18N_init()
#define TR(s) I18N_get(s)

#endif
