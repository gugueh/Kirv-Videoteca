/* Copyright (c) 2025-2026 gugueh (Kirv Studio). All rights reserved. */
#ifndef VMP_LIBRARY_H
#define VMP_LIBRARY_H
#include "core.h"
#define VMP_HISTORY 64
typedef struct { char path[VMP_PATH_CAP]; uint64_t position, duration; unsigned watched; } VmpProgress;
typedef struct {
    unsigned resume, autoplay, seek_index, hide_index, subtitles, fill, volume, language;
    size_t count;
    VmpProgress items[VMP_HISTORY];
} VmpLibrary;
void vmp_library_defaults(VmpLibrary *state);
int vmp_library_read(FILE *file, VmpLibrary *state);
int vmp_library_write(FILE *file, const VmpLibrary *state);
const VmpProgress *vmp_progress(const VmpLibrary *state, const char *path);
int vmp_remember(VmpLibrary *state, const char *path, uint64_t pos, uint64_t duration, unsigned watched);
unsigned vmp_seek_seconds(const VmpLibrary *state);
unsigned vmp_hide_seconds(const VmpLibrary *state);
/* Forgets progress for path; moves entries under a renamed folder or file. */
void vmp_forget(VmpLibrary *state, const char *path);
void vmp_rename_progress(VmpLibrary *state, const char *from, const char *to);
#endif
