#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include "defines.h"
#include "utils.h"
#include "api.h"

///////////////////////////////////////

int prefixMatch(char* pre, char* str) {
	return (strncasecmp(pre,str,strlen(pre))==0);
}

int suffixMatch(char* suf, char* str) {
	int len = strlen(suf);
	int offset = strlen(str)-len;
	return (offset>=0 && strncasecmp(suf, str+offset, len)==0);
}
int exactMatch(char* str1, char* str2) {
	int len1 = strlen(str1);
	if (len1!=strlen(str2)) return 0;
	return (strncmp(str1,str2,len1)==0);
}
int hide(char* file_name) {
	int result = 0;
	if (file_name[0]=='.' || suffixMatch(".disabled", file_name) || exactMatch("map.txt", file_name)) result = 1;
	if (file_name[0] == 'I' && file_name[1] == 'm' && file_name[2] == 'g' && file_name[3] == 's' && file_name[4] == 0) result = 1; 
	return result;
}

void bmp2png(char * filename){
	char cmd[512];
	sprintf(cmd,"bmp2png.elf -X \"%s\" && rm \"%s.bak\"", filename, filename);
	system(cmd);
}

void getStatePath(char * gamepath, char* statepath){
	//problem, given the full rom path how to retrieve the core used at the time of state creation? minarch knows the core but minui don't.
	//solution 1 -> parse the lauch.sh to get the core name from the line EMU_TAG=xxxxx 
	//solution 2 -> actually it is pointless knowing also the used core as the emuname (i.e. MAME instead of MAME-mame2003plus) is unique and always uses that core.

	//go for solution2 by changing also the minarch behavior
	char emuname[256];
	getEmuName(gamepath, emuname);
	sprintf(statepath, MYSAVESTATE_PATH "/%s/States",emuname);
}

int canResume(char * gamepath, int last_known_slot){
	//check if exists at least one saved state for the currently selected items then return the slot number
	// the target is removing the need of the file slot_path
	char statepath[256];
	char checkpath[256];
	//char slot_path[256];
	struct stat buffer;
	//char checkpath2[256];
	char romname[256];
	getStatePath(gamepath,statepath);
	getDisplayNameParens(gamepath,romname);
	//sprintf(slot_path, "%s/%s.txt",  gamepath);
	
	//ok now checkpath contains the full path of the state file to check if exists regardless of the slotnumber.
	int result = 0;
	uint64_t latest = 0;
	for (int i=1;i<9;i++){
		sprintf(checkpath,"%s/%s.state%d",statepath,romname,i);
		if (exists(checkpath)){
			if (i == last_known_slot) {
				//found the last known slot, return it
				result = i;
				break;
			}
			result = i;
			//ok the saved state exists, check how old it is.
			//stat(checkpath,&buffer);
			//	if (buffer.st_mtime > latest){
			//		latest=buffer.st_mtime;
			//		result = i;
			//	}  not all devices have rtc so we can't do this
			//instead let's check if the last stored slot number is present 
		}
	} 
	return result;
}

void getDisplayName(const char* in_name, char* out_name) {
	char* tmp;
	char work_name[256];
	strcpy(work_name, in_name);
	strcpy(out_name, in_name);
	
	if (suffixMatch("/" PLATFORM, work_name)) { // hide platform from Tools path...
		tmp = strrchr(work_name, '/');
		tmp[0] = '\0';
	}
	
	// extract just the filename if necessary
	tmp = strrchr(work_name, '/');
	if (tmp) strcpy(out_name, tmp+1);
	
	// remove extension(s), eg. .p8.png
	while ((tmp = strrchr(out_name, '.'))!=NULL) {
		int len = strlen(tmp);
		if (len>2 && len<=4) tmp[0] = '\0'; // 3 letter extension plus dot
		else break;
	}
	
	// remove trailing parens (round and square)
	strcpy(work_name, out_name);
	while ((tmp=strrchr(out_name, '('))!=NULL || (tmp=strrchr(out_name, '['))!=NULL) {
		if (tmp==out_name) break;
		tmp[0] = '\0';
		tmp = out_name;
	}
	
	// make sure we haven't nuked the entire name
	if (out_name[0]=='\0') strcpy(out_name, work_name);
	
	// remove trailing whitespace
	tmp = out_name + strlen(out_name) - 1;
    while(tmp>out_name && isspace((unsigned char)*tmp)) tmp--;
    tmp[1] = '\0';
}

void getEmuName(const char* in_name, char* out_name) { // NOTE: both char arrays need to be MAX_PATH length!
	char* tmp;
	strcpy(out_name, in_name);
	tmp = out_name;
	
	// printf("--------\n  in_name: %s\n",in_name); fflush(stdout);
	
	// extract just the Roms folder name if necessary
	if (prefixMatch(ROMS_PATH, tmp)) {
		tmp += strlen(ROMS_PATH) + 1;
		char* tmp2 = strchr(tmp, '/');
		if (tmp2) tmp2[0] = '\0';
		// printf("    tmp1: %s\n", tmp);
		strcpy(out_name, tmp);
		tmp = out_name;
	}

	// finally extract pak name from parenths if present
	tmp = strrchr(tmp, '(');
	if (tmp) {
		tmp += 1;
		// printf("    tmp2: %s\n", tmp);
		strcpy(out_name, tmp);
		tmp = strchr(out_name,')');
		tmp[0] = '\0';
	}
	
	// printf(" out_name: %s\n", out_name); fflush(stdout);
}
void getEmuPath(char* emu_name, char* pak_path) {
	sprintf(pak_path, "%s/Emus/%s/%s.pak/launch.sh", SDCARD_PATH, PLATFORM, emu_name);
	if (exists(pak_path)) return;
	sprintf(pak_path, "%s/Emus/%s.pak/launch.sh", PAKS_PATH, emu_name);
}

//boxarts
void getDisplayNameParens(const char* in_name, char* out_name) {
	char* tmp;
	char work_name[256];
	strcpy(work_name, in_name);
	strcpy(out_name, in_name);

	if (suffixMatch("/" PLATFORM, work_name)) { // hide platform from Tools path...
		tmp = strrchr(work_name, '/');
		tmp[0] = '\0';
	}

	// extract just the filename if necessary
	tmp = strrchr(work_name, '/');
	if (tmp) strcpy(out_name, tmp+1);

	// remove extension(s), eg. .p8.png
	while ((tmp = strrchr(out_name, '.'))!=NULL) {
		int len = strlen(tmp);
		if (len>2 && len<=4) tmp[0] = '\0'; // 3 letter extension plus dot
		else break;
	}


	// make sure we haven't nuked the entire name
	if (out_name[0]=='\0') strcpy(out_name, work_name);

	// remove trailing whitespace
	tmp = out_name + strlen(out_name) - 1;
    while(tmp>out_name && isspace((unsigned char)*tmp)) tmp--;
    tmp[1] = '\0';
}

//get the name of the parent directory with parentesys
void getParentFolderName(const char* in_name, char* out_name) { // NOTE: both char arrays need to be MAX_PATH length!
	char* tmp;
	strcpy(out_name, in_name);
	tmp = out_name;

	// printf("--------\n  in_name: %s\n",in_name); fflush(stdout);

	// extract just the Roms folder name if necessary
	if (prefixMatch(ROMS_PATH, tmp)) {
		tmp += strlen(ROMS_PATH) + 1;
		char* tmp2 = strchr(tmp, '/');
		if (tmp2) tmp2[0] = '\0';
		// printf("    tmp1: %s\n", tmp);
		strcpy(out_name, tmp);
		tmp = out_name;
	}

}


//get the name of the parent directory removing parentesys
void getDisplayParentFolderName(const char* in_name, char* out_name) { // NOTE: both char arrays need to be MAX_PATH length!
	char* tmp, *tmp3;
	strcpy(out_name, in_name);
	tmp = out_name;

	// printf("--------\n  in_name: %s\n",in_name); fflush(stdout);
	
	// extract just the Roms folder name if necessary
	if (prefixMatch(ROMS_PATH, tmp)) {
		tmp += strlen(ROMS_PATH) + 1;
		char* tmp2 = strchr(tmp, '/');
		if (tmp2) tmp2[0] = '\0';
		// printf("    tmp1: %s\n", tmp);
		strcpy(out_name, tmp);
		tmp = out_name;
	}
	//remove parentesys as we need only the system name
	tmp3 = strrchr(tmp, '(');
	if (tmp3) {
		tmp = strtok(tmp,"(");
		tmp = out_name;
	}
}

char *ltrim(char *s)
{
    while(isspace(*s)) s++;
    return s;
}

char *rtrim(char *s)
{
    char* back = s + strlen(s);
    while(isspace(*--back));
    *(back+1) = '\0';
    return s;
}

char *trim(char *s)
{
    return rtrim(ltrim(s)); 
}


int readBoxartcfg(char * filecfg, myBoxartData * boxartdata){
	//read BOXART_CFGFILE then fill the struct woth all read data
	FILE *config = fopen(filecfg,"r");
    //LOG_info("Opening CFG file %s\n", BOXART_CFGFILE);
    char *mygradient;
    if (config) {
		char * pch;
		char parametri[512];
        //LOG_info("Reading CFG file %s\n", BOXART_CFGFILE);
        while (fgets(parametri, sizeof(parametri), config)) {
            char tmpstr[20];
                if (strncmp(parametri,"BX =",4) == 0){
                pch = strtok (parametri,"=");
                pch = strtok (NULL, "=");
                boxartdata->bX = strtol(pch,NULL,10);
                //LOG_info("BX = %d\n", boxartdata.bX);
                }
            if (strncmp(parametri,"BY =",4) == 0){
                pch = strtok (parametri,"=");
                pch = strtok (NULL, "=");
                boxartdata->bY = strtol(pch,NULL,10);
                //LOG_info("BY = %d\n", boxartdata.bY);
                }
            if (strncmp(parametri,"BW =",4) == 0){
                pch = strtok (parametri,"=");
                pch = strtok (NULL, "=");
                boxartdata->bW = strtol(pch,NULL,10);
                //LOG_info("BW = %d\n", boxartdata.bW);
                }
            if (strncmp(parametri,"BH =",4) == 0){
                pch = strtok (parametri,"=");
                pch = strtok (NULL, "=");
                boxartdata->bH = strtol(pch,NULL,10);
                //LOG_info("BH = %d\n", boxartdata.bH);
                }
            if (strncmp(parametri,"SW =",4) == 0){
                pch = strtok (parametri,"=");
                pch = strtok (NULL, "=");
                boxartdata->sW = strtol(pch,NULL,10);
                //LOG_info("SW = %d\n", boxartdata.sW);
                }
            if (strncmp(parametri,"SH =",4) == 0){
                pch = strtok (parametri,"=");
                pch = strtok (NULL, "=");
                boxartdata->sH = strtol(pch,NULL,10);
                //LOG_info("SH = %d\n", boxartdata.sH);
                }
            if (strncmp(parametri,"ASPECT =",8) == 0){
                pch = strtok (parametri,"=");
                pch = strtok (NULL, "=");
                boxartdata->aspect = strtol(pch,NULL,10);
                //LOG_info("ASPECT = %d\n", boxartdata.aspect);
                }
            if (strncmp(parametri,"GRADIENT = ",11) == 0){
                pch = strtok (parametri,"=");
                pch = strtok (NULL, "=");
				mygradient = trim(pch);
                sprintf(boxartdata->gradient,"%s", mygradient);
                //LOG_info("GRADIENT = #%s#\n",(boxartdata.gradient);
                }            
        }
        fclose(config);
		return 1;
    }
	return -1;
}


// end boxarts



void normalizeNewline(char* line) {
	int len = strlen(line);
	if (len>1 && line[len-1]=='\n' && line[len-2]=='\r') { // windows!
		line[len-2] = '\n';
		line[len-1] = '\0';
	}
}
void trimTrailingNewlines(char* line) {
	int len = strlen(line);
	while (len>0 && line[len-1]=='\n') {
		line[len-1] = '\0'; // trim newline
		len -= 1;
	}
}
void trimSortingMeta(char** str) { // eg. `001) `
	// TODO: this code is suss
	char* safe = *str;
	while(isdigit(**str)) *str += 1; // ignore leading numbers

	if (*str[0]==')') { // then match a closing parenthesis
		*str += 1;
	}
	else { //  or bail, restoring the string to its original value
		*str = safe;
		return;
	}
	
	while(isblank(**str)) *str += 1; // ignore leading space
}

///////////////////////////////////////

int exists(char* path) {
	return access(path, F_OK)==0;
}
void touch(char* path) {
	close(open(path, O_RDWR|O_CREAT, 0777));
}
void putFile(char* path, char* contents) {
	FILE* file = fopen(path, "w");
	if (file) {
		fputs(contents, file);
		fclose(file);
	}
}
void getFile(char* path, char* buffer, size_t buffer_size) {
	FILE *file = fopen(path, "r");
	if (file) {
		fseek(file, 0L, SEEK_END);
		size_t size = ftell(file);
		if (size>buffer_size-1) size = buffer_size - 1;
		rewind(file);
		fread(buffer, sizeof(char), size, file);
		fclose(file);
		buffer[size] = '\0';
	}
}
char* allocFile(char* path) { // caller must free!
	char* contents = NULL;
	FILE *file = fopen(path, "r");
	if (file) {
		fseek(file, 0L, SEEK_END);
		size_t size = ftell(file);
		contents = calloc(size+1, sizeof(char));
		fseek(file, 0L, SEEK_SET);
		fread(contents, sizeof(char), size, file);
		fclose(file);
		contents[size] = '\0';
	}
	return contents;
}
int getInt(char* path) {
	int i = 0;
	FILE *file = fopen(path, "r");
	if (file!=NULL) {
		fscanf(file, "%i", &i);
		fclose(file);
	}
	return i;
}
void putInt(char* path, int value) {
	char buffer[16]; // an int needs up to 11 characters
	sprintf(buffer, "%d", value);
	putFile(path, buffer);
}

uint64_t getMicroseconds(void) {
    uint64_t ret;
    struct timeval tv;

    gettimeofday(&tv, NULL);

    ret = (uint64_t)tv.tv_sec * 1000000;
    ret += (uint64_t)tv.tv_usec;

    return ret;
}


int getHdmiModeValues(char * hdmimode, int *w, int *h, int *hz){
	// Inizializza i valori a 0
	int localw = *w;
	int localh = *h;
	int localhz = *hz;
    *w = 0;
    *h = 0;
    *hz = 0;

    // Trova la posizione dei separatori 'x' e 'p'
    char *x_pos = strchr(hdmimode, 'x');
    char *p_pos = strchr(hdmimode, 'p');

    if (x_pos && p_pos) {
        // Estrai la larghezza (w)
        *x_pos = '\0'; // Termina la stringa temporaneamente
        *w = atoi(hdmimode);

        // Estrai l'altezza (h)
        char *height_start = x_pos + 1;
        *p_pos = '\0'; // Termina la stringa temporaneamente
        *h = atoi(height_start);

        // Estrai la frequenza (hz)
        char *hz_start = p_pos + 1;
        *hz = atoi(hz_start);
		return 0;
    } else {
		// Se il formato non è corretto, ripristina i valori originali
		*w = localw;
		*h = localh;
		*hz = localhz;
		return -1;
	}
}

//add cheat support
void truncateString(char *string, size_t max_len) {
	size_t len = strlen(string) + 1;
	if (len <= max_len) return;

	strncpy(&string[max_len - 4], "...\0", 4);
}
void wrapString(char *string, size_t max_len, size_t max_lines) {
	char *line = string;

	for (size_t i = 1; i < max_lines; i++) {
		char *p = line;
		char *prev;
		do {
			prev = p;
			p = strchr(prev+1, ' ');
		} while (p && p - line < (int)max_len);

		if (!p && strlen(line) < max_len) break;

		if (prev && prev != line) {
			line = prev + 1;
			*prev = '\n';
		}
	}
	truncateString(line, max_len);
}
//end cheat support

///////////////////////////////////////
// i18n (see utils.h)

#define I18N_LANGUAGE_PATH SHARED_USERDATA_PATH "/language"
#define I18N_LANG_DIR RES_PATH "/lang"

typedef struct { char* key; char* val; } I18N_Entry;
static I18N_Entry* i18n_tab = NULL; // open addressing, size is a power of two
static int i18n_cap = 0;
static int i18n_count = 0;
static char i18n_code[I18N_CODE_MAX] = "en";
static int i18n_chosen = 0;

static unsigned i18n_hash(const char* s) {
	unsigned h = 2166136261u;
	while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
	return h;
}
static void i18n_put(char* key, char* val); // takes ownership of key and val
static void i18n_grow(void) {
	I18N_Entry* old = i18n_tab;
	int oldcap = i18n_cap;
	i18n_cap = oldcap ? oldcap * 2 : 1024;
	i18n_tab = calloc(i18n_cap, sizeof(I18N_Entry));
	i18n_count = 0;
	for (int i = 0; i < oldcap; i++) if (old[i].key) i18n_put(old[i].key, old[i].val);
	free(old);
}
static void i18n_put(char* key, char* val) {
	if (!i18n_cap || (i18n_count + 1) * 2 > i18n_cap) i18n_grow();
	unsigned m = i18n_cap - 1;
	unsigned i = i18n_hash(key) & m;
	while (i18n_tab[i].key) {
		if (strcmp(i18n_tab[i].key, key) == 0) { // later catalogs (paks) override earlier ones
			free(i18n_tab[i].val); free(key);
			i18n_tab[i].val = val;
			return;
		}
		i = (i + 1) & m;
	}
	i18n_tab[i].key = key;
	i18n_tab[i].val = val;
	i18n_count++;
}
static char* i18n_unescape(const char* s, size_t len) {
	char* out = malloc(len + 1);
	size_t o = 0;
	for (size_t i = 0; i < len; i++) {
		if (s[i] == '\\' && i + 1 < len) {
			i++;
			out[o++] = s[i] == 'n' ? '\n' : s[i] == 't' ? '\t' : s[i];
		}
		else out[o++] = s[i];
	}
	out[o] = '\0';
	return out;
}
static void i18n_load(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) return;
	char line[2048];
	while (fgets(line, sizeof(line), f)) {
		size_t len = strlen(line);
		while (len && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
		if (!len || line[0] == '#') continue;
		char* tab = strchr(line, '\t');
		if (!tab || tab == line) continue;
		if (!tab[1]) continue; // empty translation = untranslated
		i18n_put(i18n_unescape(line, tab - line), i18n_unescape(tab + 1, len - (tab + 1 - line)));
	}
	fclose(f);
}
static int i18n_validCode(const char* c) {
	if (!c[0]) return 0;
	for (; *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-')) return 0;
	return 1;
}

void I18N_init(void) {
	char buf[I18N_CODE_MAX] = "";
	i18n_chosen = 0;
	strcpy(i18n_code, "en");
	if (exists(I18N_LANGUAGE_PATH)) {
		getFile(I18N_LANGUAGE_PATH, buf, sizeof(buf));
		trimTrailingNewlines(buf);
		char* code = trim(buf);
		if (i18n_validCode(code)) { strcpy(i18n_code, code); i18n_chosen = 1; }
	}
	for (int i = 0; i < i18n_cap; i++) { free(i18n_tab[i].key); free(i18n_tab[i].val); }
	free(i18n_tab);
	i18n_tab = NULL; i18n_cap = 0; i18n_count = 0;
	if (strcmp(i18n_code, "en") == 0) return; // English is the source language

	char path[MAX_PATH];
	snprintf(path, sizeof(path), I18N_LANG_DIR "/%s.txt", i18n_code);
	i18n_load(path);
	const char* pak = getenv("PAK_DIR");
	snprintf(path, sizeof(path), "%s/lang/%s.txt", pak && pak[0] ? pak : ".", i18n_code);
	i18n_load(path);
}

const char* I18N_get(const char* s) {
	if (!s || !i18n_cap) return s;
	unsigned m = i18n_cap - 1;
	unsigned i = i18n_hash(s) & m;
	while (i18n_tab[i].key) {
		if (strcmp(i18n_tab[i].key, s) == 0) return i18n_tab[i].val;
		i = (i + 1) & m;
	}
	return s;
}
#define I18N_PAKNAME_CACHE 128
static struct { char* pak; char* name; char* out; } i18n_pakcache[I18N_PAKNAME_CACHE];
static int i18n_pakcache_n = 0;
const char* I18N_getPakName(const char* pak_path, const char* name) {
	if (!i18n_cap || !pak_path || !name) return I18N_get(name); // English, or nothing loaded
	for (int i = 0; i < i18n_pakcache_n; i++)
		if (strcmp(i18n_pakcache[i].pak, pak_path) == 0 && strcmp(i18n_pakcache[i].name, name) == 0) return i18n_pakcache[i].out;
	char path[MAX_PATH], line[2048];
	const char* res = NULL;
	snprintf(path, sizeof(path), "%s/lang/%s.txt", pak_path, i18n_code);
	FILE* f = fopen(path, "r");
	if (f) {
		size_t nlen = strlen(name);
		while (fgets(line, sizeof(line), f)) {
			if (line[0] == '#') continue;
			char* tab = strchr(line, '\t');
			if (!tab || (size_t)(tab - line) != nlen || strncmp(line, name, nlen) != 0 || !tab[1]) continue;
			size_t len = strlen(tab + 1);
			while (len && (tab[len] == '\n' || tab[len] == '\r')) tab[len--] = '\0';
			res = i18n_unescape(tab + 1, strlen(tab + 1)); // leaked on purpose, it is cached below
			break;
		}
		fclose(f);
	}
	if (!res) return I18N_get(name);
	if (i18n_pakcache_n < I18N_PAKNAME_CACHE) {
		i18n_pakcache[i18n_pakcache_n].pak = strdup(pak_path);
		i18n_pakcache[i18n_pakcache_n].name = strdup(name);
		i18n_pakcache[i18n_pakcache_n].out = (char*)res;
		i18n_pakcache_n++;
	}
	return res;
}
const char* I18N_getLanguage(void) { return i18n_code; }
int I18N_isLanguageChosen(void) { return i18n_chosen; }

int I18N_listLanguages(I18N_Lang* out, int max) {
	int n = 0;
	if (max < 1) return 0;
	strcpy(out[n].code, "en");
	strcpy(out[n].name, "English");
	n++;
	DIR* dir = opendir(I18N_LANG_DIR);
	if (!dir) return n;
	struct dirent* ent;
	while ((ent = readdir(dir)) && n < max) {
		size_t len = strlen(ent->d_name);
		if (len < 5 || len - 4 >= I18N_CODE_MAX || strcmp(ent->d_name + len - 4, ".txt") != 0) continue;
		char code[I18N_CODE_MAX];
		memcpy(code, ent->d_name, len - 4);
		code[len - 4] = '\0';
		if (!i18n_validCode(code) || strcmp(code, "en") == 0) continue;
		strcpy(out[n].code, code);
		strcpy(out[n].name, code); // fallback: show the code
		char path[MAX_PATH], line[256];
		snprintf(path, sizeof(path), I18N_LANG_DIR "/%s", ent->d_name);
		FILE* f = fopen(path, "r");
		if (f) {
			while (fgets(line, sizeof(line), f)) {
				if (strncmp(line, "#name:", 6) == 0) {
					trimTrailingNewlines(line);
					char* name = trim(line + 6);
					if (name[0]) snprintf(out[n].name, I18N_NAME_MAX, "%s", name);
					break;
				}
				if (line[0] != '#') break;
			}
			fclose(f);
		}
		n++;
	}
	closedir(dir);
	// keep "en" first, sort the rest by code so the order is stable
	for (int i = 1; i < n; i++)
		for (int j = i + 1; j < n; j++)
			if (strcmp(out[j].code, out[i].code) < 0) { I18N_Lang t = out[i]; out[i] = out[j]; out[j] = t; }
	return n;
}

int I18N_setLanguage(const char* code) {
	if (!i18n_validCode(code)) return -1;
	mkdir(SHARED_USERDATA_PATH, 0777); // fails harmlessly when it exists
	char buf[I18N_CODE_MAX + 2];
	snprintf(buf, sizeof(buf), "%s\n", code);
	putFile(I18N_LANGUAGE_PATH, buf);
	sync();
	return 0;
}
