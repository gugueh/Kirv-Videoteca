#ifndef KIRV_APP_H
#define KIRV_APP_H
#include "library.h"
extern VmpLibrary library;
extern char notice[160];
unsigned app_buttons(void);
typedef struct { int tap,x,y,swipe; } AppTouch;
extern AppTouch app_touch;
void app_frame_begin(void);
void app_frame_end(void);
void app_message(const char *s);
int app_list(const char *path,VmpEntry *entries,size_t *count);
int app_save(void);
/* 0 = text returned in out (UTF-8), 1 = cancelled, -1 = keyboard unavailable. */
int app_text_input(const char *title,char *out,size_t cap);
/* Shows entries[map[start]] and neighbours that are images; returns the last index shown. */
size_t app_viewer(const char *directory,const VmpEntry *entries,const size_t *map,size_t count,size_t start);
int app_browser(int (*play)(const char *path));
#endif
