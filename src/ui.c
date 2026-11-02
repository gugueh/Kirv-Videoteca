#include "ui.h"
#include "app.h"
#include <setjmp.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <png.h>
#include <jpeglib.h>
#include <psp2/io/fcntl.h>
static vita2d_pgf *font;
static vita2d_texture *background,*logo,*icons[UI_ICONS];
static const char *names[]={"library","series","movies","folder","play","pause","next","previous",
 "volume","subtitles","settings","search","sort","favorite","check","back","image"};
typedef struct { char path[VMP_PATH_CAP]; vita2d_texture *image; } Cover;
static Cover covers[8];
static unsigned cover_cursor;
static void rounded(int x,int y,int w,int h,int r,unsigned color) {
    vita2d_draw_rectangle((float)(x+r),(float)y,(float)(w-2*r),(float)h,color);
    vita2d_draw_rectangle((float)x,(float)(y+r),(float)w,(float)(h-2*r),color);
    vita2d_draw_fill_circle((float)(x+r),(float)(y+r),(float)r,color);
    vita2d_draw_fill_circle((float)(x+w-r),(float)(y+r),(float)r,color);
    vita2d_draw_fill_circle((float)(x+r),(float)(y+h-r),(float)r,color);
    vita2d_draw_fill_circle((float)(x+w-r),(float)(y+h-r),(float)r,color);
}
int ui_init(void) {
    char path[96];
    font=vita2d_load_default_pgf();
    background=vita2d_load_PNG_file("app0:assets/ui/background.png");
    logo=vita2d_load_PNG_file("app0:assets/ui/logo.png");
    for(int i=0;i<UI_ICONS;i++) {
        snprintf(path,sizeof(path),"app0:assets/ui/%s.png",names[i]);
        icons[i]=vita2d_load_PNG_file(path);
    }
    if(!font || !background || !logo) return -1;
    for(int i=0;i<UI_ICONS;i++) if(!icons[i]) return -1;
    return 0;
}
void ui_clear_covers(void) {
    vita2d_wait_rendering_done();
    for(unsigned i=0;i<8;i++) { if(covers[i].image) vita2d_free_texture(covers[i].image); memset(&covers[i],0,sizeof(Cover)); }
    cover_cursor=0;
}
void ui_fini(void) {
    ui_clear_covers();
    for(int i=0;i<UI_ICONS;i++) if(icons[i]) vita2d_free_texture(icons[i]);
    if(logo) vita2d_free_texture(logo);
    if(background) vita2d_free_texture(background);
    if(font) vita2d_free_pgf(font);
}
void ui_text(int x,int y,float scale,unsigned color,const char *s,int width) {
    char clipped[VMP_PATH_CAP+4]; size_t n;
    if(!s || !font) return;
    n=strlen(s); if(n>=VMP_PATH_CAP) n=VMP_PATH_CAP-1;
    memcpy(clipped,s,n); clipped[n]=0;
    while(n && !vmp_utf8_valid(clipped,n)) clipped[--n]=0;
    if(n && width>0 && vita2d_pgf_text_width(font,scale,clipped)>width) {
        do {
            do { n--; } while(n && ((unsigned char)clipped[n]&0xc0)==0x80);
            memcpy(clipped+n,"...",4);
        } while(n && vita2d_pgf_text_width(font,scale,clipped)>width);
    }
    vita2d_pgf_draw_text(font,x,y,color,scale,clipped);
}
void ui_background(void) {
    if(background) vita2d_draw_texture(background,0,0);
    else vita2d_draw_rectangle(0,0,960,544,RGBA8(15,28,46,255));
}
void ui_panel(int x,int y,int w,int h,int selected) {
    if(selected) rounded(x-2,y-2,w+4,h+4,14,RGBA8(50,150,216,100));
    rounded(x,y,w,h,12,selected?UI_CYAN:RGBA8(119,160,196,155));
    rounded(x+1,y+1,w-2,h-2,11,RGBA8(19,44,72,245));
    vita2d_draw_line((float)(x+12),(float)(y+2),(float)(x+w-12),(float)(y+2),RGBA8(220,238,255,100));
}
void ui_icon(int id,int x,int y,int size) {
    if(id>=0 && id<UI_ICONS && icons[id]) vita2d_draw_texture_scale(icons[id],(float)x,(float)y,(float)size/64,(float)size/64);
}
void ui_header(const char *title) {
    char clock_text[16]=""; time_t now=time(NULL); struct tm *tm=localtime(&now);
    if(tm) strftime(clock_text,sizeof(clock_text),"%H:%M",tm);
    vita2d_draw_rectangle(0,0,960,43,RGBA8(10,25,43,200));
    ui_icon(UI_LIBRARY,16,4,34); ui_text(60,30,1.1f,UI_WHITE,title,490);
    ui_text(690,29,.8f,UI_MUTED,"Kirv Studio",140); ui_text(855,29,.85f,UI_WHITE,clock_text,90);
    vita2d_draw_line(0,43,960,43,RGBA8(109,160,201,180));
}
void ui_footer(const char *hint) {
    vita2d_draw_rectangle(0,500,960,44,RGBA8(9,24,42,235));
    vita2d_draw_line(0,500,960,500,RGBA8(109,160,201,180));
    ui_text(24,528,.75f,UI_WHITE,hint,920);
}
void ui_welcome(void) {
    ui_background();
    if(logo) vita2d_draw_texture(logo,240,80);
    ui_text(367,359,1.55f,UI_WHITE,"Videoteca",300);
    ui_panel(350,407,260,55,1); ui_icon(UI_PLAY,370,416,38);
    ui_text(426,443,1.0f,UI_WHITE,L("Iniciar","Start"),160);
    ui_text(30,520,.65f,UI_MUTED,"Kirv Studio  /  DEV 0.22",500);
}
static unsigned be32(const unsigned char *b) { return ((unsigned)b[0]<<24)|((unsigned)b[1]<<16)|((unsigned)b[2]<<8)|b[3]; }
typedef struct { struct jpeg_error_mgr base; jmp_buf jump; } JpegError;
static void jpeg_fail(j_common_ptr info) { longjmp(((JpegError *)info->err)->jump,1); }
/* Streams the JPEG from disk and lets libjpeg scale it by 1/2, 1/4 or 1/8, so
   a 24 MP photo needs only a texture the size of the result. */
static vita2d_texture *load_jpeg(FILE *file,unsigned max_side) {
    struct jpeg_decompress_struct info; JpegError error;
    vita2d_texture *volatile texture=NULL; volatile unsigned denom=1; unsigned largest,stride; unsigned char *data;
    memset(&info,0,sizeof(info));
    info.err=jpeg_std_error(&error.base); error.base.error_exit=jpeg_fail;
    if(setjmp(error.jump)) {
        jpeg_destroy_decompress(&info);
        if(texture) { vita2d_wait_rendering_done(); vita2d_free_texture(texture); }
        return NULL;
    }
    jpeg_create_decompress(&info); jpeg_stdio_src(&info,file);
    if(jpeg_read_header(&info,TRUE)!=JPEG_HEADER_OK) longjmp(error.jump,1);
    largest=info.image_width>info.image_height?info.image_width:info.image_height;
    while(denom<8 && largest/denom>max_side) denom*=2;
    info.scale_num=1; info.scale_denom=denom; info.out_color_space=JCS_EXT_RGBA;
    jpeg_start_decompress(&info);
    if(!info.output_width || !info.output_height || info.output_width>4096 || info.output_height>4096 || info.output_components!=4) longjmp(error.jump,1);
    texture=vita2d_create_empty_texture(info.output_width,info.output_height);
    if(!texture) longjmp(error.jump,1);
    stride=vita2d_texture_get_stride(texture); data=vita2d_texture_get_datap(texture);
    while(info.output_scanline<info.output_height) {
        JSAMPROW row=data+(size_t)info.output_scanline*stride;
        if(jpeg_read_scanlines(&info,&row,1)!=1) longjmp(error.jump,1);
    }
    jpeg_finish_decompress(&info); jpeg_destroy_decompress(&info);
    return texture;
}
vita2d_texture *ui_load_image(const char *path,int kind,unsigned max_side,const char **error) {
    FILE *file; long size; unsigned w,h; vita2d_texture *texture;
    const char *none;
    if(!error) error=&none;
    *error=L("No se pudo abrir la imagen.","Could not open the image.");
    if(!vmp_path_valid(path,0) || !kind || !(file=fopen(path,"rb"))) return NULL;
    if(fseek(file,0,SEEK_END) || (size=ftell(file))<=0 || vmp_image_probe(file,kind,(uint64_t)size,&w,&h)) {
        fclose(file);
        *error=L("Imagen no compatible o demasiado grande.","Unsupported or too large image.");
        return NULL;
    }
    /* Thumbnails never pay for a full decode of a big PNG or BMP. */
    if(kind!=VMP_IMAGE_JPEG && max_side<=512 && (uint64_t)w*h>1200000u) { fclose(file); return NULL; }
    if(kind==VMP_IMAGE_JPEG) { rewind(file); texture=load_jpeg(file,max_side); fclose(file); }
    else { fclose(file); texture=kind==VMP_IMAGE_PNG?vita2d_load_PNG_file(path):vita2d_load_BMP_file(path); }
    if(!texture) *error=L("El decodificador no pudo leer la imagen.","The decoder could not read the image.");
    return texture;
}
static vita2d_texture *get_cover(const char *directory) {
    char path[VMP_PATH_CAP]; unsigned char h[33]; FILE *f; long length; unsigned w,height; Cover *entry;
    if(!directory || vmp_copy(path,sizeof(path),directory)) return NULL;
    if(vmp_image_kind(directory)) {
        for(unsigned i=0;i<8;i++) if(!strcmp(covers[i].path,directory)) return covers[i].image;
        if(cover_cursor>=8 || vmp_copy(covers[cover_cursor].path,sizeof(covers[cover_cursor].path),directory)) return NULL;
        entry=&covers[cover_cursor++];
        entry->image=ui_load_image(directory,vmp_image_kind(directory),320,NULL);
        return entry->image;
    }
    if(vmp_is_mp4(directory)) {
        size_t n=strlen(path);
        if(!vmp_path_valid(path,0) || n+7>=sizeof(path)) return NULL;
        memcpy(path+n-4,".thumb.png",11);
    } else if(vmp_child(path,"cover.png",0)) return NULL;
    for(unsigned i=0;i<8;i++) if(!strcmp(covers[i].path,path)) return covers[i].image;
    /* Do not evict a texture referenced by the current GPU scene. The browser
       clears this eight-slot cache between pages, outside drawing. */
    if(cover_cursor>=8) return NULL;
    entry=&covers[cover_cursor++]; vmp_copy(entry->path,sizeof(entry->path),path);
    f=fopen(path,"rb"); if(!f) return NULL;
    if(fseek(f,0,SEEK_END) || (length=ftell(f))<33 || length>1024*1024 || fseek(f,0,SEEK_SET) || fread(h,1,33,f)!=33) { fclose(f); return NULL; }
    fclose(f);
    if(memcmp(h,"\211PNG\r\n\032\n",8) || be32(h+8)!=13 || memcmp(h+12,"IHDR",4)) return NULL;
    w=be32(h+16); height=be32(h+20);
    if(!w || !height || w>1024 || height>1024 || (uint64_t)w*height>1024*1024 || h[24]!=8) return NULL;
    entry->image=vita2d_load_PNG_file(path);
    return entry->image;
}
void ui_cover(const char *directory,int x,int y,int w,int h,const char *label) {
    vita2d_texture *cover=get_cover(directory);
    ui_panel(x,y,w,h,0);
    if(cover) {
        float tw=(float)vita2d_texture_get_width(cover), th=(float)vita2d_texture_get_height(cover);
        float sx=(float)(w-4)/tw, sy=(float)(h-4)/th, scale=sx<sy?sx:sy;
        vita2d_draw_texture_scale(cover,x+(w-tw*scale)/2,y+(h-th*scale)/2,scale,scale);
    } else {
        int size=h>100?64:36;
        ui_icon(directory && vmp_image_kind(directory)?UI_IMAGE:UI_MOVIES,x+(w-size)/2,y+(h-size)/2-9,size);
        if(h>100) ui_text(x+12,y+h-17,.65f,UI_MUTED,label?label:"Kirv Studio",w-24);
    }
}
void ui_save_thumbnail(const char *video,vita2d_texture *frame) {
    char path[VMP_PATH_CAP],temp[VMP_PATH_CAP]; size_t n;
    unsigned char *pixels,*source; unsigned stride,sw,sh; png_image image;
    if(!frame || !vmp_path_writable(video) || !vmp_is_mp4(video) || vmp_copy(path,sizeof(path),video)) return;
    n=strlen(path); if(n+11>=sizeof(path)) return;
    memcpy(path+n-4,".thumb.png",11);
    memcpy(temp,path,n+6); memcpy(temp+n+6,".tmp",5);
    /* Capture only after playback stops and all GPU work has completed. */
    sw=vita2d_texture_get_width(frame); sh=vita2d_texture_get_height(frame);
    stride=vita2d_texture_get_stride(frame); source=vita2d_texture_get_datap(frame);
    if(!source || !sw || !sh || stride<sw*4) return;
    pixels=malloc(320*180*4); if(!pixels) return;
    for(unsigned y=0;y<180;y++) for(unsigned x=0;x<320;x++)
        memcpy(pixels+(y*320+x)*4,source+(y*sh/180)*stride+(x*sw/320)*4,4);
    memset(&image,0,sizeof(image)); image.version=PNG_IMAGE_VERSION;
    image.width=320; image.height=180; image.format=PNG_FORMAT_RGBA;
    if(png_image_write_to_file(&image,temp,0,pixels,0,NULL)) {
        /* Existing thumbnails remain valid if the filesystem cannot replace. */
        if(sceIoRename(temp,path)<0) sceIoRemove(temp);
    } else sceIoRemove(temp);
    png_image_free(&image); free(pixels);
}
void ui_time(char *out,size_t cap,uint64_t ms) {
    unsigned seconds=(unsigned)(ms/1000);
    if(seconds>=3600) snprintf(out,cap,"%u:%02u:%02u",seconds/3600,(seconds/60)%60,seconds%60);
    else snprintf(out,cap,"%02u:%02u",seconds/60,seconds%60);
}
void ui_bar(int x,int y,int w,uint64_t pos,uint64_t duration) {
    float ratio=duration?(float)((double)pos/(double)duration):0;
    if(ratio>1) ratio=1;
    rounded(x,y,w,6,3,RGBA8(82,111,144,255));
    if(ratio>0) vita2d_draw_rectangle((float)x,(float)y,(float)w*ratio,6,UI_CYAN);
}
void ui_player(const char *name,uint64_t pos,uint64_t duration,int paused,unsigned volume,int subs,int has_subs,unsigned seek) {
    char a[24],b[24],line[160];
    vita2d_draw_rectangle(0,0,960,40,RGBA8(9,24,42,215));
    ui_icon(UI_BACK,12,3,34); ui_text(70,28,.85f,UI_WHITE,name,710);
    ui_text(822,28,.75f,UI_MUTED,paused?L("Pausado","Paused"):L("Reproduciendo","Playing"),128);
    ui_panel(16,392,928,98,0); ui_time(a,sizeof(a),pos);
    if(duration) ui_time(b,sizeof(b),duration); else vmp_copy(b,sizeof(b),"--:--");
    ui_text(32,420,.8f,UI_WHITE,a,90); ui_text(846,420,.8f,UI_WHITE,b,90);
    ui_bar(130,407,700,pos,duration);
    ui_icon(UI_VOLUME,34,438,34); snprintf(line,sizeof(line),"- %u%% +",volume); ui_text(75,463,.8f,UI_WHITE,line,115);
    ui_icon(paused?UI_PLAY:UI_PAUSE,456,433,45);
    ui_icon(UI_PREVIOUS,388,439,32); ui_icon(UI_NEXT,538,439,32);
    ui_icon(UI_SUBTITLES,690,439,32); ui_text(735,462,.72f,UI_MUTED,has_subs?(subs?L("Subtítulos: sí","Subtitles: on"):L("Subtítulos: no","Subtitles: off")):L("Sin subtítulos","No subtitles"),195);
    snprintf(line,sizeof(line),L("X pausa   O volver   L/R %u s   arriba/abajo volumen   cuadrado subtítulos   triángulo controles","X pause   O back   L/R %u s   up/down volume   square subtitles   triangle controls"),seek);
    ui_footer(line);
}
