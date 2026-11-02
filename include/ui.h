#ifndef KIRV_UI_H
#define KIRV_UI_H
#include <vita2d.h>
#include "library.h"
enum { UI_LIBRARY, UI_SERIES, UI_MOVIES, UI_FOLDER, UI_PLAY, UI_PAUSE, UI_NEXT, UI_PREVIOUS,
 UI_VOLUME, UI_SUBTITLES, UI_SETTINGS, UI_SEARCH, UI_SORT, UI_FAVORITE, UI_CHECK, UI_BACK, UI_IMAGE, UI_ICONS };
int ui_init(void);
void ui_fini(void);
void ui_background(void);
void ui_panel(int x,int y,int w,int h,int selected);
void ui_text(int x,int y,float scale,unsigned color,const char *s,int max_width);
void ui_icon(int id,int x,int y,int size);
void ui_header(const char *title);
void ui_footer(const char *hint);
void ui_welcome(void);
void ui_cover(const char *directory,int x,int y,int w,int h,const char *label);
void ui_clear_covers(void);
void ui_save_thumbnail(const char *video,vita2d_texture *frame);
void ui_time(char *out,size_t cap,uint64_t ms);
void ui_bar(int x,int y,int w,uint64_t pos,uint64_t duration);
void ui_player(const char *name,uint64_t pos,uint64_t duration,int paused,unsigned volume,int subtitles,int has_subtitles,unsigned seek);
#define UI_WHITE RGBA8(237,245,255,255)
#define UI_MUTED RGBA8(167,191,214,255)
#define UI_CYAN RGBA8(99,216,255,255)
#endif
