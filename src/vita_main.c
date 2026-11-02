/* Kirv VitaSDK frontend. Restricted SELF; no patches, networking or bundled modules. */
#include "core.h"
#include "app.h"
#include "ui.h"
#include <vita2d.h>
#include <psp2/avplayer.h>
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/display.h>
#include <psp2/ime_dialog.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/apputil.h>
#include <stdatomic.h>
#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COLOR RGBA8(232,235,242,255)
#define MAX_MEDIA_BYTES (UINT64_C(8) * 1024 * 1024 * 1024)
unsigned int _newlib_heap_size_user = 64 * 1024 * 1024;

typedef VmpEntry Entry;
typedef struct { SceUID fd; const char *path; uint64_t size; atomic_int error; } FileContext;
typedef struct {
    SceAvPlayerHandle player;
    atomic_int stop, failed, volume, stage;
    atomic_uint rate, channels, bytes;
} AudioContext;
static unsigned previous_buttons;
AppTouch app_touch;
static SceTouchPanelInfo touch_panel;
static int touch_down,touch_x,touch_y,touch_last_x,touch_last_y;
char notice[160];
/* Callback threads only update atomics. The UI thread writes at most 32
 * snapshots per playback attempt to one fixed, replaced diagnostic file. */
static atomic_uint diag_reads, diag_bytes, diag_opens, diag_allocs, diag_frames;
static atomic_int diag_io, diag_alloc_error, diag_event;
static atomic_uint decoder_bytes, diag_request_bytes, diag_request_alignment;
static FILE *diag_file;
static unsigned diag_lines;

static void diag_snapshot(const char *stage, int active, uint64_t time, int io)
{
    if (!diag_file || diag_lines++ >= 32) return;
    fprintf(diag_file,"%s active=%d time=%llu io_stage=%d native_io=%08X opens=%u reads=%u bytes=%u allocs=%u frames=%u alloc_error=%08X event=%08X\n",
        stage,active,(unsigned long long)time,io,(unsigned)atomic_load(&diag_io),
        atomic_load(&diag_opens),atomic_load(&diag_reads),atomic_load(&diag_bytes),
        atomic_load(&diag_allocs),atomic_load(&diag_frames),
        (unsigned)atomic_load(&diag_alloc_error),(unsigned)atomic_load(&diag_event));
    fprintf(diag_file,"decoder_bytes=%u request_bytes=%u request_alignment=%u\n",
        atomic_load(&decoder_bytes),atomic_load(&diag_request_bytes),atomic_load(&diag_request_alignment));
    fflush(diag_file);
}

static void player_event(void *context, int32_t event, int32_t source, void *data)
{
    (void)context; (void)source; (void)data;
    atomic_store(&diag_event,event);
}

unsigned app_buttons(void)
{
    SceCtrlData pad;
    SceTouchData touch;
    unsigned now, edge;
    memset(&app_touch,0,sizeof(app_touch)); memset(&touch,0,sizeof(touch));
    if(sceTouchPeek(SCE_TOUCH_PORT_FRONT,&touch,1)>0) {
        if(touch.reportNum && touch.reportNum<=SCE_TOUCH_MAX_REPORT) {
            int x=(touch.report[0].x-touch_panel.minDispX)*960/(touch_panel.maxDispX-touch_panel.minDispX+1);
            int y=(touch.report[0].y-touch_panel.minDispY)*544/(touch_panel.maxDispY-touch_panel.minDispY+1);
            if(x<0) x=0;
            if(x>959) x=959;
            if(y<0) y=0;
            if(y>543) y=543;
            if(!touch_down) { touch_x=x; touch_y=y; }
            if(touch_down) { app_touch.down=1; app_touch.dx=x-touch_last_x; app_touch.dy=y-touch_last_y; }
            touch_last_x=x; touch_last_y=y; touch_down=1;
        } else if(touch_down) {
            int dx=touch_last_x-touch_x,dy=touch_last_y-touch_y;
            touch_down=0; app_touch.x=touch_last_x; app_touch.y=touch_last_y;
            if(abs(dx)>80 && abs(dx)>abs(dy)) app_touch.swipe=dx<0?1:-1;
            else if(abs(dx)<35 && abs(dy)<35) app_touch.tap=1;
        }
    }
    memset(&pad, 0, sizeof(pad));
    if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0) return 0;
    now = pad.buttons; edge = now & ~previous_buttons; previous_buttons = now;
    return edge;
}

unsigned app_held(void)
{
    return previous_buttons;
}

static void text(int x, int y, const char *s)
{
    if (s) ui_text(x,y,.9f,COLOR,s,900);
}

void app_message(const char *s)
{
    (void)vmp_copy(notice, sizeof(notice), s);
}

static void message_code(const char *stage, int code, int io_error)
{
    (void)snprintf(notice, sizeof(notice), "%s: 0x%08X (IO %d)", stage, (unsigned)code, io_error);
}

void app_frame_begin(void)
{
    vita2d_start_drawing();
    vita2d_clear_screen();
}

void app_frame_end(void)
{
    vita2d_end_drawing();
    vita2d_wait_rendering_done();
    vita2d_swap_buffers();
    /* The display queue callback in vita2d already waits for vblank. */
}

int app_text_input(const char *title, const char *initial, char *out, size_t cap)
{
    static SceWChar16 ime_title[64], ime_initial[65], ime_input[65];
    SceImeDialogParam param;
    SceImeDialogResult result;
    int status = -1;
    unsigned drain;
    if (!out || !cap) return -1;
    out[0] = 0;
    memset(ime_input, 0, sizeof(ime_input));
    if (vmp_utf8_to_utf16(title, (uint16_t *)ime_title, sizeof(ime_title)/sizeof(*ime_title)) ||
        vmp_utf8_to_utf16(initial ? initial : "", (uint16_t *)ime_initial, sizeof(ime_initial)/sizeof(*ime_initial)))
        return -1;
    sceImeDialogParamInit(&param);
    param.supportedLanguages = 0x0001FFFF; param.languagesForced = SCE_FALSE;
    param.type = SCE_IME_TYPE_DEFAULT; param.option = 0;
    param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
    param.title = ime_title; param.maxTextLength = 64;
    param.initialText = ime_initial; param.inputTextBuffer = ime_input;
    if (sceImeDialogInit(&param) < 0) return -1;
    while (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
        vita2d_start_drawing(); vita2d_clear_screen(); ui_background(); ui_header("Videoteca"); vita2d_end_drawing();
        vita2d_common_dialog_update(); vita2d_swap_buffers();
    }
    memset(&result, 0, sizeof(result));
    if (sceImeDialogGetResult(&result) >= 0) {
        if (result.button != SCE_IME_DIALOG_BUTTON_ENTER) status = 1;
        else status = vmp_utf16_to_utf8((const uint16_t *)ime_input, out, cap) ? -1 : 0;
    }
    (void)sceImeDialogTerm();
    /* Buttons pressed to close the dialog must not reach the browser as new presses. */
    for (drain = 0; drain < 120; ++drain) {
        SceCtrlData pad; memset(&pad, 0, sizeof(pad));
        if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0 || !pad.buttons) break;
        sceKernelDelayThread(16000);
    }
    previous_buttons = 0; touch_down = 0;
    return status;
}

static uint64_t entry_time(const SceIoStat *st)
{
    return vmp_time_key(st->st_mtime.year, st->st_mtime.month, st->st_mtime.day,
                        st->st_mtime.hour, st->st_mtime.minute, st->st_mtime.second);
}

static int next_entry(void *context, VmpEntry *out)
{
    SceIoDirent item;
    SceUID dir = *(SceUID *)context;
    int ret;
    memset(&item, 0, sizeof(item));
    ret = sceIoDread(dir, &item);
    if (ret <= 0) return ret;
    if (!memchr(item.d_name, 0, sizeof(item.d_name)) ||
        vmp_copy(out->name, sizeof(out->name), item.d_name)) return -1;
    out->directory = SCE_S_ISDIR(item.d_stat.st_mode);
    out->regular = SCE_S_ISREG(item.d_stat.st_mode);
    out->time = entry_time(&item.d_stat);
    return 1;
}

/* Bounded recursive walk: depth, entries read per folder and results are all capped. */
static void walk_images(const char *dir, int depth, VmpEntry *entries, char (*paths)[VMP_PATH_CAP],
                        size_t cap, size_t *count)
{
    SceUID handle;
    if (depth > 4 || *count >= cap || (handle = sceIoDopen(dir)) < 0) return;
    for (unsigned seen = 0; seen < VMP_MAX_ENTRIES && *count < cap; ++seen) {
        SceIoDirent item;
        char child[VMP_PATH_CAP];
        memset(&item, 0, sizeof(item));
        if (sceIoDread(handle, &item) <= 0) break;
        if (!memchr(item.d_name, 0, sizeof(item.d_name)) || !strcmp(item.d_name, ".") ||
            !strcmp(item.d_name, "..") || vmp_copy(child, sizeof(child), dir)) continue;
        if (SCE_S_ISDIR(item.d_stat.st_mode)) {
            if (!vmp_child(child, item.d_name, 1)) walk_images(child, depth + 1, entries, paths, cap, count);
        } else if (SCE_S_ISREG(item.d_stat.st_mode) && vmp_image_kind(item.d_name) &&
                   !vmp_child(child, item.d_name, 0) &&
                   !vmp_copy(entries[*count].name, sizeof(entries[*count].name), item.d_name)) {
            entries[*count].directory = 0; entries[*count].regular = 1;
            entries[*count].time = entry_time(&item.d_stat);
            vmp_copy(paths[*count], VMP_PATH_CAP, child);
            ++*count;
        }
    }
    (void)sceIoDclose(handle);
}

static const char *picture_root;
static int photo_mount_tried;

/* Restricted apps may not read ux0:picture directly; photo0: is the system's photo mount. */
static int try_open(FILE *log, const char *path)
{
    SceUID dir = sceIoDopen(path);
    if (log) fprintf(log, "dopen %s = %08X\n", path, (unsigned)dir);
    if (dir >= 0) (void)sceIoDclose(dir);
    return dir;
}

/* Each attempt is written to pictures.log so a failure on hardware can be diagnosed. */
static int find_pictures(FILE *log)
{
    int first = try_open(log, VMP_PICTURES);
    if (first >= 0) { picture_root = VMP_PICTURES; return 0; }
    if (!photo_mount_tried) {
        SceAppUtilInitParam init; SceAppUtilBootParam boot;
        int ret;
        photo_mount_tried = 1;
        memset(&init, 0, sizeof(init)); memset(&boot, 0, sizeof(boot));
        ret = sceAppUtilInit(&init, &boot);
        if (log) fprintf(log, "sceAppUtilInit = %08X\n", (unsigned)ret);
        ret = sceAppUtilPhotoMount();
        if (log) fprintf(log, "sceAppUtilPhotoMount = %08X\n", (unsigned)ret);
    }
    if (try_open(log, VMP_PHOTOS) >= 0 || try_open(log, "photo0:") >= 0) { picture_root = VMP_PHOTOS; return 0; }
    picture_root = NULL;
    return first;
}

int app_list_images(VmpEntry *entries, char (*paths)[VMP_PATH_CAP], size_t cap, size_t *count)
{
    FILE *log = fopen("ux0:data/VMPH00001/pictures.log", "w");
    int access = find_pictures(log);
    size_t system;
    *count = 0;
    if (picture_root) walk_images(picture_root, 0, entries, paths, cap, count);
    system = *count;
    walk_images(VMP_ROOT, 0, entries, paths, cap, count);
    if (log) {
        fprintf(log, "root=%s system_images=%u library_images=%u\n", picture_root ? picture_root : "-",
                (unsigned)system, (unsigned)(*count - system));
        if (picture_root) {
            char child[VMP_PATH_CAP];
            vmp_copy(child, sizeof(child), picture_root);
            if (!vmp_child(child, "SCREENSHOT", 1)) (void)try_open(log, child);
        }
        fclose(log);
    }
    return access;
}

int app_list(const char *path, Entry *entries, size_t *count)
{
    SceUID dir;
    int ret;
    *count = 0;
    if (!vmp_path_valid(path, 1)) {
        app_message(L("Ruta de biblioteca inválida.","Invalid library path."));
        return -1;
    }
    dir = sceIoDopen(path);
    if (dir < 0) {
        message_code(L("Abrir carpeta","Open folder"), dir, 0);
        return -1;
    }
    ret = vmp_collect(path, entries, VMP_MAX_ENTRIES, count, next_entry, &dir);
    if (sceIoDclose(dir) < 0) ret = -1;
    if (ret < 0) {
        *count = 0;
        app_message(L("Carpeta no disponible: error, nombre demasiado largo o más de 1024 entradas.","Folder unavailable: error, name too long or more than 1024 entries."));
        return -1;
    }
    return 0;
}

static int open_video(void *opaque, const char *name)
{
    FileContext *ctx = opaque;
    SceIoStat st;
    int status;
    if (!ctx) return -1;
    atomic_fetch_add(&diag_opens,1);
    if (!name || strcmp(name, ctx->path) || !vmp_path_valid(name, 0) || !vmp_is_mp4(name)) {
        atomic_store(&ctx->error, 1); return -1;
    }
    if (ctx->fd >= 0) { atomic_store(&ctx->error, 2); return -1; }
    memset(&st, 0, sizeof(st));
    status=sceIoGetstat(name, &st);
    if(status<0) atomic_store(&diag_io,status);
    if (status < 0 || !SCE_S_ISREG(st.st_mode) ||
        st.st_size <= 0 || (uint64_t)st.st_size > MAX_MEDIA_BYTES) {
        atomic_store(&ctx->error, 3); return -1;
    }
    ctx->size = (uint64_t)st.st_size;
    ctx->fd = sceIoOpen(name, SCE_O_RDONLY, 0);
    if (ctx->fd < 0) { atomic_store(&diag_io,ctx->fd); atomic_store(&ctx->error, 4); return -1; }
    return 0;
}

static int close_video(void *opaque)
{
    FileContext *ctx = opaque;
    int result = 0;
    if (!ctx) return -1;
    if (ctx->fd >= 0) result = sceIoClose(ctx->fd);
    ctx->fd = -1;
    return result;
}

static int read_video(void *opaque, uint8_t *buffer, uint64_t offset, uint32_t bytes)
{
    FileContext *ctx = opaque;
    int ret;
    uint32_t available;
    if (!ctx) return -1;
    if (ctx->fd < 0 || (!buffer && bytes) || vmp_read_window(ctx->size, offset, bytes, &available)) {
        atomic_store(&ctx->error, 5); return -1;
    }
    /* Decoder probes may extend past EOF. A short read is normal, not an IO error. */
    if (!available) return 0;
    ret = sceIoPread(ctx->fd, buffer, available, (SceOff)offset);
    atomic_fetch_add(&diag_reads,1);
    if(ret>0) atomic_fetch_add(&diag_bytes,(unsigned)ret);
    if (ret < 0) { atomic_store(&diag_io,ret); atomic_store(&ctx->error, 6); }
    return ret < 0 ? -1 : ret;
}

static uint64_t video_size(void *opaque)
{
    FileContext *ctx = opaque;
    return ctx && ctx->fd >= 0 ? ctx->size : 0;
}

static void *allocate(void *unused, uint32_t alignment, uint32_t bytes)
{
    uint32_t checked;
    void *ptr;
    (void)unused;
    atomic_fetch_add(&diag_allocs,1);
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    if (vmp_round_allocation(bytes, alignment, &checked)) { atomic_store(&diag_alloc_error,-1); return NULL; }
    ptr=memalign(alignment, checked);
    if(!ptr) atomic_store(&diag_alloc_error,-2);
    return ptr;
}

static void deallocate(void *unused, void *ptr) { (void)unused; free(ptr); }

static void *allocate_frame(void *unused, uint32_t alignment, uint32_t bytes)
{
    SceKernelAllocMemBlockOpt opt;
    SceUID uid;
    void *ptr = NULL;
    uint32_t rounded;
    unsigned used;
    (void)unused;
    atomic_fetch_add(&diag_frames,1);
    atomic_store(&diag_request_bytes,bytes);
    atomic_store(&diag_request_alignment,alignment);
    if (alignment < 0x40000) alignment = 0x40000;
    if (vmp_round_decoder_allocation(bytes, alignment, &rounded)) { atomic_store(&diag_alloc_error,-3); return NULL; }
    used=atomic_load(&decoder_bytes);
    do {
        if(used>VMP_MAX_DECODER_BYTES-rounded) { atomic_store(&diag_alloc_error,-5); return NULL; }
    } while(!atomic_compare_exchange_weak(&decoder_bytes,&used,used+rounded));
    memset(&opt, 0, sizeof(opt));
    opt.size = sizeof(opt); opt.attr = 4; opt.alignment = alignment;
    uid = sceKernelAllocMemBlock("vmp_frame", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, rounded, &opt);
    if (uid < 0) { atomic_fetch_sub(&decoder_bytes,rounded); atomic_store(&diag_alloc_error,uid); return NULL; }
    if (sceKernelGetMemBlockBase(uid, &ptr) < 0 || !ptr ||
        sceGxmMapMemory(ptr, rounded, SCE_GXM_MEMORY_ATTRIB_READ) < 0) {
        if(sceKernelFreeMemBlock(uid)>=0) atomic_fetch_sub(&decoder_bytes,rounded);
        atomic_store(&diag_alloc_error,-4);
        return NULL;
    }
    return ptr;
}

static void free_frame(void *unused, void *ptr)
{
    SceUID uid;
    SceKernelMemBlockInfo info;
    (void)unused;
    if (!ptr || (uid = sceKernelFindMemBlockByAddr(ptr, 0)) < 0) return;
    memset(&info,0,sizeof(info)); info.size=sizeof(info);
    if(sceKernelGetMemBlockInfoByAddr(ptr,&info)<0 || info.mappedBase!=ptr ||
       info.type!=SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW) return;
    vita2d_wait_rendering_done();
    /* If GPU unmapping fails, retain the allocation rather than free mapped memory. */
    if (sceGxmUnmapMemory(ptr) >= 0 && sceKernelFreeMemBlock(uid)>=0)
        atomic_fetch_sub(&decoder_bytes,info.mappedSize);
}

static int audio_thread(SceSize args, void *argp)
{
    AudioContext *ctx;
    int port = -1;
    unsigned old_rate = 0, old_channels = 0,buffer_index=0,filled=0;
    /* Decoder-owned PCM may be reused on the next GetAudioData call.
       Own two output buffers and keep the hardware chunk size stable. */
    int16_t pcm[2][1024*2];
    int old_volume=-1;
    if (args != sizeof(ctx)) return -1;
    memcpy(&ctx, argp, sizeof(ctx));
    while (!atomic_load(&ctx->stop)) {
        SceAvPlayerFrameInfo frame;
        unsigned samples, channels, rate, bytes;
        memset(&frame, 0, sizeof(frame));
        if (!sceAvPlayerGetAudioData(ctx->player, &frame)) {
            sceKernelDelayThread(2000);
            continue;
        }
        channels = frame.details.audio.channelCount;
        rate = frame.details.audio.sampleRate;
        bytes = frame.details.audio.size;
        atomic_store(&ctx->rate,rate); atomic_store(&ctx->channels,channels); atomic_store(&ctx->bytes,bytes);
        if (!frame.pData || vmp_audio_shape(channels, rate, bytes, &samples)) {
            atomic_store(&ctx->stage,1); atomic_store(&ctx->failed,-1); break;
        }
        if (rate != old_rate || channels != old_channels) {
            if (port >= 0) { sceAudioOutOutput(port, NULL); sceAudioOutReleasePort(port); port = -1; }
            port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, 1024,
                (int)rate, channels == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO);
            if (port < 0) { atomic_store(&ctx->stage,2); atomic_store(&ctx->failed,port); break; }
            old_rate = rate; old_channels = channels; old_volume=-1; filled=0;
        }
        {
            int volume=atomic_load(&ctx->volume);
            if(volume!=old_volume) {
                int values[2]={volume*32768/100,volume*32768/100};
                int result=sceAudioOutSetVolume(port,SCE_AUDIO_VOLUME_FLAG_L_CH|SCE_AUDIO_VOLUME_FLAG_R_CH,values);
                if(result<0) {
                    atomic_store(&ctx->stage,3); atomic_store(&ctx->failed,result); break;
                }
                old_volume=volume;
            }
        }
        for(unsigned offset=0;offset<samples && !atomic_load(&ctx->stop);) {
            unsigned take=samples-offset;
            if(take>1024-filled) take=1024-filled;
            memcpy(pcm[buffer_index]+filled*channels,(const int16_t *)frame.pData+offset*channels,take*channels*2);
            filled+=take; offset+=take;
            if(filled==1024) {
                int result=sceAudioOutOutput(port,pcm[buffer_index]);
                if(result<0) { atomic_store(&ctx->stage,4); atomic_store(&ctx->failed,result); break; }
                buffer_index^=1; filled=0;
            }
        }
        if(atomic_load(&ctx->failed)) break;
    }
    if (port >= 0) { sceAudioOutOutput(port, NULL); sceAudioOutReleasePort(port); }
    return 0;
}


static int playback(const char *path)
{
    FileContext file;
    AudioContext audio;
    AudioContext *audio_arg=&audio;
    SceAvPlayerInitData init;
    SceAvPlayerHandle player=-1;
    SceUID thread=-1;
    VmpSubtitles subtitles={0};
    vita2d_texture *cached=NULL;
    char srt[VMP_PATH_CAP];
    const char *name=strrchr(path,'/');
    const VmpProgress *saved=vmp_progress(&library,path);
    uint64_t resume_at=(saved && !saved->watched && library.resume)?saved->position:0;
    uint64_t duration=0,position=0,last_time=0,last_progress=0,interaction=0,checkpoint=0;
    int ret=-1,player_created=0,thread_started=0,active_seen=0,paused=0,have_video=0,resume_pending=resume_at>0;
    int show_subtitles=(int)library.subtitles,controls_visible=1;
    FILE *input;
    uint64_t diagnostic_tick=0;
    atomic_store(&diag_reads,0); atomic_store(&diag_bytes,0); atomic_store(&diag_opens,0);
    atomic_store(&diag_allocs,0); atomic_store(&diag_frames,0); atomic_store(&diag_io,0);
    atomic_store(&diag_alloc_error,0); atomic_store(&diag_event,0);
    memset(&init,0,sizeof(init)); memset(&file,0,sizeof(file));
    file.fd=-1; file.path=path; atomic_init(&file.error,0);
    memset(&audio,0,sizeof(audio)); atomic_init(&audio.stop,0); atomic_init(&audio.failed,0); atomic_init(&audio.volume,(int)library.volume);
    atomic_init(&audio.stage,0); atomic_init(&audio.rate,0); atomic_init(&audio.channels,0); atomic_init(&audio.bytes,0);
    if(!vmp_path_valid(path,0) || !vmp_is_mp4(path)) return -1;
    diag_lines=0; diag_file=fopen("ux0:data/VMPH00001/playback.log","w");
    if(diag_file) { fprintf(diag_file,"Kirv DEV 0.21\npath=%s\n",path); fflush(diag_file); }
    diag_snapshot("begin",0,0,0);
    if(!vmp_srt_path(srt,path) && (input=fopen(srt,"rb"))!=NULL) {
        int parsed=vmp_srt_read(input,&subtitles);
        if(fclose(input) || parsed) { app_message(L("Subtítulos inválidos o demasiado grandes.","Invalid or too large subtitles.")); goto cleanup; }
    }
    /* Cache is owned by the UI; decoder buffers are used only until rendering
       finishes. This permits redraws while paused and hidden controls. */
    cached=vita2d_create_empty_texture_rendertarget(960,544,SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
    if(!cached) { app_message(L("No hay memoria para la pantalla de reproducción.","Not enough memory for the playback screen.")); goto cleanup; }
    init.memoryReplacement.allocate=allocate; init.memoryReplacement.deallocate=deallocate;
    init.memoryReplacement.allocateTexture=allocate_frame; init.memoryReplacement.deallocateTexture=free_frame;
    init.fileReplacement.objectPointer=&file; init.fileReplacement.open=open_video;
    init.fileReplacement.close=close_video; init.fileReplacement.readOffset=read_video; init.fileReplacement.size=video_size;
    init.basePriority=125; init.numOutputVideoFrameBuffers=3; init.autoStart=SCE_TRUE;
    init.eventReplacement.eventCallback=player_event;
    player=sceAvPlayerInit(&init);
    diag_snapshot("init-result",player,0,atomic_load(&file.error));
    {
        int mapped=player<0 && sceKernelFindMemBlockByAddr((const void *)(uintptr_t)(uint32_t)player,0)>=0;
        if(!vmp_player_handle_valid(player,mapped)) { message_code(L("Inicializar MP4","Initialize MP4"),player,atomic_load(&file.error)); goto cleanup; }
    }
    player_created=1; audio.player=player;
    {
        int added=sceAvPlayerAddSource(player,path);
        diag_snapshot("add-source-result",added,0,atomic_load(&file.error));
        if(added<0) { message_code(L("Abrir MP4","Open MP4"),added,atomic_load(&file.error)); goto cleanup; }
    }
    (void)sceAvPlayerSetLooping(player,SCE_FALSE);
    /* Restore the user-thread priority used by the version tested on hardware.
       0x10000080 introduced in 0.17 prevented entry into the playback loop. */
    thread=sceKernelCreateThread("vmp_audio",audio_thread,0x10000100,0x10000,0,0,NULL);
    diag_snapshot("audio-thread-create",thread,0,0);
    if(thread<0) { message_code(L("Crear hilo de audio","Create audio thread"),thread,0); goto cleanup; }
    {
        int result=sceKernelStartThread(thread,sizeof(audio_arg),&audio_arg);
        diag_snapshot("audio-thread-start",result,0,0);
        if(result<0) { message_code(L("Iniciar hilo de audio","Start audio thread"),result,0); goto cleanup; }
    }
    thread_started=1;
    last_progress=interaction=checkpoint=sceKernelGetProcessTimeWide();
    for(;;) {
        SceAvPlayerFrameInfo frame;
        unsigned buttons=app_buttons();
        uint64_t time_ms=sceAvPlayerCurrentTime(player),now=sceKernelGetProcessTimeWide();
        int active=sceAvPlayerIsActive(player),have_frame;
        if(!have_video && now-diagnostic_tick>=UINT64_C(1000000)) {
            diagnostic_tick=now;
            diag_snapshot("poll",active,time_ms,atomic_load(&file.error));
        }
        if(buttons) { interaction=now; controls_visible=1; }
        if(app_touch.tap) {
            int x=app_touch.x,y=app_touch.y;
            interaction=now;
            if(!controls_visible) controls_visible=1;
            else if(y<55 && x<90) buttons|=SCE_CTRL_CIRCLE;
            else if(y>=392 && y<=428 && x>=130 && x<=830 && duration) {
                uint64_t dest=duration*(unsigned)(x-130)/700;
                if(dest>=duration) dest=duration>1000?duration-1000:0;
                if(sceAvPlayerJumpToTime(player,dest)>=0) last_progress=now;
            } else if(y>=430 && y<492) {
                if(x>=440 && x<520) buttons|=SCE_CTRL_CROSS;
                else if(x>=360 && x<440) buttons|=SCE_CTRL_LTRIGGER;
                else if(x>=520 && x<605) buttons|=SCE_CTRL_RTRIGGER;
                else if(x>=680) buttons|=SCE_CTRL_SQUARE;
                else if(x<100) buttons|=SCE_CTRL_DOWN;
                else if(x<190) buttons|=SCE_CTRL_UP;
            } else if(y<392) buttons|=SCE_CTRL_TRIANGLE; /* tapping the picture hides the controls; pause is the centre button */
        }
        if(buttons&SCE_CTRL_CIRCLE) { ret=0; break; }
        if(buttons&SCE_CTRL_SQUARE) show_subtitles=!show_subtitles;
        if(buttons&SCE_CTRL_TRIANGLE) { controls_visible=0; interaction=now-(uint64_t)vmp_hide_seconds(&library)*1000000; }
        if(buttons&SCE_CTRL_CROSS) {
            int changed=paused?sceAvPlayerResume(player):sceAvPlayerPause(player);
            if(changed>=0) paused=!paused;
            /* Pausing is a natural moment to keep progress if the app is then closed. */
            if(changed>=0 && paused && have_video && position>0 && !vmp_remember(&library,path,position,duration,0)) { (void)app_save(); checkpoint=now; }
        }
        if(buttons&SCE_CTRL_UP) library.volume=library.volume<=90?library.volume+10:100;
        if(buttons&SCE_CTRL_DOWN) library.volume=library.volume>=10?library.volume-10:0;
        atomic_store(&audio.volume,(int)library.volume);
        if(buttons&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT|SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER)) {
            uint64_t step=(uint64_t)vmp_seek_seconds(&library)*1000,dest=time_ms;
            if(buttons&(SCE_CTRL_LEFT|SCE_CTRL_LTRIGGER)) dest=dest>step?dest-step:0;
            else if(dest<=UINT64_MAX-step) dest+=step;
            if(duration && dest>=duration) dest=duration>1000?duration-1000:0;
            if(sceAvPlayerJumpToTime(player,dest)>=0) last_progress=now;
        }
        if(atomic_load(&audio.failed)) {
            const char *stages[]={"Audio",L("Formato PCM","PCM format"),L("Abrir salida de audio","Open audio output"),L("Volumen de audio","Audio volume"),L("Enviar audio","Send audio")};
            int stage=atomic_load(&audio.stage);
            message_code(stages[stage>=0 && stage<=4?stage:0],atomic_load(&audio.failed),0); break;
        }
        if(!active && atomic_load(&diag_alloc_error)) {
            message_code(L("Memoria del decodificador","Decoder memory"),atomic_load(&diag_alloc_error),atomic_load(&file.error));
            break;
        }
        if(active) active_seen=1;
        if(active_seen && have_video && !active && !paused) { ret=1; break; }
        if(active_seen && !have_video && !active && now-interaction>UINT64_C(1000000)) {
            app_message(L("El decodificador no admite este archivo. Usa una copia H.264 / AAC-LC.","The decoder does not support this file. Use an H.264 / AAC-LC copy.")); break;
        }
        if(active && !duration) {
            for(unsigned i=0;i<8;i++) {
                SceAvPlayerStreamInfo info; memset(&info,0,sizeof(info));
                if(sceAvPlayerGetStreamInfo(player,i,&info)<0) break;
                if(info.duration>duration && info.duration<=UINT64_C(604800000)) duration=info.duration;
            }
        }
        memset(&frame,0,sizeof(frame)); have_frame=sceAvPlayerGetVideoData(player,&frame);
        if(have_frame || time_ms!=last_time || paused) last_progress=now;
        last_time=time_ms;
        if(time_ms<=UINT64_C(604800000) && have_video) position=duration && time_ms>duration?duration:time_ms;
        if(now-last_progress>UINT64_C(15000000)) {
            diag_snapshot("timeout",active,time_ms,atomic_load(&file.error));
            app_message(L("Sin respuesta en 15 s. Diagnóstico guardado en playback.log.","No response for 15 s. Diagnostics saved to playback.log.")); break;
        }
        if(have_frame) {
            vita2d_texture texture; unsigned w=frame.details.video.width,h=frame.details.video.height;
            if(!frame.pData || !w || !h || w>1280 || h>736) { app_message(L("Resolución no compatible (máximo 1280 x 736).","Unsupported resolution (max 1280 x 736).")); break; }
            memset(&texture,0,sizeof(texture));
            if(sceGxmTextureInitLinear(&texture.gxm_tex,frame.pData,SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1,w,h,0)<0) { app_message(L("No se pudo preparar el fotograma.","Could not prepare the frame.")); break; }
            vita2d_start_drawing_advanced(cached,0); vita2d_clear_screen();
            {
                float sx=960.0f/(float)w,sy=544.0f/(float)h;
                float scale=library.fill?(sx>sy?sx:sy):(sx<sy?sx:sy);
                vita2d_texture_set_filters(&texture,SCE_GXM_TEXTURE_FILTER_LINEAR,SCE_GXM_TEXTURE_FILTER_LINEAR);
                vita2d_draw_texture_scale(&texture,(960-w*scale)/2,(544-h*scale)/2,scale,scale);
            }
            vita2d_end_drawing(); vita2d_wait_rendering_done(); have_video=1;
            if(resume_pending) {
                resume_pending=0;
                if(!duration || resume_at+2000<duration) (void)sceAvPlayerJumpToTime(player,resume_at);
            }
        }
        if(!paused && now-interaction>(uint64_t)vmp_hide_seconds(&library)*1000000) controls_visible=0;
        app_frame_begin();
        if(have_video) vita2d_draw_texture(cached,0,0);
        else { ui_background(); ui_text(300,250,1.1f,UI_WHITE,L("Preparando vídeo...","Loading video..."),600); }
        if(show_subtitles) {
            const char *cue=vmp_srt_at(&subtitles,time_ms);
            if(cue && cue[0]) {
                int y=controls_visible||paused?348:468;
                vita2d_draw_rectangle(20,(float)y-25,920,70,RGBA8(0,0,0,175));
                text(30,y,cue);
            }
        }
        if(controls_visible || paused || !have_video) ui_player(name?name+1:path,time_ms,duration,paused,library.volume,show_subtitles,subtitles.count>0,vmp_seek_seconds(&library));
        app_frame_end(); sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
        if(have_video && position>0 && now-checkpoint>UINT64_C(10000000)) {
            if(!vmp_remember(&library,path,position,duration,0)) (void)app_save();
            checkpoint=now;
        }
    }
cleanup:
    diag_snapshot("cleanup",ret,position,atomic_load(&file.error));
    atomic_store(&audio.stop,1);
    if(thread_started) {
        SceUInt timeout=2000000;
        if(sceKernelWaitThreadEnd(thread,NULL,&timeout)<0) sceKernelExitProcess(1);
    }
    if(thread>=0) sceKernelDeleteThread(thread);
    if(diag_file) {
        fprintf(diag_file,"audio stage=%d error=%08X rate=%u channels=%u packet_bytes=%u\n",
            atomic_load(&audio.stage),(unsigned)atomic_load(&audio.failed),atomic_load(&audio.rate),
            atomic_load(&audio.channels),atomic_load(&audio.bytes)); fflush(diag_file);
    }
    vita2d_wait_rendering_done();
    if(player_created) { sceAvPlayerStop(player); if(sceAvPlayerClose(player)<0) sceKernelExitProcess(1); }
    diag_snapshot("closed",ret,position,atomic_load(&file.error));
    if(diag_file) { fclose(diag_file); diag_file=NULL; }
    close_video(&file); vmp_srt_free(&subtitles);
    if(cached && have_video) ui_save_thumbnail(path,cached);
    if(cached) vita2d_free_texture(cached);
    if(have_video && (position || ret==1)) {
        if(ret==1 && duration) position=duration;
        if(!vmp_remember(&library,path,position,duration,ret==1)) (void)app_save();
    }
    return ret;
}

int main(void)
{
    int result=1,graphics=0,ui=0;
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    memset(&touch_panel,0,sizeof(touch_panel)); touch_panel.maxDispX=1919; touch_panel.maxDispY=1087;
    (void)sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT,&touch_panel);
    if(touch_panel.maxDispX<=touch_panel.minDispX || touch_panel.maxDispY<=touch_panel.minDispY) {
        touch_panel.minDispX=touch_panel.minDispY=0; touch_panel.maxDispX=1919; touch_panel.maxDispY=1087;
    }
    (void)sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,SCE_TOUCH_SAMPLING_STATE_START);
    if(sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER)<0) goto done;
    /* The keyboard dialog is optional: a failure here only disables "Nueva carpeta". */
    (void)sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
    if(vita2d_init()<0) goto done;
    graphics=1; vita2d_set_clear_color(RGBA8(0,0,0,255));
    ui=1; if(ui_init()<0) goto done;
    result=app_browser(playback);
done:
    if(ui) ui_fini();
    if(graphics) vita2d_fini();
    /* Process teardown owns module unloading, avoiding the 0.12 thread race. */
    sceKernelExitProcess(result);
    return result;
}
