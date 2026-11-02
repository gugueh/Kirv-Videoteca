#include "core.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

int vmp_copy(char *dst, size_t cap, const char *src)
{
    size_t n;
    if (!dst || !cap) return -1;
    if (!src) { dst[0] = 0; return -1; }
    n = strlen(src);
    if (n >= cap) { dst[0] = 0; return -1; }
    memmove(dst, src, n + 1);
    return 0;
}

static unsigned lower_ascii(unsigned c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

int vmp_is_mp4(const char *name)
{
    size_t n;
    if (!name || (n = strlen(name)) < 5) return 0;
    return name[n-4] == '.' && lower_ascii((unsigned char)name[n-3]) == 'm'
        && lower_ascii((unsigned char)name[n-2]) == 'p' && name[n-1] == '4';
}

static int has_suffix(const char *name, const char *suffix)
{
    size_t n = strlen(name), m = strlen(suffix), i;
    if (n <= m) return 0;
    for (i = 0; i < m; ++i)
        if (lower_ascii((unsigned char)name[n-m+i]) != (unsigned char)suffix[i]) return 0;
    return 1;
}

int vmp_image_kind(const char *name)
{
    if (!name) return VMP_IMAGE_NONE;
    if (has_suffix(name, ".thumb.png") || !strcmp(name, "cover.png")) return VMP_IMAGE_NONE;
    if (has_suffix(name, ".png")) return VMP_IMAGE_PNG;
    if (has_suffix(name, ".jpg") || has_suffix(name, ".jpeg")) return VMP_IMAGE_JPEG;
    if (has_suffix(name, ".bmp")) return VMP_IMAGE_BMP;
    return VMP_IMAGE_NONE;
}

static unsigned be16(const unsigned char *b) { return ((unsigned)b[0] << 8) | b[1]; }
static unsigned be32(const unsigned char *b) { return ((unsigned)b[0] << 24) | ((unsigned)b[1] << 16) | ((unsigned)b[2] << 8) | b[3]; }
static unsigned le32(const unsigned char *b) { return ((unsigned)b[3] << 24) | ((unsigned)b[2] << 16) | ((unsigned)b[1] << 8) | b[0]; }

static int jpeg_dimensions(FILE *f, unsigned *w, unsigned *h)
{
    unsigned char b[8];
    int c;
    if (fread(b, 1, 2, f) != 2 || b[0] != 0xFF || b[1] != 0xD8) return -1;
    for (unsigned segments = 0; segments < 256; ++segments) {
        do { c = fgetc(f); } while (c != EOF && c != 0xFF);
        do { c = fgetc(f); } while (c == 0xFF);
        if (c == EOF) return -1;
        if (c == 0xD9 || c == 0xDA) return -1;           /* no frame header before data */
        if (c == 0x01 || (c >= 0xD0 && c <= 0xD7)) continue; /* markers without length */
        if (fread(b, 1, 2, f) != 2) return -1;
        unsigned length = be16(b);
        if (length < 2) return -1;
        if (c >= 0xC0 && c <= 0xCF && c != 0xC4 && c != 0xC8 && c != 0xCC) {
            if (length < 8 || fread(b, 1, 6, f) != 6) return -1;
            *h = be16(b + 1); *w = be16(b + 3);
            return 0;
        }
        if (fseek(f, (long)length - 2, SEEK_CUR)) return -1;
    }
    return -1;
}

int vmp_image_probe(FILE *input, int kind, uint64_t file_bytes, unsigned *width, unsigned *height)
{
    unsigned char h[40];
    unsigned w = 0, ht = 0;
    if (!input || !width || !height) return -1;
    *width = *height = 0;
    if (!file_bytes || file_bytes > (kind == VMP_IMAGE_JPEG ? VMP_JPEG_MAX_BYTES : VMP_IMAGE_MAX_BYTES) ||
        fseek(input, 0, SEEK_SET)) return -1;
    if (kind == VMP_IMAGE_PNG) {
        if (fread(h, 1, 32, input) != 32 || memcmp(h, "\211PNG\r\n\032\n", 8) ||
            be32(h + 8) != 13 || memcmp(h + 12, "IHDR", 4)) return -1;
        w = be32(h + 16); ht = be32(h + 20);
    } else if (kind == VMP_IMAGE_JPEG) {
        if (jpeg_dimensions(input, &w, &ht)) return -1;
    } else if (kind == VMP_IMAGE_BMP) {
        unsigned bits, compression; int32_t sh;
        if (fread(h, 1, 34, input) != 34 || h[0] != 'B' || h[1] != 'M' || le32(h + 14) < 40) return -1;
        w = le32(h + 18); sh = (int32_t)le32(h + 22);
        if (sh == INT32_MIN) return -1;
        ht = (unsigned)(sh < 0 ? -sh : sh);
        bits = (unsigned)h[28] | ((unsigned)h[29] << 8); compression = le32(h + 30);
        if ((bits != 24 && bits != 32) || compression != 0) return -1;
    } else return -1;
    if (!w || !ht) return -1;
    if (kind == VMP_IMAGE_JPEG ? (w > VMP_JPEG_MAX_SIDE || ht > VMP_JPEG_MAX_SIDE) :
        (w > VMP_IMAGE_MAX_SIDE || ht > VMP_IMAGE_MAX_SIDE || (uint64_t)w * ht > VMP_IMAGE_MAX_PIXELS)) return -1;
    *width = w; *height = ht;
    return 0;
}

int vmp_folder_name_valid(const char *name)
{
    size_t n, i;
    if (!name || !(n = strlen(name)) || n > 255) return 0;
    if (name[0] == ' ' || name[n-1] == ' ' || name[n-1] == '.') return 0;
    for (i = 0; i < n; ++i)
        if ((unsigned char)name[i] < 32 || name[i] == 127 || strchr("<>:\"/\\|?*", name[i])) return 0;
    return vmp_utf8_valid(name, n);
}

int vmp_utf16_to_utf8(const uint16_t *text, char *out, size_t cap)
{
    size_t n = 0, start = 0, end;
    if (!out || !cap) return -1;
    out[0] = 0;
    if (!text) return -1;
    for (size_t i = 0; text[i]; ) {
        uint32_t cp = text[i++];
        unsigned char enc[4]; size_t len;
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (text[i] < 0xDC00 || text[i] > 0xDFFF) return -1;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (text[i++] - 0xDC00);
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) return -1;
        if (cp < 0x80) { enc[0] = (unsigned char)cp; len = 1; }
        else if (cp < 0x800) { enc[0] = (unsigned char)(0xC0 | (cp >> 6)); enc[1] = (unsigned char)(0x80 | (cp & 63)); len = 2; }
        else if (cp < 0x10000) { enc[0] = (unsigned char)(0xE0 | (cp >> 12)); enc[1] = (unsigned char)(0x80 | ((cp >> 6) & 63)); enc[2] = (unsigned char)(0x80 | (cp & 63)); len = 3; }
        else { enc[0] = (unsigned char)(0xF0 | (cp >> 18)); enc[1] = (unsigned char)(0x80 | ((cp >> 12) & 63)); enc[2] = (unsigned char)(0x80 | ((cp >> 6) & 63)); enc[3] = (unsigned char)(0x80 | (cp & 63)); len = 4; }
        if (n + len >= cap) { out[0] = 0; return -1; }
        memcpy(out + n, enc, len); n += len;
    }
    out[n] = 0;
    end = n;
    while (end > 0 && out[end-1] == ' ') --end;
    while (start < end && out[start] == ' ') ++start;
    memmove(out, out + start, end - start); out[end - start] = 0;
    return 0;
}

int vmp_utf8_to_utf16(const char *text, uint16_t *out, size_t cap)
{
    size_t n = 0, i = 0, len;
    if (!out || !cap) return -1;
    out[0] = 0;
    if (!text || !vmp_utf8_valid(text, len = strlen(text))) return -1;
    while (i < len) {
        unsigned char c = (unsigned char)text[i];
        uint32_t cp; size_t extra = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
        cp = extra == 0 ? c : extra == 1 ? (c & 0x1Fu) : extra == 2 ? (c & 0x0Fu) : (c & 0x07u);
        for (size_t k = 1; k <= extra; ++k) cp = (cp << 6) | ((unsigned char)text[i + k] & 0x3Fu);
        i += extra + 1;
        if (cp >= 0x10000) {
            if (n + 2 >= cap) { out[0] = 0; return -1; }
            cp -= 0x10000;
            out[n++] = (uint16_t)(0xD800 + (cp >> 10)); out[n++] = (uint16_t)(0xDC00 + (cp & 0x3FF));
        } else {
            if (n + 1 >= cap) { out[0] = 0; return -1; }
            out[n++] = (uint16_t)cp;
        }
    }
    out[n] = 0;
    return 0;
}

uint64_t vmp_time_key(unsigned year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second)
{
    return ((((((uint64_t)year * 13 + month) * 32 + day) * 24 + hour) * 60 + minute) * 60) + second;
}

static size_t root_length(const char *path)
{
    static const char *const roots[] = {VMP_ROOT, VMP_PICTURES, VMP_PHOTOS};
    for (size_t i = 0; i < sizeof(roots) / sizeof(*roots); ++i) {
        size_t n = strlen(roots[i]);
        if (!strncmp(path, roots[i], n)) return n;
    }
    return 0;
}

static int component_valid(const char *s, size_t n)
{
    size_t i;
    if (!n || n > 255 || (n == 1 && s[0] == '.') ||
        (n == 2 && s[0] == '.' && s[1] == '.')) return 0;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c == 127 || c == '/' || c == '\\' || c == ':') return 0;
    }
    return vmp_utf8_valid(s, n);
}

int vmp_utf8_valid(const char *text, size_t bytes)
{
    size_t i = 0;
    if (!text) return 0;
    while (i < bytes) {
        unsigned c = (unsigned char)text[i++], value, remaining, minimum;
        if (c < 128) {
            if ((c < 32 && c != '\n' && c != '\t') || c == 127) return 0;
            continue;
        }
        if (c >= 0xc2 && c <= 0xdf) { value = c & 31; remaining = 1; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { value = c & 15; remaining = 2; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { value = c & 7; remaining = 3; minimum = 0x10000; }
        else return 0;
        if (remaining > bytes-i) return 0;
        while (remaining--) {
            c = (unsigned char)text[i++];
            if ((c & 0xc0) != 0x80) return 0;
            value = (value << 6) | (c & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return 0;
    }
    return 1;
}

int vmp_path_valid(const char *path, int directory)
{
    size_t len, root, start, i;
    if (!path || (len = strlen(path)) >= VMP_PATH_CAP || !(root = root_length(path))) return 0;
    if (directory && path[len-1] != '/') return 0;
    if (!directory && (len == root || path[len-1] == '/')) return 0;
    start = root;
    for (i = root; i < len; ++i) {
        if (path[i] == '/') {
            if (!component_valid(path + start, i - start)) return 0;
            start = i + 1;
        }
    }
    return start == len ? directory : component_valid(path + start, len-start);
}

int vmp_child(char path[VMP_PATH_CAP], const char *name, int directory)
{
    char result[VMP_PATH_CAP];
    size_t p, n;
    if (!vmp_path_valid(path, 1) || !name ||
        !component_valid(name, n = strlen(name))) return -1;
    p = strlen(path);
    if (p + n + (directory ? 1u : 0u) >= sizeof(result)) return -1;
    memcpy(result, path, p);
    memcpy(result + p, name, n);
    if (directory) result[p + n++] = '/';
    result[p + n] = 0;
    memcpy(path, result, p + n + 1);
    return 0;
}

int vmp_parent(char path[VMP_PATH_CAP])
{
    size_t len;
    if (!vmp_path_valid(path, 1)) return -1;
    len = strlen(path);
    if (len == root_length(path)) return 0;
    --len;
    while (len && path[len-1] != '/') --len;
    path[len] = 0;
    return 0;
}

int vmp_path_writable(const char *path)
{
    return (vmp_path_valid(path, 0) || vmp_path_valid(path, 1)) &&
        !strncmp(path, VMP_ROOT, sizeof(VMP_ROOT) - 1);
}

int vmp_rebase(char path[VMP_PATH_CAP], const char *from, const char *to)
{
    char result[VMP_PATH_CAP];
    size_t f, t, rest;
    if (!path || !vmp_path_valid(from, 1) || !vmp_path_valid(to, 1)) return -1;
    f = strlen(from); t = strlen(to);
    if (strncmp(path, from, f)) return -1;
    rest = strlen(path + f);
    if (t + rest >= sizeof(result)) return -1;
    memcpy(result, to, t); memcpy(result + t, path + f, rest + 1);
    if (!vmp_path_valid(result, 0) && !vmp_path_valid(result, 1)) return -1;
    memcpy(path, result, t + rest + 1);
    return 0;
}

int vmp_srt_path(char dst[VMP_PATH_CAP], const char *video)
{
    size_t len;
    if (!vmp_path_valid(video, 0) || !vmp_is_mp4(video) ||
        vmp_copy(dst, VMP_PATH_CAP, video)) { if (dst) dst[0]=0; return -1; }
    len = strlen(dst);
    memcpy(dst + len - 4, ".srt", 5);
    return 0;
}

static int round_bounded(uint32_t size, uint32_t alignment, uint32_t limit, uint32_t *out)
{
    uint32_t rounded;
    if (!out || !size || size > limit || !alignment ||
        (alignment & (alignment-1)) || alignment > limit ||
        size > UINT32_MAX - (alignment-1)) return -1;
    rounded = (size + alignment - 1) & ~(alignment - 1);
    if (rounded > limit) return -1;
    *out = rounded;
    return 0;
}

int vmp_round_allocation(uint32_t size, uint32_t alignment, uint32_t *out)
{
    return round_bounded(size,alignment,VMP_MAX_FRAME_BYTES,out);
}

int vmp_round_decoder_allocation(uint32_t size, uint32_t alignment, uint32_t *out)
{
    return round_bounded(size,alignment,VMP_MAX_DECODER_BYTES,out);
}

int vmp_read_window(uint64_t size, uint64_t offset, uint32_t requested, uint32_t *available)
{
    uint64_t remaining;
    if (!available || requested > INT_MAX || offset > size) return -1;
    remaining = size-offset;
    *available = remaining < requested ? (uint32_t)remaining : requested;
    return 0;
}

int vmp_player_handle_valid(int32_t handle, int mapped_user_address)
{
    /* Vita's SDK declares this opaque handle as int. Real firmware may return
       a user-heap pointer with its sign bit set. Negative alone is not failure.
       The frontend checks the kernel memory map, rather than treating error
       constants (for example 0x806A0003) as valid pointers. */
    return handle > 0 || (handle < 0 && mapped_user_address);
}

int vmp_audio_shape(unsigned channels, unsigned rate, unsigned bytes,
                    unsigned *samples)
{
    unsigned count;
    if (!samples || (channels != 1 && channels != 2) ||
        (rate != 48000 && rate != 44100 && rate != 32000 && rate != 24000 &&
         rate != 22050 && rate != 16000 && rate != 12000 && rate != 11025 && rate != 8000) ||
        bytes % (channels * 2)) return -1;
    count = bytes / (channels * 2);
    /* Decoder packets need not meet the hardware port's granularity: the
       audio worker accumulates them into fixed 1024-sample output blocks. */
    if (!count || count > VMP_MAX_AUDIO_FRAMES) return -1;
    *samples = count;
    return 0;
}

static int entry_compare(const void *a, const void *b)
{
    const VmpEntry *left = a, *right = b;
    if (left->directory != right->directory) return right->directory-left->directory;
    return strcmp(left->name, right->name);
}

int vmp_collect(const char *path, VmpEntry *entries, size_t capacity,
                size_t *count, VmpNextEntry next, void *context)
{
    size_t n = 0, seen = 0;
    int ret;
    if (!count) return -1;
    *count = 0;
    if (!entries || !capacity || capacity > VMP_MAX_ENTRIES || !next ||
        !vmp_path_valid(path, 1)) return -1;
    for (;;) {
        VmpEntry item;
        char child[VMP_PATH_CAP];
        memset(&item, 0, sizeof(item));
        ret = next(context, &item);
        if (ret < 0) return -1;
        if (!ret) break;
        if (++seen > VMP_MAX_ENTRIES || !memchr(item.name, 0, sizeof(item.name))) return -1;
        if (!strcmp(item.name, ".") || !strcmp(item.name, "..")) continue;
        if (!item.directory && (!item.regular || (!vmp_is_mp4(item.name) && !vmp_image_kind(item.name)))) continue;
        item.directory = !!item.directory;
        item.regular = !!item.regular;
        if (n == capacity || vmp_copy(child, sizeof(child), path) ||
            vmp_child(child, item.name, item.directory)) return -1;
        entries[n++] = item;
    }
    qsort(entries, n, sizeof(*entries), entry_compare);
    *count = n;
    return 0;
}

void vmp_srt_free(VmpSubtitles *s)
{
    if (s) { free(s->cues); s->cues = NULL; s->count = 0; }
}

/* Bounded physical lines, strict NUL rejection, CRLF support, fixed total budget. */
static int read_line(FILE *f, char *line, size_t cap, size_t *budget)
{
    size_t n = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (!*budget) return -1;
        --*budget;
        if (c == 0) return -1;
        if (c == '\n') break;
        if (n + 1 >= cap) return -1;
        line[n++] = (char)c;
    }
    if (ferror(f)) return -1;
    if (c == EOF && !n) { line[0] = 0; return 0; }
    if (n && line[n-1] == '\r') --n;
    line[n] = 0;
    return 1;
}

static int digit(char c) { return c >= '0' && c <= '9'; }
static int two(const char *s) { return (s[0]-'0')*10 + s[1]-'0'; }

static int timestamp(const char *s, uint64_t *value)
{
    static const size_t positions[] = {0,1,3,4,6,7,9,10,11};
    size_t i;
    for (i = 0; i < sizeof(positions)/sizeof(positions[0]); ++i)
        if (!digit(s[positions[i]])) return -1;
    if (s[2] != ':' || s[5] != ':' || s[8] != ',' ||
        two(s+3) > 59 || two(s+6) > 59) return -1;
    *value = (uint64_t)two(s)*3600000 + (uint64_t)two(s+3)*60000 +
        (uint64_t)two(s+6)*1000 + (unsigned)two(s+9)*10 + (unsigned)(s[11]-'0');
    return 0;
}

static int time_line(const char *line, uint64_t *start, uint64_t *end)
{
    return strlen(line) != 29 || memcmp(line+12, " --> ", 5) ||
        timestamp(line, start) || timestamp(line+17, end) || *end <= *start ? -1 : 0;
}

static int index_line(const char *line)
{
    size_t i, n = strlen(line);
    if (!n || n > 9) return 0;
    for (i = 0; i < n; ++i) if (!digit(line[i])) return 0;
    return 1;
}

/* Remove simple presentation tags without XML parsing or overlapping copies. */
static void clean_tags(char *line)
{
    size_t r = 0, w = 0;
    while (line[r]) {
        if (line[r] == '<') {
            const char *end = strchr(line+r, '>');
            if (end) { r = (size_t)(end-line)+1; continue; }
        }
        if ((unsigned char)line[r] >= 32 || line[r] == '\t') line[w++] = line[r];
        ++r;
    }
    line[w] = 0;
}

int vmp_srt_read(FILE *f, VmpSubtitles *out)
{
    VmpSubtitles temp = {0};
    VmpCue cue;
    char line[VMP_SRT_TEXT_CAP];
    size_t budget = VMP_SRT_MAX_BYTES, capacity = 0;
    int status, first = 1;
    if (!out) return -1;
    vmp_srt_free(out);
    if (!f) return -1;
    for (;;) {
        status = read_line(f, line, sizeof(line), &budget);
        if (status < 0) goto fail;
        if (!status) break;
        if (first) {
            first = 0;
            if (strlen(line) >= 3 && !memcmp(line, "\xef\xbb\xbf", 3))
                memmove(line, line+3, strlen(line+3)+1);
        }
        if (!line[0]) continue;
        if (index_line(line)) {
            if (read_line(f, line, sizeof(line), &budget) != 1) goto fail;
        }
        memset(&cue, 0, sizeof(cue));
        if (time_line(line, &cue.start_ms, &cue.end_ms) ||
            (temp.count && cue.start_ms < temp.cues[temp.count-1].end_ms)) goto fail;
        for (;;) {
            size_t old, len;
            status = read_line(f, line, sizeof(line), &budget);
            if (status < 0) goto fail;
            if (!status || !line[0]) break;
            clean_tags(line);
            old = strlen(cue.text); len = strlen(line);
            if (old + (old ? 1u : 0u) + len >= sizeof(cue.text)) goto fail;
            if (old) cue.text[old++] = '\n';
            memcpy(cue.text + old, line, len+1);
        }
        if (!cue.text[0] || !vmp_utf8_valid(cue.text, strlen(cue.text)) ||
            temp.count == VMP_SRT_MAX_CUES) goto fail;
        if (temp.count == capacity) {
            VmpCue *grown;
            size_t next = capacity ? capacity * 2 : 16;
            if (next > VMP_SRT_MAX_CUES) next = VMP_SRT_MAX_CUES;
            grown = realloc(temp.cues, next * sizeof(*grown));
            if (!grown) goto fail;
            temp.cues = grown; capacity = next;
        }
        temp.cues[temp.count++] = cue;
        if (!status) break;
    }
    if (!temp.count) goto fail;
    *out = temp;
    return 0;
fail:
    vmp_srt_free(&temp);
    return -1;
}

const char *vmp_srt_at(const VmpSubtitles *s, uint64_t time_ms)
{
    size_t lo = 0, hi;
    if (!s || !s->cues) return NULL;
    hi = s->count;
    while (lo < hi) {
        size_t mid = lo + (hi-lo)/2;
        if (s->cues[mid].start_ms <= time_ms) lo = mid+1;
        else hi = mid;
    }
    if (!lo || time_ms >= s->cues[lo-1].end_ms) return NULL;
    return s->cues[lo-1].text;
}
