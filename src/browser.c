#include "app.h"
#include "ui.h"
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STATE "ux0:data/VMPH00001/ui-state.bin"
#define TEMP "ux0:data/VMPH00001/ui-state.tmp"
#define BACKUP "ux0:data/VMPH00001/ui-state.bak"
VmpLibrary library;
#define TABS 5
#define TAB_IMAGES 3
#define TAB_FOLDERS 4
static const char *tabs[TABS]={"Todo","Series","Películas","Imágenes","Carpetas"};
static const int tab_icons[TABS]={UI_LIBRARY,UI_SERIES,UI_MOVIES,UI_IMAGE,UI_FOLDER};
static const char *leaf(const char *path) { const char *p=strrchr(path,'/'); return p?p+1:path; }
static void title(char *out,size_t cap,const char *name) {
    vmp_copy(out,cap,name);
    if(vmp_is_mp4(out)) out[strlen(out)-4]=0;
    else if(vmp_image_kind(out)) { char *dot=strrchr(out,'.'); if(dot) *dot=0; }
}
static int is_image(const VmpEntry *e) { return !e->directory && vmp_image_kind(e->name); }
static const char *kind_label(const VmpEntry *e) { return e->directory?"Carpeta":is_image(e)?"Imagen":"MP4"; }
static int exists(const char *path,int dir) {
    SceIoStat stat; memset(&stat,0,sizeof(stat));
    return sceIoGetstat(path,&stat)>=0 && (dir?SCE_S_ISDIR(stat.st_mode):SCE_S_ISREG(stat.st_mode));
}
static void load_state(void) {
    FILE *f=fopen(STATE,"rb"); int valid=-1;
    vmp_library_defaults(&library);
    if(f) { valid=vmp_library_read(f,&library); fclose(f); }
    if(valid) { f=fopen(BACKUP,"rb"); if(f) { (void)vmp_library_read(f,&library); fclose(f); } }
}
int app_save(void) {
    FILE *f=fopen(TEMP,"wb"); int failed;
    if(!f) { app_message("No se pudieron guardar las preferencias."); return -1; }
    failed=vmp_library_write(f,&library); if(fflush(f)) failed=1; if(fclose(f)) failed=1;
    if(failed) { sceIoRemove(TEMP); app_message("No se pudo guardar el progreso."); return -1; }
    /* Only three fixed app-owned paths are ever changed. Retain a recoverable
       old state if the final rename fails or power is lost during replacement. */
    if(exists(STATE,0)) {
        if(exists(BACKUP,0) && sceIoRemove(BACKUP)<0) goto failure;
        if(sceIoRename(STATE,BACKUP)<0) goto failure;
    }
    if(sceIoRename(TEMP,STATE)<0) { if(!exists(STATE,0)) (void)sceIoRename(BACKUP,STATE); goto failure; }
    return 0;
failure:
    app_message("No se pudo reemplazar el archivo de progreso."); return -1;
}
static void draw_notice(void) {
    if(notice[0]) { ui_panel(24,451,912,39,0); ui_text(37,477,.67f,UI_WHITE,notice,885); }
}
static void settings(void) {
    const char *labels[]={"Continuar donde lo dejé","Siguiente vídeo automático","Saltar al avanzar","Ajuste de imagen","Mostrar controles","Subtítulos externos","Volumen"};
    size_t selected=0; int exit=0;
    while(!exit) {
        unsigned b=app_buttons(); char value[64];
        if(app_touch.tap) {
            if(app_touch.y>=500) b|=SCE_CTRL_CIRCLE;
            else if(app_touch.y>=120 && app_touch.y<428) {
                selected=(size_t)(app_touch.y-120)/44;
                b|=app_touch.x<650?SCE_CTRL_LEFT:SCE_CTRL_RIGHT;
            }
        }
        if(b&(SCE_CTRL_CIRCLE|SCE_CTRL_START)) { (void)app_save(); break; }
        if(b&SCE_CTRL_UP) selected=selected?selected-1:6;
        if(b&SCE_CTRL_DOWN) selected=(selected+1)%7;
        if(b&(SCE_CTRL_CROSS|SCE_CTRL_LEFT|SCE_CTRL_RIGHT)) {
            switch(selected) {
            case 0: library.resume=!library.resume; break;
            case 1: library.autoplay=!library.autoplay; break;
            case 2: library.seek_index=(library.seek_index+((b&SCE_CTRL_LEFT)?2:1))%3; break;
            case 3: library.fill=!library.fill; break;
            case 4: library.hide_index=(library.hide_index+((b&SCE_CTRL_LEFT)?2:1))%3; break;
            case 5: library.subtitles=!library.subtitles; break;
            default: if(b&SCE_CTRL_LEFT) library.volume=library.volume>=10?library.volume-10:0;
                else library.volume=library.volume<100?library.volume+10:0;
                if(library.volume>100) library.volume=100;
            }
        }
        app_frame_begin(); ui_background(); ui_header("Videoteca");
        ui_icon(UI_SETTINGS,25,57,40); ui_text(78,86,1.2f,UI_WHITE,"Ajustes",500);
        ui_panel(24,109,912,330,0);
        for(size_t i=0;i<7;i++) {
            int y=120+(int)i*44;
            if(i==selected) ui_panel(35,y,890,41,1);
            ui_text(55,y+28,.85f,UI_WHITE,labels[i],535);
            switch(i) {
            case 0: vmp_copy(value,sizeof(value),library.resume?"Sí":"No"); break;
            case 1: vmp_copy(value,sizeof(value),library.autoplay?"Sí":"No"); break;
            case 2: snprintf(value,sizeof(value),"%u segundos",vmp_seek_seconds(&library)); break;
            case 3: vmp_copy(value,sizeof(value),library.fill?"Llenar pantalla":"Mantener proporción"); break;
            case 4: snprintf(value,sizeof(value),"%u segundos",vmp_hide_seconds(&library)); break;
            case 5: vmp_copy(value,sizeof(value),library.subtitles?"Activados":"Desactivados"); break;
            default: snprintf(value,sizeof(value),"%u%%",library.volume);
            }
            ui_text(665,y+28,.8f,i==selected?UI_CYAN:UI_MUTED,value,235);
        }
        draw_notice(); ui_footer("X cambiar   izquierda/derecha ajustar   O guardar y volver"); app_frame_end();
    }
}
static int options(void) {
    const char *labels[]={"Ajustes","Nueva carpeta","Actualizar biblioteca","Orden: A-Z / Z-A","Salir de Videoteca"};
    const int ids[]={UI_SETTINGS,UI_FOLDER,UI_LIBRARY,UI_SORT,UI_BACK}; size_t selected=0;
    for(;;) {
        unsigned b=app_buttons();
        if(app_touch.tap) {
            if(app_touch.y>=500) b|=SCE_CTRL_CIRCLE;
            else if(app_touch.x>=210 && app_touch.x<750 && app_touch.y>=127 && app_touch.y<492) {
                selected=(size_t)(app_touch.y-127)/73; b|=SCE_CTRL_CROSS;
            }
        }
        if(b&SCE_CTRL_CIRCLE) return 0;
        if(b&SCE_CTRL_UP) selected=selected?selected-1:4;
        if(b&SCE_CTRL_DOWN) selected=(selected+1)%5;
        if(b&SCE_CTRL_CROSS) { if(!selected) { settings(); return 0; } return (int)selected; }
        app_frame_begin(); ui_background(); ui_header("Opciones");
        for(size_t i=0;i<5;i++) { int y=127+(int)i*73; ui_panel(210,y,540,61,i==selected); ui_icon(ids[i],227,y+10,40); ui_text(290,y+40,.95f,UI_WHITE,labels[i],430); }
        draw_notice(); ui_footer("X elegir   O volver"); app_frame_end();
    }
}
static int child_path(char *out,const char *path,const VmpEntry *e) { return vmp_copy(out,VMP_PATH_CAP,path) || vmp_child(out,e->name,e->directory); }
static void parent_of(char *out,const char *file) { char *p; vmp_copy(out,VMP_PATH_CAP,file); p=strrchr(out,'/'); if(p) p[1]=0; }
/* Creates a folder inside the library directory shown. Returns 1 when the listing changed. */
static int new_folder(const char *path) {
    char name[256],child[VMP_PATH_CAP]; int input=app_text_input("Nombre de la carpeta",name,sizeof(name)),made;
    if(input>0) return 0;
    if(input<0) { app_message("No se pudo abrir el teclado."); return 0; }
    if(!vmp_folder_name_valid(name)) { app_message("Nombre no válido: sin \\ / : * ? \" < > |, ni espacios o punto al borde."); return 0; }
    if(vmp_copy(child,sizeof(child),path) || vmp_child(child,name,1)) { app_message("Nombre demasiado largo para esta ubicación."); return 0; }
    if(exists(child,1) || exists(child,0)) { app_message("Ya existe un elemento con ese nombre."); return 0; }
    made=sceIoMkdir(child,0777);
    if(made<0) { (void)snprintf(notice,sizeof(notice),"No se pudo crear la carpeta: 0x%08X",(unsigned)made); return 0; }
    app_message("Carpeta creada."); return 1;
}
static const VmpProgress *recent(void) {
    for(size_t i=0;i<library.count;i++) if(!library.items[i].watched && library.items[i].position>0 && exists(library.items[i].path,0)) return &library.items[i];
    return NULL;
}
int app_browser(int (*play)(const char *)) {
    VmpEntry *entries=calloc(VMP_MAX_ENTRIES,sizeof(*entries));
    size_t map[VMP_MAX_ENTRIES],count=0,total=0,selected=0;
    char path[VMP_PATH_CAP]=VMP_ROOT,tab_root[VMP_PATH_CAP]=VMP_ROOT;
    unsigned tab=0; int refresh=1,list_view=0,descending=0,focus_recent=0,done=0;
    if(!entries) return 1;
    (void)sceIoMkdir("ux0:data/VMPH00001",0777); (void)sceIoMkdir(VMP_ROOT,0777); load_state();
    for(;;) {
        unsigned b=app_buttons();
        if(app_touch.tap && app_touch.x>=350 && app_touch.x<=610 && app_touch.y>=407 && app_touch.y<=462) b|=SCE_CTRL_CROSS;
        if(b&SCE_CTRL_START) { free(entries); return 0; }
        if(b&SCE_CTRL_CROSS) break;
        app_frame_begin(); ui_welcome(); app_frame_end();
    }
    while(!done) {
        unsigned b; int at_root,grid,desired_tab=-1; const VmpProgress *last;
        if(refresh) {
            total=0; count=0; (void)app_list(path,entries,&total);
            for(size_t i=0;i<total;i++) {
                if(!strcmp(path,tab_root) && tab==1 && !entries[i].directory) continue;
                if(!strcmp(path,tab_root) && tab==2 && !(entries[i].regular && vmp_is_mp4(entries[i].name))) continue;
                if(!strcmp(path,tab_root) && tab==TAB_IMAGES && !entries[i].directory && !is_image(&entries[i])) continue;
                map[count++]=i;
            }
            if(descending) for(size_t i=0;i<count/2;i++) { size_t t=map[i]; map[i]=map[count-1-i]; map[count-1-i]=t; }
            if(selected>=count) selected=count?count-1:0;
            focus_recent=0; ui_clear_covers(); refresh=0;
        }
        at_root=!strcmp(path,tab_root); grid=at_root && tab!=TAB_FOLDERS && !list_view;
        last=recent(); b=app_buttons();
        if(app_touch.swipe) {
            size_t page=grid?4:5;
            if(app_touch.swipe>0) selected=selected+page<count?selected+page:selected;
            else selected=selected>=page?selected-page:0;
            focus_recent=0; ui_clear_covers();
        }
        if(app_touch.tap) {
            int x=app_touch.x,y=app_touch.y;
            if(y>=50 && y<100 && x>=130 && x<910) desired_tab=(x-130)/156;
            else if(y>=500) {
                if(x<200) b|=SCE_CTRL_CIRCLE;
                else if(x<400) b|=SCE_CTRL_SQUARE;
                else if(x<650) b|=SCE_CTRL_TRIANGLE;
                else {
                    size_t page=grid?4:5;
                    if(x<800) selected=selected>=page?selected-page:0;
                    else selected=selected+page<count?selected+page:selected;
                    focus_recent=0; ui_clear_covers();
                }
            } else if(grid && last && y>=138 && y<257 && x>=24 && x<936) {
                focus_recent=1; b|=SCE_CTRL_CROSS;
            } else if(grid) {
                int top=last?302:144;
                if(y>=top && y<top+175 && x>=25 && x<937) {
                    size_t pick=(selected/4)*4+(size_t)(x-25)/234;
                    if(pick<count) { selected=pick; focus_recent=0; b|=SCE_CTRL_CROSS; }
                }
            } else if(x>=32 && x<586 && y>=153 && y<428) {
                size_t pick=(selected/5)*5+(size_t)(y-153)/55;
                if(pick<count) { selected=pick; b|=SCE_CTRL_CROSS; }
            }
        }
        if(b&(SCE_CTRL_START|SCE_CTRL_TRIANGLE)) {
            int choice=options();
            if(choice==1 && new_folder(path)) refresh=1;
            if(choice==2) { notice[0]=0; refresh=1; }
            if(choice==3) { descending=!descending; refresh=1; }
            if(choice==4) done=1;
            continue;
        }
        if(desired_tab>=0 || (b&(SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER))) {
            tab=desired_tab>=0?(unsigned)desired_tab:(tab+((b&SCE_CTRL_LTRIGGER)?TABS-1:1))%TABS;
            vmp_copy(tab_root,sizeof(tab_root),VMP_ROOT);
            if(tab==1 && exists(VMP_ROOT"Series/",1)) vmp_copy(tab_root,sizeof(tab_root),VMP_ROOT"Series/");
            if(tab==2 && exists(VMP_ROOT"Peliculas/",1)) vmp_copy(tab_root,sizeof(tab_root),VMP_ROOT"Peliculas/");
            if(tab==TAB_IMAGES && exists(VMP_ROOT"Imagenes/",1)) vmp_copy(tab_root,sizeof(tab_root),VMP_ROOT"Imagenes/");
            vmp_copy(path,sizeof(path),tab_root); selected=0; notice[0]=0; refresh=1; continue;
        }
        if(b&SCE_CTRL_SQUARE) { list_view=!list_view; ui_clear_covers(); }
        if(b&SCE_CTRL_CIRCLE) {
            if(strcmp(path,tab_root)) { (void)vmp_parent(path); selected=0; notice[0]=0; refresh=1; }
            else { tab=0; vmp_copy(path,sizeof(path),VMP_ROOT); vmp_copy(tab_root,sizeof(tab_root),VMP_ROOT); refresh=1; }
            continue;
        }
        if(grid) {
            if((b&SCE_CTRL_UP) && last) focus_recent=1;
            if(b&SCE_CTRL_DOWN) focus_recent=0;
            if(!focus_recent && count) {
                size_t old=selected;
                if(b&SCE_CTRL_LEFT) selected=selected?selected-1:count-1;
                if(b&SCE_CTRL_RIGHT) selected=(selected+1)%count;
                if(old/4!=selected/4) ui_clear_covers();
            }
        } else if(count) {
            size_t old=selected;
            if(b&SCE_CTRL_UP) selected=selected?selected-1:count-1;
            if(b&SCE_CTRL_DOWN) selected=(selected+1)%count;
            if(b&SCE_CTRL_LEFT) selected=selected>=5?selected-5:0;
            if(b&SCE_CTRL_RIGHT) selected=selected+5<count?selected+5:count-1;
            if(old!=selected) ui_clear_covers();
        }
        if(b&SCE_CTRL_CROSS) {
            char child[VMP_PATH_CAP];
            if(focus_recent && last) { vmp_copy(child,sizeof(child),last->path); notice[0]=0; play(child); refresh=1; continue; }
            if(count && !child_path(child,path,&entries[map[selected]])) {
                notice[0]=0;
                if(entries[map[selected]].directory) { vmp_copy(path,sizeof(path),child); selected=0; refresh=1; continue; }
                if(is_image(&entries[map[selected]])) { selected=app_viewer(path,entries,map,count,selected); ui_clear_covers(); continue; }
                for(;;) {
                    int ended=play(child);
                    if(ended!=1 || !library.autoplay) break;
                    do { selected++; } while(selected<count && (entries[map[selected]].directory || is_image(&entries[map[selected]])));
                    if(selected>=count || child_path(child,path,&entries[map[selected]])) break;
                }
                refresh=1; continue;
            }
        }
        app_frame_begin(); ui_background(); ui_header("Videoteca");
        for(unsigned i=0;i<TABS;i++) {
            int x=130+(int)i*156; ui_icon(tab_icons[i],x,55,33); ui_text(x+40,78,.8f,i==tab?UI_WHITE:UI_MUTED,tabs[i],112);
            if(i==tab) vita2d_draw_rectangle((float)x,94,140,3,UI_CYAN);
        }
        if(grid) {
            char caption[256],folder[VMP_PATH_CAP];
            int shift=last?0:-158;
            if(last) {
                ui_text(25,125,.9f,UI_WHITE,"Seguir viendo",500);
                ui_panel(24,138,912,119,focus_recent);
                char position[24]; parent_of(folder,last->path); title(caption,sizeof(caption),leaf(last->path));
                ui_cover(last->path,35,148,185,98,"Kirv Studio"); ui_text(241,179,1.1f,UI_WHITE,caption,660);
                ui_time(position,sizeof(position),last->position); ui_text(242,207,.8f,UI_MUTED,position,200);
                ui_bar(242,222,470,last->position,last->duration); ui_icon(UI_PLAY,790,192,40); ui_text(836,219,.75f,UI_WHITE,"Continuar",85);
            }
            snprintf(caption,sizeof(caption),"%s  ·  %u elementos",tabs[tab],(unsigned)count); ui_text(25,286+shift,.9f,UI_WHITE,caption,850);
            for(size_t i=(selected/4)*4,col=0;i<count && col<4;i++,col++) {
                VmpEntry *e=&entries[map[i]]; char child[VMP_PATH_CAP]; int x=25+(int)col*234;
                if(child_path(child,path,e)) continue;
                if(e->directory) vmp_copy(folder,sizeof(folder),child); else parent_of(folder,child);
                ui_panel(x-2,302+shift,214,139,!focus_recent && i==selected);
                ui_cover(e->directory?folder:child,x+2,306+shift,206,131,kind_label(e));
                title(caption,sizeof(caption),e->name); ui_text(x+2,465+shift,.77f,UI_WHITE,caption,210);
                if(!e->directory && !is_image(e)) { const VmpProgress *p=vmp_progress(&library,child); if(p && p->duration) ui_bar(x+4,434+shift,198,p->position,p->duration); }
            }
            if(!count) ui_text(40,363+shift,.9f,UI_MUTED,"No hay elementos en esta categoría.",880);
        } else {
            char caption[512]; const char *relative=path+strlen(VMP_ROOT);
            snprintf(caption,sizeof(caption),"%s%s",relative[0]?"Mi biblioteca / ":"Mi biblioteca",relative);
            ui_text(25,127,.85f,UI_WHITE,caption,900);
            ui_panel(24,144,570,294,0); ui_panel(612,144,324,294,0);
            for(size_t i=(selected/5)*5,row=0;i<count && row<5;i++,row++) {
                VmpEntry *e=&entries[map[i]]; int y=153+(int)row*55; char child[VMP_PATH_CAP]; const VmpProgress *p=NULL;
                if(i==selected) ui_panel(32,y,554,51,1);
                ui_icon(e->directory?UI_FOLDER:is_image(e)?UI_IMAGE:UI_PLAY,43,y+7,36);
                title(caption,sizeof(caption),e->name); ui_text(92,y+31,.82f,UI_WHITE,caption,437);
                if(!e->directory && !is_image(e) && !child_path(child,path,e)) p=vmp_progress(&library,child);
                if(p && p->watched) ui_icon(UI_CHECK,547,y+15,20);
            }
            if(count) {
                VmpEntry *e=&entries[map[selected]]; char child[VMP_PATH_CAP],folder[VMP_PATH_CAP];
                if(!child_path(child,path,e)) {
                    if(e->directory) vmp_copy(folder,sizeof(folder),child); else parent_of(folder,child);
                    ui_cover(e->directory?folder:child,627,159,294,166,"Kirv Studio"); title(caption,sizeof(caption),e->name);
                    ui_text(632,355,.95f,UI_WHITE,caption,280); ui_text(632,383,.75f,UI_MUTED,e->directory?"Carpeta local":is_image(e)?"Imagen":"Vídeo MP4",280);
                    if(!e->directory && !is_image(e)) { const VmpProgress *p=vmp_progress(&library,child); if(p) { char tm[24]; ui_time(tm,sizeof(tm),p->position); snprintf(caption,sizeof(caption),"%s  %s",p->watched?"Visto":"En curso",tm); ui_text(632,413,.75f,UI_CYAN,caption,280); } }
                }
            } else ui_text(45,203,.9f,UI_MUTED,"No hay MP4, imágenes o carpetas.",500);
            snprintf(caption,sizeof(caption),"%u de %u",count?(unsigned)selected+1:0,(unsigned)count); ui_text(26,466,.75f,UI_MUTED,caption,900);
        }
        draw_notice(); ui_footer("O volver             Vista               Opciones               < página          página >"); app_frame_end();
    }
    (void)app_save(); free(entries); return 0;
}
