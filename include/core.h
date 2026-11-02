#ifndef VMP_CORE_H
#define VMP_CORE_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define VMP_PATH_CAP 512
#define VMP_ROOT "ux0:data/VMPH00001/video/"
/* Read-only roots for the console's own pictures (screenshots, camera, saved images). */
#define VMP_PICTURES "ux0:picture/"
#define VMP_PHOTOS "photo0:/"
#define VMP_MAX_ENTRIES 1024
#define VMP_SRT_TEXT_CAP 1024
#define VMP_SRT_MAX_CUES 4096
#define VMP_SRT_MAX_BYTES (4u * 1024u * 1024u)
#define VMP_MAX_FRAME_BYTES (8u * 1024u * 1024u)
/* AvPlayer uses the texture callback for decoder work pools, not just one frame. */
#define VMP_MAX_DECODER_BYTES (64u * 1024u * 1024u)
#define VMP_MAX_AUDIO_FRAMES 4096u
/* Image viewer limits: decoded RGB plus the encoded file must fit the 64 MiB heap. */
#define VMP_IMAGE_MAX_BYTES (12u * 1024u * 1024u)
#define VMP_IMAGE_MAX_PIXELS (8u * 1000u * 1000u)
#define VMP_IMAGE_MAX_SIDE 4096u
/* JPEG is decoded scaled down (1/2 .. 1/8) and streamed, so much larger photos fit. */
#define VMP_JPEG_MAX_BYTES (48u * 1024u * 1024u)
#define VMP_JPEG_MAX_SIDE 16384u
enum { VMP_IMAGE_NONE, VMP_IMAGE_PNG, VMP_IMAGE_JPEG, VMP_IMAGE_BMP };

typedef struct {
    uint64_t start_ms, end_ms;
    char text[VMP_SRT_TEXT_CAP];
} VmpCue;
typedef struct {
    VmpCue *cues;
    size_t count;
} VmpSubtitles;
typedef struct { char name[256]; int directory, regular; uint64_t time; } VmpEntry;
typedef int (*VmpNextEntry)(void *context, VmpEntry *entry);

/* All failures leave an empty output or unchanged path; no partial paths. */
int vmp_copy(char *dst, size_t cap, const char *src);
int vmp_is_mp4(const char *name);
/* Image kind from the extension (.png .jpg .jpeg .bmp). Generated thumbnails
 * (*.thumb.png) and folder covers (cover.png) are not browsable images. */
int vmp_image_kind(const char *name);
/* Reads dimensions from the header and checks size limits. 0 on success. */
int vmp_image_probe(FILE *input, int kind, uint64_t file_bytes, unsigned *width, unsigned *height);
/* Name for a new folder: no FAT-reserved characters, no edge spaces or final dot. */
int vmp_folder_name_valid(const char *name);
/* Converts UTF-16 (surrogates allowed) to UTF-8 and trims edge spaces. 0 on success. */
int vmp_utf16_to_utf8(const uint16_t *text, char *out, size_t cap);
/* Converts valid UTF-8 to NUL-terminated UTF-16 (cap in code units). 0 on success. */
int vmp_utf8_to_utf16(const char *text, uint16_t *out, size_t cap);
/* True only inside the app's own library: the picture roots are never modified. */
int vmp_path_writable(const char *path);
/* Replaces the prefix "from" of path with "to" (both directories). 0 on success. */
int vmp_rebase(char path[VMP_PATH_CAP], const char *from, const char *to);
/* Orders a date-time so that larger values are newer. */
uint64_t vmp_time_key(unsigned year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second);
int vmp_utf8_valid(const char *text, size_t bytes);
int vmp_path_valid(const char *path, int directory);
int vmp_child(char path[VMP_PATH_CAP], const char *name, int directory);
int vmp_parent(char path[VMP_PATH_CAP]);
int vmp_srt_path(char dst[VMP_PATH_CAP], const char *video);
int vmp_round_allocation(uint32_t size, uint32_t alignment, uint32_t *out);
int vmp_round_decoder_allocation(uint32_t size, uint32_t alignment, uint32_t *out);
int vmp_read_window(uint64_t size, uint64_t offset, uint32_t requested, uint32_t *available);
int vmp_player_handle_valid(int32_t handle, int mapped_user_address);
int vmp_audio_shape(unsigned channels, unsigned rate, unsigned bytes,
                    unsigned *samples);
int vmp_collect(const char *path, VmpEntry *entries, size_t capacity,
                size_t *count, VmpNextEntry next, void *context);
/* Replaces previous content. Invalid/oversized input is rejected atomically. */
int vmp_srt_read(FILE *input, VmpSubtitles *out);
const char *vmp_srt_at(const VmpSubtitles *subtitles, uint64_t time_ms);
void vmp_srt_free(VmpSubtitles *subtitles);
#endif
