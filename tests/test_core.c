#include "core.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(expr) do { assert(expr); ++checks; } while (0)

static int parse(const void *bytes, size_t len, VmpSubtitles *out)
{
    FILE *f = tmpfile();
    int result;
    assert(f);
    assert(fwrite(bytes, 1, len, f) == len);
    rewind(f);
    result = vmp_srt_read(f, out);
    assert(fclose(f) == 0);
    return result;
}

static void paths(void)
{
    char path[VMP_PATH_CAP] = VMP_ROOT, saved[VMP_PATH_CAP], dst[VMP_PATH_CAP];
    char long_name[600];
    memset(long_name, 'a', sizeof(long_name)-1); long_name[sizeof(long_name)-1]=0;
    CHECK(vmp_is_mp4("episode.MP4")); CHECK(!vmp_is_mp4(".mp4"));
    CHECK(!vmp_is_mp4("")); CHECK(!vmp_is_mp4(NULL));
    CHECK(!vmp_is_mp4("video.mp4.exe"));
    CHECK(!vmp_is_mp4("movie.verylongextension"));
    CHECK(vmp_path_valid(VMP_ROOT, 1)); CHECK(!vmp_path_valid(VMP_ROOT, 0));
    CHECK(!vmp_path_valid("ux0:/video/", 1));
    CHECK(!vmp_path_valid("ux0:data/VMPH00001/videoX/a.mp4", 0));
    CHECK(!vmp_path_valid("ur0:/video/x.mp4", 0));
    CHECK(!vmp_path_valid("ux0:/videoX/x.mp4", 0));
    CHECK(!vmp_path_valid("ux0:data/VMPH00001/video/../data/", 1));
    CHECK(!vmp_path_valid("ux0:data/VMPH00001/video/./x.mp4", 0));
    CHECK(!vmp_path_valid("ux0:data/VMPH00001/video//x.mp4", 0));
    CHECK(!vmp_path_valid("ux0:data/VMPH00001/video/a\\x.mp4", 0));
    CHECK(!vmp_child(path,"Serie",1)); CHECK(!vmp_child(path,"Temporada 01",1));
    CHECK(!strcmp(path,"ux0:data/VMPH00001/video/Serie/Temporada 01/"));
    CHECK(!vmp_copy(saved,sizeof(saved),path));
    CHECK(vmp_child(path,"..",1)<0 && !strcmp(path,saved));
    CHECK(vmp_child(path,"http://example.test",0)<0 && !strcmp(path,saved));
    CHECK(vmp_child(path,long_name,1)<0 && !strcmp(path,saved));
    CHECK(vmp_child(path,"bad\nname",1)<0 && !strcmp(path,saved));
    CHECK(!vmp_child(path,"01 - Capitulo.mp4",0));
    CHECK(!vmp_srt_path(dst,path)); CHECK(strstr(dst,"01 - Capitulo.srt")!=NULL);
    CHECK(vmp_srt_path(dst,"ux0:data/VMPH00001/video/a")<0 && !dst[0]);
    CHECK(!vmp_copy(path,sizeof(path),saved));
    CHECK(!vmp_parent(path)); CHECK(!strcmp(path,"ux0:data/VMPH00001/video/Serie/"));
    CHECK(!vmp_parent(path)); CHECK(!vmp_parent(path)); CHECK(!strcmp(path,VMP_ROOT));
    for (;;) {
        vmp_copy(saved,sizeof(saved),path);
        if (vmp_child(path,"0123456789abcdef",1)) break;
        CHECK(vmp_path_valid(path,1));
    }
    CHECK(!strcmp(path,saved)); CHECK(strlen(path)<VMP_PATH_CAP);
    CHECK(vmp_copy(dst,4,"1234")<0 && dst[0]==0);
    CHECK(!vmp_copy(dst,4,"123") && !strcmp(dst,"123"));
    CHECK(vmp_copy(NULL,4,"123")<0);
}

static void subtitles(void)
{
    VmpSubtitles s = {0};
    const char *valid = "\xef\xbb\xbf" "1\r\n00:00:01,000 --> 00:00:02,000\r\nHola\r\nmundo\r\n\r\n2\r\n00:00:03,000 --> 00:00:04,000\r\n<i>Segundo</i>";
    const char *invalid[] = {"", "1\n", "00:00:01,000 --> 00:00:02,000\n", "garbage\n",
        "1\n00:99:01,000 --> 00:99:02,000\nHola\n",
        "1\n00:00:02,000 --> 00:00:01,000\nHola\n",
        "1\n-1:00:01,000 --> 00:00:02,000\nHola\n",
        "1\n00:00:01,000 --> 00:00:02,000 trailing\nHola\n",
        "1\n00:00:01,000 --> \nHola\n",
        "1\n00:00:01,000 --> 00:00:02,000\nA\n\n2\n00:00:01,500 --> 00:00:03,000\nB\n"};
    CHECK(!parse(valid,strlen(valid),&s)); CHECK(s.count==2);
    CHECK(vmp_srt_at(&s,999)==NULL); CHECK(!strcmp(vmp_srt_at(&s,1000),"Hola\nmundo"));
    CHECK(vmp_srt_at(&s,2000)==NULL); CHECK(!strcmp(vmp_srt_at(&s,3999),"Segundo"));
    CHECK(vmp_srt_at(&s,4000)==NULL); CHECK(vmp_srt_at(&s,UINT64_MAX)==NULL);
    CHECK(!strcmp(vmp_srt_at(&s,1000),"Hola\nmundo")); /* backwards seek */
    for (size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        CHECK(parse(invalid[i],strlen(invalid[i]),&s)<0);
        CHECK(s.cues==NULL && s.count==0);
    }
    {
        const char nul[]="00:00:01,000 --> 00:00:02,000\nHe\0llo\n";
        CHECK(parse(nul,sizeof(nul)-1,&s)<0);
    }
    {
        char data[3000]; size_t prefix;
        const char *head="00:00:01,000 --> 00:00:02,000\n";
        prefix=strlen(head); memcpy(data,head,prefix);
        memset(data+prefix,'a',1023);
        CHECK(!parse(data,prefix+1023,&s)); CHECK(strlen(s.cues[0].text)==1023);
        data[prefix+1023]='a'; CHECK(parse(data,prefix+1024,&s)<0);
        memset(data+prefix,'a',800); data[prefix+800]='\n';
        memset(data+prefix+801,'b',800);
        CHECK(parse(data,prefix+1601,&s)<0); /* cumulative overflow */
    }
    {
        FILE *f=tmpfile(); assert(f);
        for (size_t i=0;i<=VMP_SRT_MAX_BYTES;++i) assert(fputc('\n',f)!=EOF);
        rewind(f); CHECK(vmp_srt_read(f,&s)<0); fclose(f);
    }
    {
        FILE *f=tmpfile(); assert(f);
        for (unsigned i=0;i<=VMP_SRT_MAX_CUES;++i) {
            unsigned a=i*2,b=a+1;
            if (i==VMP_SRT_MAX_CUES) {
                long mark=ftell(f); assert(mark>=0);
                rewind(f); CHECK(!vmp_srt_read(f,&s)); CHECK(s.count==VMP_SRT_MAX_CUES);
                assert(fseek(f,mark,SEEK_SET)==0);
            }
            fprintf(f,"%02u:%02u:%02u,000 --> %02u:%02u:%02u,000\nx\n\n",a/3600,(a/60)%60,a%60,b/3600,(b/60)%60,b%60);
        }
        rewind(f); CHECK(vmp_srt_read(f,&s)<0); CHECK(!s.count && !s.cues); fclose(f);
    }
    vmp_srt_free(&s); vmp_srt_free(&s);
}

static uint32_t rng=0x624b7101u;
static uint32_t random32(void) { rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5; return rng; }

static void fuzz(void)
{
    VmpSubtitles s={0};
    char data[2048];
    for (unsigned trial=0;trial<5000;++trial) {
        size_t n=random32()%sizeof(data);
        for (size_t i=0;i<n;++i) data[i]=(char)random32();
        if (trial%2==0 && n>=30) memcpy(data,"00:00:01,000 --> 00:00:02,000\n",30);
        if (!parse(data,n,&s)) {
            CHECK(s.count>0 && s.count<=VMP_SRT_MAX_CUES);
            for (size_t i=0;i<s.count;++i) {
                CHECK(s.cues[i].start_ms<s.cues[i].end_ms);
                CHECK(memchr(s.cues[i].text,0,sizeof(s.cues[i].text))!=NULL);
            }
        } else CHECK(s.cues==NULL && s.count==0);
        vmp_srt_free(&s);
    }
}

static void shapes(void)
{
    uint32_t out; unsigned samples;
    CHECK(!vmp_round_allocation(1,0x40000,&out) && out==0x40000);
    CHECK(!vmp_round_allocation(VMP_MAX_FRAME_BYTES,0x40000,&out));
    CHECK(vmp_round_allocation(UINT32_MAX,0x40000,&out)<0);
    CHECK(vmp_round_allocation(4096,3,&out)<0);
    CHECK(vmp_round_allocation(4096,0,&out)<0);
    CHECK(vmp_round_allocation(0,16,&out)<0);
    CHECK(vmp_round_allocation(16u*1024*1024,0x40000,&out)<0);
    CHECK(!vmp_round_decoder_allocation(16u*1024*1024,0x40000,&out) && out==16u*1024*1024);
    CHECK(!vmp_round_decoder_allocation(9u*1024*1024+1,0x40000,&out) && out==9u*1024*1024+0x40000);
    CHECK(!vmp_round_decoder_allocation(VMP_MAX_DECODER_BYTES,0x40000,&out));
    CHECK(vmp_round_decoder_allocation(VMP_MAX_DECODER_BYTES+1,0x40000,&out)<0);
    CHECK(vmp_round_decoder_allocation(UINT32_MAX,0x40000,&out)<0);
    CHECK(vmp_round_decoder_allocation(4096,3,&out)<0);
    CHECK(vmp_round_decoder_allocation(4096,0,&out)<0);
    CHECK(vmp_round_decoder_allocation(0,16,&out)<0);
    CHECK(vmp_round_decoder_allocation(4096,16,NULL)<0);
    CHECK(!vmp_read_window(100,0,20,&out) && out==20);
    CHECK(!vmp_read_window(100,90,20,&out) && out==10);
    CHECK(!vmp_read_window(100,100,20,&out) && out==0);
    CHECK(!vmp_read_window(100,99,0,&out) && out==0);
    CHECK(vmp_read_window(100,101,20,&out)<0);
    CHECK(vmp_read_window(100,0,UINT32_MAX,&out)<0);
    CHECK(vmp_read_window(100,0,20,NULL)<0);
    CHECK(!vmp_read_window(UINT64_MAX,UINT64_MAX-5,20,&out) && out==5);
    CHECK(vmp_player_handle_valid((int32_t)UINT32_C(0x81269c90),1));
    CHECK(vmp_player_handle_valid((int32_t)UINT32_C(0x813ea030),1));
    CHECK(vmp_player_handle_valid((int32_t)UINT32_C(0x8156a250),1));
    CHECK(vmp_player_handle_valid((int32_t)UINT32_C(0x816ea390),1));
    CHECK(!vmp_player_handle_valid((int32_t)UINT32_C(0x806a0003),0));
    CHECK(!vmp_player_handle_valid((int32_t)UINT32_C(0x806a0001),0));
    CHECK(!vmp_player_handle_valid(0,1));
    CHECK(!vmp_player_handle_valid(-1,0));
    CHECK(vmp_player_handle_valid(1,0));
    CHECK(!vmp_audio_shape(2,48000,4096,&samples) && samples==1024);
    CHECK(!vmp_audio_shape(1,44100,2048,&samples) && samples==1024);
    CHECK(!vmp_audio_shape(2,48000,68,&samples) && samples==17);
    CHECK(!vmp_audio_shape(1,48000,8190,&samples) && samples==4095);
    CHECK(vmp_audio_shape(6,48000,4096,&samples)<0);
    CHECK(vmp_audio_shape(2,99999,4096,&samples)<0);
    CHECK(vmp_audio_shape(2,48000,4097,&samples)<0);
    CHECK(vmp_audio_shape(2,48000,0,&samples)<0);
    CHECK(vmp_audio_shape(2,48000,UINT_MAX,&samples)<0);
    CHECK(vmp_audio_shape(2,8000,(VMP_MAX_AUDIO_FRAMES+64)*4,&samples)<0);
    CHECK(vmp_utf8_valid("Cap\xc3\xadtulo",9));
    CHECK(!vmp_utf8_valid("\xc0\x80",2));
    CHECK(!vmp_utf8_valid("\xed\xa0\x80",3));
    CHECK(!vmp_utf8_valid("\xf4\x90\x80\x80",4));
    CHECK(!vmp_utf8_valid("\xe2\x82",2));
    CHECK(vmp_utf8_valid("\xf0\x9f\x8e\xac",4));
}

typedef struct { unsigned pos, total; const char *name; int error, directory, unterminated; } FakeDir;
static int fake_next(void *context, VmpEntry *entry)
{
    FakeDir *dir = context;
    if (dir->pos == dir->total) return dir->error ? -1 : 0;
    ++dir->pos;
    if (dir->unterminated) memset(entry->name,'x',sizeof(entry->name));
    else vmp_copy(entry->name,sizeof(entry->name),dir->name);
    entry->directory = dir->directory;
    entry->regular = 1;
    return 1;
}

static void directories(void)
{
    struct { unsigned before; VmpEntry entries[2]; unsigned after; } guarded;
    VmpEntry *large = calloc(VMP_MAX_ENTRIES,sizeof(*large));
    FakeDir dir={0,2,"01.mp4",0,0,0};
    size_t count=0;
    assert(large);
    guarded.before=0x12345678; guarded.after=0x98765432;
    CHECK(!vmp_collect(VMP_ROOT,guarded.entries,2,&count,fake_next,&dir) && count==2);
    dir.pos=0; dir.total=3;
    CHECK(vmp_collect(VMP_ROOT,guarded.entries,2,&count,fake_next,&dir)<0 && count==0);
    CHECK(guarded.before==0x12345678 && guarded.after==0x98765432);
    dir.pos=0; dir.total=VMP_MAX_ENTRIES;
    CHECK(!vmp_collect(VMP_ROOT,large,VMP_MAX_ENTRIES,&count,fake_next,&dir) && count==VMP_MAX_ENTRIES);
    dir.pos=0; dir.total=VMP_MAX_ENTRIES+1;
    CHECK(vmp_collect(VMP_ROOT,large,VMP_MAX_ENTRIES,&count,fake_next,&dir)<0 && count==0);
    dir.pos=0; dir.total=1; dir.name="movie.verylongextension";
    CHECK(!vmp_collect(VMP_ROOT,guarded.entries,2,&count,fake_next,&dir) && !count);
    dir.pos=0; dir.total=VMP_MAX_ENTRIES+1;
    CHECK(vmp_collect(VMP_ROOT,guarded.entries,2,&count,fake_next,&dir)<0);
    dir.pos=0; dir.total=1; dir.name=".."; dir.directory=1;
    CHECK(!vmp_collect(VMP_ROOT,guarded.entries,2,&count,fake_next,&dir) && !count);
    dir.pos=0; dir.name="../escape";
    CHECK(vmp_collect(VMP_ROOT,guarded.entries,2,&count,fake_next,&dir)<0 && !count);
    dir.pos=0; dir.name="folder"; dir.error=1;
    CHECK(vmp_collect(VMP_ROOT,guarded.entries,2,&count,fake_next,&dir)<0 && !count);
    dir.pos=0; dir.error=0; dir.unterminated=1;
    CHECK(vmp_collect(VMP_ROOT,guarded.entries,2,&count,fake_next,&dir)<0 && !count);
    free(large);
}

static int probe(const void *bytes, size_t len, int kind, unsigned *w, unsigned *h)
{
    FILE *f = tmpfile();
    int result;
    assert(f && fwrite(bytes, 1, len, f) == len);
    result = vmp_image_probe(f, kind, len, w, h);
    assert(fclose(f) == 0);
    return result;
}

static void images(void)
{
    static const unsigned char png[33] = {0x89,'P','N','G',13,10,26,10,0,0,0,13,'I','H','D','R',
        0,0,2,0x80, 0,0,1,0xE0, 8,2,0,0,0, 0,0,0,0};
    static const unsigned char jpg[] = {0xFF,0xD8, 0xFF,0xE0,0,4,'J','F', 0xFF,0xC4,0,3,0,
        0xFF,0xC0,0,17,8, 0x02,0x58, 0x03,0x20, 3,1,0x22,0,2,0x11,1,3,0x11,1};
    static const unsigned char bmp[54] = {'B','M',0,0,0,0,0,0,0,0,54,0,0,0, 40,0,0,0,
        0x40,1,0,0, 0xF0,0,0,0, 1,0, 24,0, 0,0,0,0};
    unsigned char bad[sizeof(png)];
    uint16_t utf16[8]; char out[16];
    unsigned w = 0, h = 0;
    CHECK(vmp_image_kind("a.PNG")==VMP_IMAGE_PNG && vmp_image_kind("a.jpg")==VMP_IMAGE_JPEG);
    CHECK(vmp_image_kind("a.JPEG")==VMP_IMAGE_JPEG && vmp_image_kind("a.bmp")==VMP_IMAGE_BMP);
    CHECK(!vmp_image_kind("a.thumb.png") && !vmp_image_kind("cover.png") && !vmp_image_kind(".png"));
    CHECK(!vmp_image_kind("a.png.exe") && !vmp_image_kind(NULL) && !vmp_image_kind("a.gif"));
    CHECK(!probe(png,sizeof(png),VMP_IMAGE_PNG,&w,&h) && w==640 && h==480);
    CHECK(!probe(jpg,sizeof(jpg),VMP_IMAGE_JPEG,&w,&h) && w==800 && h==600);
    CHECK(!probe(bmp,sizeof(bmp),VMP_IMAGE_BMP,&w,&h) && w==320 && h==240);
    CHECK(probe(png,sizeof(png),VMP_IMAGE_JPEG,&w,&h)<0 && !w && !h);
    memcpy(bad,png,sizeof(bad)); bad[16]=0x10;             /* 268M wide */
    CHECK(probe(bad,sizeof(bad),VMP_IMAGE_PNG,&w,&h)<0);
    memcpy(bad,png,sizeof(bad)); bad[18]=0x0F; bad[19]=0xFF; bad[22]=0x0F; bad[23]=0xFF; /* 4095 x 4095 */
    CHECK(probe(bad,sizeof(bad),VMP_IMAGE_PNG,&w,&h)<0);   /* too many pixels */
    memcpy(bad,png,sizeof(bad)); bad[16]=bad[17]=bad[18]=bad[19]=0;
    CHECK(probe(bad,sizeof(bad),VMP_IMAGE_PNG,&w,&h)<0);
    CHECK(probe(png,10,VMP_IMAGE_PNG,&w,&h)<0 && probe(jpg,5,VMP_IMAGE_JPEG,&w,&h)<0);
    CHECK(probe(jpg,sizeof(jpg)-14,VMP_IMAGE_JPEG,&w,&h)<0);
    {
        FILE *f=tmpfile(); assert(f && fwrite(png,1,sizeof(png),f)==sizeof(png));
        CHECK(vmp_image_probe(f,VMP_IMAGE_PNG,VMP_IMAGE_MAX_BYTES+1,&w,&h)<0);
        CHECK(vmp_image_probe(f,VMP_IMAGE_PNG,0,&w,&h)<0); fclose(f);
    }
    CHECK(vmp_folder_name_valid("Fotos 2024") && vmp_folder_name_valid("Pel\xc3\xad" "culas \xc3\xb1"));
    CHECK(!vmp_folder_name_valid("") && !vmp_folder_name_valid(" a") && !vmp_folder_name_valid("a "));
    CHECK(!vmp_folder_name_valid("a.") && !vmp_folder_name_valid("a/b") && !vmp_folder_name_valid("a:b"));
    CHECK(!vmp_folder_name_valid("a?b") && !vmp_folder_name_valid("..") && !vmp_folder_name_valid(NULL));
    CHECK(!vmp_folder_name_valid("a\\b") && !vmp_folder_name_valid("a\"b") && !vmp_folder_name_valid("a\tb"));
    utf16[0]=' '; utf16[1]='A'; utf16[2]=0xE9; utf16[3]=0xD83D; utf16[4]=0xDE00; utf16[5]=' '; utf16[6]=0;
    CHECK(!vmp_utf16_to_utf8(utf16,out,sizeof(out)) && !strcmp(out,"A\xC3\xA9\xF0\x9F\x98\x80"));
    CHECK(vmp_utf16_to_utf8(utf16,out,6)<0 && !out[0]);
    utf16[0]=0xD83D; utf16[1]='x'; utf16[2]=0;
    CHECK(vmp_utf16_to_utf8(utf16,out,sizeof(out))<0);
    utf16[0]=0xDC00; utf16[1]=0;
    CHECK(vmp_utf16_to_utf8(utf16,out,sizeof(out))<0);
    utf16[0]=' '; utf16[1]=0;
    CHECK(!vmp_utf16_to_utf8(utf16,out,sizeof(out)) && !out[0]);
}

int main(void)
{
    paths(); subtitles(); shapes(); directories(); images(); fuzz();
    printf("PASS: %u assertions, including 5000 deterministic parser mutations.\n",checks);
    return 0;
}
