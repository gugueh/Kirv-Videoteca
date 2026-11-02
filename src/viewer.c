/* Image viewer: PNG, JPEG and BMP from the library. Reads only; never writes. */
#include "app.h"
#include "ui.h"
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { vita2d_texture *texture; char error[96]; } Shown;

static void release(Shown *shown) {
    if(shown->texture) { vita2d_wait_rendering_done(); vita2d_free_texture(shown->texture); }
    shown->texture=NULL;
}

static void load(Shown *shown,const char *path,int kind) {
    SceIoStat stat; FILE *file; unsigned w,h; int probed;
    release(shown); shown->error[0]=0;
    memset(&stat,0,sizeof(stat));
    if(!vmp_path_valid(path,0) || sceIoGetstat(path,&stat)<0 || !SCE_S_ISREG(stat.st_mode)) {
        vmp_copy(shown->error,sizeof(shown->error),"No se pudo abrir la imagen."); return;
    }
    file=fopen(path,"rb");
    if(!file) { vmp_copy(shown->error,sizeof(shown->error),"No se pudo abrir la imagen."); return; }
    probed=stat.st_size>0 ? vmp_image_probe(file,kind,(uint64_t)stat.st_size,&w,&h) : -1;
    fclose(file);
    if(probed) {
        vmp_copy(shown->error,sizeof(shown->error),"Imagen no compatible o demasiado grande (máx. 8 MP, 12 MB).");
        return;
    }
    shown->texture=kind==VMP_IMAGE_PNG?vita2d_load_PNG_file(path):kind==VMP_IMAGE_JPEG?vita2d_load_JPEG_file(path):vita2d_load_BMP_file(path);
    if(!shown->texture) vmp_copy(shown->error,sizeof(shown->error),"El decodificador no pudo leer la imagen.");
}

static size_t step(const VmpEntry *entries,const size_t *map,size_t count,size_t from,int direction) {
    for(size_t tried=0;tried<count;tried++) {
        from=direction>0?(from+1)%count:(from?from-1:count-1);
        if(!entries[map[from]].directory && vmp_image_kind(entries[map[from]].name)) return from;
    }
    return from;
}

static void ordinal(const VmpEntry *entries,const size_t *map,size_t count,size_t at,unsigned *position,unsigned *total) {
    *position=0; *total=0;
    for(size_t i=0;i<count;i++) {
        if(entries[map[i]].directory || !vmp_image_kind(entries[map[i]].name)) continue;
        ++*total; if(i==at) *position=*total;
    }
}

size_t app_viewer(const char *directory,const VmpEntry *entries,const size_t *map,size_t count,size_t start) {
    Shown shown; size_t index=start; int reload=1,overlay=1,fill=0;
    memset(&shown,0,sizeof(shown));
    for(;;) {
        char path[VMP_PATH_CAP],name[256],line[96]; unsigned position,total; unsigned b=app_buttons();
        int move=0;
        if(index>=count || entries[map[index]].directory || !vmp_image_kind(entries[map[index]].name)) break;
        if(app_touch.swipe) move=app_touch.swipe>0?1:-1;
        else if(app_touch.tap) {
            if(app_touch.y<55 && app_touch.x<90) b|=SCE_CTRL_CIRCLE;
            else if(app_touch.x<300) move=-1;
            else if(app_touch.x>=660) move=1;
            else overlay=!overlay;
        }
        if(b&SCE_CTRL_CIRCLE) break;
        if(b&(SCE_CTRL_LEFT|SCE_CTRL_LTRIGGER)) move=-1;
        if(b&(SCE_CTRL_RIGHT|SCE_CTRL_RTRIGGER)) move=1;
        if(b&SCE_CTRL_SQUARE) fill=!fill;
        if(b&SCE_CTRL_TRIANGLE) overlay=!overlay;
        if(move) { size_t next=step(entries,map,count,index,move); if(next!=index) { index=next; reload=1; } }
        if(reload) {
            vmp_copy(name,sizeof(name),entries[map[index]].name);
            /* Decoding can take a second or two: show progress first. */
            app_frame_begin(); ui_background(); ui_text(360,260,1.1f,UI_WHITE,"Cargando imagen...",400); app_frame_end();
            if(vmp_copy(path,sizeof(path),directory) || vmp_child(path,name,0)) {
                release(&shown); vmp_copy(shown.error,sizeof(shown.error),"Ruta no válida.");
            } else load(&shown,path,vmp_image_kind(name));
            reload=0;
        }
        vmp_copy(name,sizeof(name),entries[map[index]].name); ordinal(entries,map,count,index,&position,&total);
        app_frame_begin();
        vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,255));
        if(shown.texture) {
            float tw=(float)vita2d_texture_get_width(shown.texture),th=(float)vita2d_texture_get_height(shown.texture);
            float sx=960.0f/tw,sy=544.0f/th,scale=fill?(sx>sy?sx:sy):(sx<sy?sx:sy);
            vita2d_texture_set_filters(shown.texture,SCE_GXM_TEXTURE_FILTER_LINEAR,SCE_GXM_TEXTURE_FILTER_LINEAR);
            vita2d_draw_texture_scale(shown.texture,(960-tw*scale)/2,(544-th*scale)/2,scale,scale);
        } else ui_text(60,270,.9f,UI_WHITE,shown.error[0]?shown.error:"Sin imagen.",840);
        if(overlay || !shown.texture) {
            vita2d_draw_rectangle(0,0,960,40,RGBA8(9,24,42,215));
            ui_icon(UI_BACK,12,3,34); ui_text(70,28,.85f,UI_WHITE,name,690);
            snprintf(line,sizeof(line),"%u / %u",position,total); ui_text(790,28,.8f,UI_MUTED,line,150);
            ui_footer("O volver   izquierda/derecha o L/R imagen   cuadrado ajuste   triángulo ocultar");
        }
        app_frame_end(); sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
    }
    release(&shown);
    return index<count?index:start;
}
