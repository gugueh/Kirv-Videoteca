#ifndef KIRV_APP_H
#define KIRV_APP_H
#include "library.h"
extern VmpLibrary library;
extern char notice[160];
/* Interface language: Spanish (0) or English (1). */
#define L(es,en) (library.language?(en):(es))
unsigned app_buttons(void);
/* Buttons held right now (not only newly pressed). */
unsigned app_held(void);
/* tap/swipe on release; down with dx,dy = finger movement since the last frame. */
typedef struct { int tap,x,y,swipe,down,dx,dy; } AppTouch;
extern AppTouch app_touch;
void app_frame_begin(void);
void app_frame_end(void);
void app_message(const char *s);
int app_list(const char *path,VmpEntry *entries,size_t *count);
/* Every image under the console's pictures and the library, newest first.
   paths[i] is the full path of entries[i]. Returns the picture-folder access
   result (negative error code when the system pictures are not readable). */
int app_list_images(VmpEntry *entries,char (*paths)[VMP_PATH_CAP],size_t cap,size_t *count);
int app_save(void);
/* 0 = text returned in out (UTF-8), 1 = cancelled, -1 = keyboard unavailable. */
int app_text_input(const char *title,const char *initial,char *out,size_t cap);
/* Shows paths[start] and lets the user move through the list; returns the last index shown. */
size_t app_viewer(char (*paths)[VMP_PATH_CAP],size_t count,size_t start);
int app_browser(int (*play)(const char *path));
#endif
