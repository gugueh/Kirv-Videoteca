/* Image viewer: PNG, JPEG and BMP. Reads only; never writes. */
#include "app.h"
#include "ui.h"
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <string.h>

static const float zooms[]={1.0f,1.5f,2.0f,3.0f,4.0f};
#define ZOOMS (sizeof(zooms)/sizeof(*zooms))

static void release(vita2d_texture **texture) {
    if(*texture) { vita2d_wait_rendering_done(); vita2d_free_texture(*texture); }
    *texture=NULL;
}

static float clamp(float value,float limit) { return value>limit?limit:value<-limit?-limit:value; }

size_t app_viewer(char (*paths)[VMP_PATH_CAP],size_t count,size_t start) {
    vita2d_texture *texture=NULL; const char *error=""; size_t index=start; unsigned level=0;
    int reload=1,overlay=1; float pan_x=0,pan_y=0;
    if(!paths || start>=count) return start;
    for(;;) {
        const char *name=strrchr(paths[index],'/'); char line[96];
        unsigned b=app_buttons(),held=app_held(); int move=0; float zoom=zooms[level];
        name=name?name+1:paths[index];
        if(b&SCE_CTRL_CIRCLE) break;
        if(app_touch.tap && app_touch.y<55 && app_touch.x<90) break;
        if(b&(SCE_CTRL_LTRIGGER)) move=-1;
        if(b&(SCE_CTRL_RTRIGGER)) move=1;
        if(level) {
            /* Zoomed: the d-pad and a finger drag move the picture. */
            if(app_touch.down) { pan_x+=(float)app_touch.dx; pan_y+=(float)app_touch.dy; }
            if(held&SCE_CTRL_LEFT) pan_x+=14;
            if(held&SCE_CTRL_RIGHT) pan_x-=14;
            if(held&SCE_CTRL_UP) pan_y+=14;
            if(held&SCE_CTRL_DOWN) pan_y-=14;
        } else {
            if(b&SCE_CTRL_LEFT) move=-1;
            if(b&SCE_CTRL_RIGHT) move=1;
            if(app_touch.swipe) move=app_touch.swipe>0?1:-1;
            else if(app_touch.tap && app_touch.y>=55) {
                if(app_touch.x<300) move=-1;
                else if(app_touch.x>=660) move=1;
                else overlay=!overlay;
            }
        }
        if(b&SCE_CTRL_CROSS) { level=(level+1)%ZOOMS; if(!level) pan_x=pan_y=0; }
        if((b&SCE_CTRL_SQUARE) && level) { level--; if(!level) pan_x=pan_y=0; }
        if(b&SCE_CTRL_TRIANGLE) overlay=!overlay;
        if(move && count>1) { index=move>0?(index+1)%count:(index?index-1:count-1); reload=1; }
        if(reload) {
            const char *next=strrchr(paths[index],'/');
            /* Decoding can take a second or two: show progress first. */
            app_frame_begin(); ui_background(); ui_text(330,260,1.1f,UI_WHITE,L("Cargando imagen...","Loading image..."),400);
            ui_text(330,295,.75f,UI_MUTED,next?next+1:paths[index],400); app_frame_end();
            release(&texture);
            texture=ui_load_image(paths[index],vmp_image_kind(paths[index]),2048,&error);
            level=0; pan_x=pan_y=0; reload=0; continue;
        }
        app_frame_begin();
        vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,255));
        if(texture) {
            float tw=(float)vita2d_texture_get_width(texture),th=(float)vita2d_texture_get_height(texture);
            float sx=960.0f/tw,sy=544.0f/th,scale=(sx<sy?sx:sy)*zoom,dw=tw*scale,dh=th*scale;
            pan_x=clamp(pan_x,dw>960?(dw-960)/2:0); pan_y=clamp(pan_y,dh>544?(dh-544)/2:0);
            vita2d_texture_set_filters(texture,SCE_GXM_TEXTURE_FILTER_LINEAR,SCE_GXM_TEXTURE_FILTER_LINEAR);
            vita2d_draw_texture_scale(texture,(960-dw)/2+pan_x,(544-dh)/2+pan_y,scale,scale);
        } else ui_text(60,270,.9f,UI_WHITE,error,840);
        if(overlay || !texture) {
            vita2d_draw_rectangle(0,0,960,40,RGBA8(9,24,42,215));
            ui_icon(UI_BACK,12,3,34); ui_text(70,28,.85f,UI_WHITE,name,640);
            if(level) snprintf(line,sizeof(line),"%u / %u   %u%%",(unsigned)index+1,(unsigned)count,(unsigned)(zoom*100));
            else snprintf(line,sizeof(line),"%u / %u",(unsigned)index+1,(unsigned)count);
            ui_text(740,28,.8f,UI_MUTED,line,210);
            ui_footer(level?L("O volver   X acercar   cuadrado alejar   cruceta o dedo mover   L/R imagen","O back   X zoom in   square zoom out   d-pad or finger pan   L/R image")
                           :L("O volver   X acercar   izquierda/derecha o L/R imagen   triángulo ocultar","O back   X zoom in   left/right or L/R image   triangle hide"));
        }
        app_frame_end(); sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
    }
    release(&texture);
    return index;
}
