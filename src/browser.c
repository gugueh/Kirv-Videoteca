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
static const char *tabs_es[TABS]={"Todo","Series","Películas","Imágenes","Carpetas"};
static const char *tabs_en[TABS]={"All","Series","Movies","Images","Folders"};
static const int tab_icons[TABS]={UI_LIBRARY,UI_SERIES,UI_MOVIES,UI_IMAGE,UI_FOLDER};
/* The Images tab root is a flat, newest-first list of every picture; its
   entries carry full paths in flat[] instead of names relative to one folder. */
static VmpEntry *listing;
static char (*flat)[VMP_PATH_CAP];
static int flat_mode;
static const char *tab_name(unsigned tab) { return library.language?tabs_en[tab]:tabs_es[tab]; }
static const char *leaf(const char *path) { const char *p=strrchr(path,'/'); return p?p+1:path; }
static void title(char *out,size_t cap,const char *name) {
    vmp_copy(out,cap,name);
    if(vmp_is_mp4(out)) out[strlen(out)-4]=0;
    else if(vmp_image_kind(out)) { char *dot=strrchr(out,'.'); if(dot) *dot=0; }
}
static int is_image(const VmpEntry *e) { return !e->directory && vmp_image_kind(e->name); }
static const char *kind_label(const VmpEntry *e) { return e->directory?L("Carpeta","Folder"):is_image(e)?L("Imagen","Image"):"MP4"; }
/* sceIo calls get directory paths without the trailing slash used internally. */
static void io_path(char *out,const char *path) {
    size_t n; vmp_copy(out,VMP_PATH_CAP,path); n=strlen(out);
    if(n>1 && out[n-1]=='/') out[n-1]=0;
}
static int exists(const char *path,int dir) {
    SceIoStat stat; char io[VMP_PATH_CAP]; io_path(io,path); memset(&stat,0,sizeof(stat));
    return sceIoGetstat(io,&stat)>=0 && (dir?SCE_S_ISDIR(stat.st_mode):SCE_S_ISREG(stat.st_mode));
}
static void load_state(void) {
    FILE *f=fopen(STATE,"rb"); int valid=-1;
    vmp_library_defaults(&library);
    if(f) { valid=vmp_library_read(f,&library); fclose(f); }
    if(valid) { f=fopen(BACKUP,"rb"); if(f) { (void)vmp_library_read(f,&library); fclose(f); } }
}
int app_save(void) {
    FILE *f=fopen(TEMP,"wb"); int failed;
    if(!f) { app_message(L("No se pudieron guardar las preferencias.","Could not save preferences.")); return -1; }
    failed=vmp_library_write(f,&library); if(fflush(f)) failed=1; if(fclose(f)) failed=1;
    if(failed) { sceIoRemove(TEMP); app_message(L("No se pudo guardar el progreso.","Could not save progress.")); return -1; }
    /* Only three fixed app-owned paths are ever changed. Retain a recoverable
       old state if the final rename fails or power is lost during replacement. */
    if(exists(STATE,0)) {
        if(exists(BACKUP,0) && sceIoRemove(BACKUP)<0) goto failure;
        if(sceIoRename(STATE,BACKUP)<0) goto failure;
    }
    if(sceIoRename(TEMP,STATE)<0) { if(!exists(STATE,0)) (void)sceIoRename(BACKUP,STATE); goto failure; }
    return 0;
failure:
    app_message(L("No se pudo reemplazar el archivo de progreso.","Could not replace the progress file.")); return -1;
}
static void draw_notice(void) {
    if(notice[0]) { ui_panel(24,451,912,39,0); ui_text(37,477,.67f,UI_WHITE,notice,885); }
}
#define SETTINGS 8
static void settings(void) {
    size_t selected=0;
    for(;;) {
        const char *labels[SETTINGS]={L("Continuar donde lo dejé","Resume where I left off"),L("Siguiente vídeo automático","Autoplay next video"),
            L("Saltar al avanzar","Seek step"),L("Ajuste de imagen","Picture fit"),L("Mostrar controles","Show controls for"),
            L("Subtítulos externos","External subtitles"),L("Volumen","Volume"),L("Idioma","Language")};
        unsigned b=app_buttons(); char value[64];
        if(app_touch.tap) {
            if(app_touch.y>=500) b|=SCE_CTRL_CIRCLE;
            else if(app_touch.y>=118 && app_touch.y<118+SETTINGS*39) {
                selected=(size_t)(app_touch.y-118)/39;
                b|=app_touch.x<650?SCE_CTRL_LEFT:SCE_CTRL_RIGHT;
            }
        }
        if(b&(SCE_CTRL_CIRCLE|SCE_CTRL_START)) { (void)app_save(); break; }
        if(b&SCE_CTRL_UP) selected=selected?selected-1:SETTINGS-1;
        if(b&SCE_CTRL_DOWN) selected=(selected+1)%SETTINGS;
        if(b&(SCE_CTRL_CROSS|SCE_CTRL_LEFT|SCE_CTRL_RIGHT)) {
            switch(selected) {
            case 0: library.resume=!library.resume; break;
            case 1: library.autoplay=!library.autoplay; break;
            case 2: library.seek_index=(library.seek_index+((b&SCE_CTRL_LEFT)?2:1))%3; break;
            case 3: library.fill=!library.fill; break;
            case 4: library.hide_index=(library.hide_index+((b&SCE_CTRL_LEFT)?2:1))%3; break;
            case 5: library.subtitles=!library.subtitles; break;
            case 6: if(b&SCE_CTRL_LEFT) library.volume=library.volume>=10?library.volume-10:0;
                else library.volume=library.volume<100?library.volume+10:0;
                if(library.volume>100) library.volume=100;
                break;
            default: library.language=!library.language;
            }
        }
        app_frame_begin(); ui_background(); ui_header("Videoteca");
        ui_icon(UI_SETTINGS,25,57,40); ui_text(78,86,1.2f,UI_WHITE,L("Ajustes","Settings"),500);
        ui_panel(24,109,912,330,0);
        for(size_t i=0;i<SETTINGS;i++) {
            int y=118+(int)i*39;
            if(i==selected) ui_panel(35,y,890,36,1);
            ui_text(55,y+25,.82f,UI_WHITE,labels[i],535);
            switch(i) {
            case 0: vmp_copy(value,sizeof(value),library.resume?L("Sí","Yes"):"No"); break;
            case 1: vmp_copy(value,sizeof(value),library.autoplay?L("Sí","Yes"):"No"); break;
            case 2: snprintf(value,sizeof(value),L("%u segundos","%u seconds"),vmp_seek_seconds(&library)); break;
            case 3: vmp_copy(value,sizeof(value),library.fill?L("Llenar pantalla","Fill screen"):L("Mantener proporción","Keep aspect ratio")); break;
            case 4: snprintf(value,sizeof(value),L("%u segundos","%u seconds"),vmp_hide_seconds(&library)); break;
            case 5: vmp_copy(value,sizeof(value),library.subtitles?L("Activados","On"):L("Desactivados","Off")); break;
            case 6: snprintf(value,sizeof(value),"%u%%",library.volume); break;
            default: vmp_copy(value,sizeof(value),library.language?"English":"Español");
            }
            ui_text(665,y+25,.8f,i==selected?UI_CYAN:UI_MUTED,value,235);
        }
        draw_notice(); ui_footer(L("X cambiar   izquierda/derecha ajustar   O guardar y volver","X change   left/right adjust   O save and back")); app_frame_end();
    }
}
enum { OPT_SETTINGS, OPT_NEW_FOLDER, OPT_RENAME, OPT_DELETE, OPT_REFRESH, OPT_SORT, OPT_EXIT, OPTIONS };
static int options(void) {
    const int ids[OPTIONS]={UI_SETTINGS,UI_FOLDER,UI_SORT,UI_BACK,UI_LIBRARY,UI_SORT,UI_BACK}; size_t selected=0;
    for(;;) {
        const char *labels[OPTIONS]={L("Ajustes","Settings"),L("Nueva carpeta","New folder"),L("Renombrar seleccionado","Rename selected"),
            L("Eliminar seleccionado","Delete selected"),L("Actualizar biblioteca","Refresh library"),L("Orden: A-Z / Z-A","Order: A-Z / Z-A"),L("Salir de Videoteca","Exit Videoteca")};
        unsigned b=app_buttons();
        if(app_touch.tap) {
            if(app_touch.y>=500) b|=SCE_CTRL_CIRCLE;
            else if(app_touch.x>=210 && app_touch.x<750 && app_touch.y>=58 && app_touch.y<58+OPTIONS*61) {
                selected=(size_t)(app_touch.y-58)/61; b|=SCE_CTRL_CROSS;
            }
        }
        if(b&SCE_CTRL_CIRCLE) return -1;
        if(b&SCE_CTRL_UP) selected=selected?selected-1:OPTIONS-1;
        if(b&SCE_CTRL_DOWN) selected=(selected+1)%OPTIONS;
        if(b&SCE_CTRL_CROSS) { if(selected==OPT_SETTINGS) { settings(); return -1; } return (int)selected; }
        app_frame_begin(); ui_background(); ui_header(L("Opciones","Options"));
        for(size_t i=0;i<OPTIONS;i++) { int y=58+(int)i*61; ui_panel(210,y,540,53,i==selected); ui_icon(ids[i],227,y+9,36); ui_text(285,y+35,.9f,UI_WHITE,labels[i],440); }
        draw_notice(); ui_footer(L("X elegir   O volver","X choose   O back")); app_frame_end();
    }
}
/* Yes/no screen used before deleting anything. */
static int confirm(const char *question,const char *name) {
    for(;;) {
        unsigned b=app_buttons();
        if(app_touch.tap) {
            if(app_touch.y>=300 && app_touch.y<360 && app_touch.x>=230 && app_touch.x<470) b|=SCE_CTRL_CROSS;
            else if(app_touch.y>=300 && app_touch.y<360 && app_touch.x>=490 && app_touch.x<730) b|=SCE_CTRL_CIRCLE;
        }
        if(b&SCE_CTRL_CROSS) return 1;
        if(b&SCE_CTRL_CIRCLE) return 0;
        app_frame_begin(); ui_background(); ui_header(L("Confirmar","Confirm"));
        ui_panel(150,150,660,240,0);
        ui_text(180,205,1.0f,UI_WHITE,question,600); ui_text(180,250,.85f,UI_CYAN,name,600);
        ui_panel(230,300,240,60,1); ui_text(270,338,.9f,UI_WHITE,L("X Eliminar","X Delete"),180);
        ui_panel(490,300,240,60,0); ui_text(530,338,.9f,UI_WHITE,L("O Cancelar","O Cancel"),180);
        ui_footer(L("X eliminar   O cancelar","X delete   O cancel")); app_frame_end();
    }
}
static int child_path(char *out,const char *path,const VmpEntry *e) {
    if(flat_mode) return vmp_copy(out,VMP_PATH_CAP,flat[e-listing]);
    return vmp_copy(out,VMP_PATH_CAP,path) || vmp_child(out,e->name,e->directory);
}
static void parent_of(char *out,const char *file) { char *p; vmp_copy(out,VMP_PATH_CAP,file); p=strrchr(out,'/'); if(p) p[1]=0; }
static void read_only(void) { app_message(L("Las fotos de la consola son de solo lectura.","The console's pictures are read-only.")); }
static void invalid_name(void) { app_message(L("Nombre no válido: sin \\ / : * ? \" < > |, ni espacios o punto al borde.","Invalid name: no \\ / : * ? \" < > |, nor edge spaces or a final dot.")); }
static void io_error(const char *what,int code) { (void)snprintf(notice,sizeof(notice),"%s: 0x%08X",what,(unsigned)code); }
/* Creates a folder inside the library directory shown. Returns 1 when the listing changed. */
static int new_folder(const char *path) {
    char name[256],child[VMP_PATH_CAP],io[VMP_PATH_CAP]; int input,made;
    if(flat_mode || !vmp_path_writable(path)) { app_message(L("Entra a una carpeta de la biblioteca para crear otra.","Open a library folder to create one inside it.")); return 0; }
    input=app_text_input(L("Nombre de la carpeta","Folder name"),"",name,sizeof(name));
    if(input>0) return 0;
    if(input<0) { app_message(L("No se pudo abrir el teclado.","Could not open the keyboard.")); return 0; }
    if(!vmp_folder_name_valid(name)) { invalid_name(); return 0; }
    if(vmp_copy(child,sizeof(child),path) || vmp_child(child,name,1)) { app_message(L("Nombre demasiado largo para esta ubicación.","Name too long for this location.")); return 0; }
    if(exists(child,1) || exists(child,0)) { app_message(L("Ya existe un elemento con ese nombre.","An item with that name already exists.")); return 0; }
    io_path(io,child); made=sceIoMkdir(io,0777);
    if(made<0) { io_error(L("No se pudo crear la carpeta","Could not create the folder"),made); return 0; }
    app_message(L("Carpeta creada.","Folder created.")); return 1;
}
/* "x.mp4" -> "x.srt" / "x.thumb.png" sidecars that follow a video. */
static int sidecar(char *out,const char *video,const char *suffix) {
    size_t n=strlen(video),m=strlen(suffix);
    if(!vmp_is_mp4(video) || n-4+m>=VMP_PATH_CAP) return -1;
    memcpy(out,video,n-4); memcpy(out+n-4,suffix,m+1); return 0;
}
static int rename_entry(const char *path,const VmpEntry *e) {
    char from[VMP_PATH_CAP],to[VMP_PATH_CAP],io_from[VMP_PATH_CAP],io_to[VMP_PATH_CAP],base[256],name[256],ext[16]="";
    int input,result;
    if(child_path(from,path,e)) return 0;
    if(!vmp_path_writable(from)) { read_only(); return 0; }
    vmp_copy(base,sizeof(base),e->name);
    if(!e->directory && (vmp_is_mp4(base) || vmp_image_kind(base))) { char *dot=strrchr(base,'.'); if(dot && strlen(dot)<sizeof(ext)) { vmp_copy(ext,sizeof(ext),dot); *dot=0; } }
    input=app_text_input(L("Nuevo nombre","New name"),base,name,sizeof(name)-sizeof(ext));
    if(input>0) return 0;
    if(input<0) { app_message(L("No se pudo abrir el teclado.","Could not open the keyboard.")); return 0; }
    if(strlen(name)+strlen(ext)>=sizeof(name)) { invalid_name(); return 0; }
    strcat(name,ext);
    if(!vmp_folder_name_valid(name)) { invalid_name(); return 0; }
    if(!strcmp(name,e->name)) return 0;
    if(e->directory) { vmp_copy(to,sizeof(to),from); (void)vmp_parent(to); } else parent_of(to,from);
    if(vmp_child(to,name,e->directory)) { app_message(L("Nombre demasiado largo para esta ubicación.","Name too long for this location.")); return 0; }
    if(exists(to,1) || exists(to,0)) { app_message(L("Ya existe un elemento con ese nombre.","An item with that name already exists.")); return 0; }
    io_path(io_from,from); io_path(io_to,to);
    if((result=sceIoRename(io_from,io_to))<0) { io_error(L("No se pudo renombrar","Could not rename"),result); return 0; }
    if(!e->directory) {
        char a[VMP_PATH_CAP],b[VMP_PATH_CAP];
        if(!sidecar(a,from,".srt") && !sidecar(b,to,".srt") && exists(a,0)) (void)sceIoRename(a,b);
        if(!sidecar(a,from,".thumb.png") && !sidecar(b,to,".thumb.png") && exists(a,0)) (void)sceIoRename(a,b);
    }
    vmp_rename_progress(&library,from,to); (void)app_save();
    app_message(L("Renombrado.","Renamed.")); return 1;
}
static int delete_entry(const char *path,const VmpEntry *e) {
    char target[VMP_PATH_CAP],io[VMP_PATH_CAP],shown[256]; int result;
    if(child_path(target,path,e)) return 0;
    if(!vmp_path_writable(target)) { read_only(); return 0; }
    title(shown,sizeof(shown),e->name);
    if(!confirm(e->directory?L("¿Eliminar esta carpeta vacía?","Delete this empty folder?"):L("¿Eliminar este archivo?","Delete this file?"),shown)) return 0;
    io_path(io,target);
    if(e->directory) {
        if((result=sceIoRmdir(io))<0) { io_error(L("La carpeta no está vacía o no se pudo borrar","The folder is not empty or could not be deleted"),result); return 0; }
    } else {
        char extra[VMP_PATH_CAP];
        if((result=sceIoRemove(io))<0) { io_error(L("No se pudo eliminar","Could not delete"),result); return 0; }
        if(!sidecar(extra,target,".srt") && exists(extra,0)) (void)sceIoRemove(extra);
        if(!sidecar(extra,target,".thumb.png") && exists(extra,0)) (void)sceIoRemove(extra);
        vmp_forget(&library,target); (void)app_save();
    }
    app_message(L("Eliminado.","Deleted.")); return 1;
}
/* Opens the viewer over the images of the current listing, in the order shown. */
static size_t open_images(const char *path,const size_t *map,size_t count,size_t selected) {
    char (*list)[VMP_PATH_CAP]=malloc(count*sizeof(*list)); size_t *back=malloc(count*sizeof(*back)),n=0,start=0,result=selected;
    if(!list || !back) { free(list); free(back); app_message(L("No hay memoria para abrir la imagen.","Not enough memory to open the image.")); return selected; }
    for(size_t i=0;i<count;i++) {
        if(!is_image(&listing[map[i]]) || child_path(list[n],path,&listing[map[i]])) continue;
        if(i==selected) start=n;
        back[n++]=i;
    }
    if(n) result=back[app_viewer(list,n,start)];
    free(list); free(back); return result;
}
static int newest_first(const void *a,const void *b) {
    const VmpEntry *x=&listing[*(const size_t *)a],*y=&listing[*(const size_t *)b];
    if(x->time!=y->time) return x->time<y->time?1:-1;
    return strcmp(x->name,y->name);
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
    flat=calloc(VMP_MAX_ENTRIES,sizeof(*flat)); listing=entries;
    if(!entries || !flat) { free(entries); free(flat); return 1; }
    (void)sceIoMkdir("ux0:data/VMPH00001",0777); (void)sceIoMkdir("ux0:data/VMPH00001/video",0777); load_state();
    for(;;) {
        unsigned b=app_buttons();
        if(app_touch.tap && app_touch.x>=350 && app_touch.x<=610 && app_touch.y>=407 && app_touch.y<=462) b|=SCE_CTRL_CROSS;
        if(b&SCE_CTRL_START) { free(entries); free(flat); return 0; }
        if(b&SCE_CTRL_CROSS) break;
        app_frame_begin(); ui_welcome(); app_frame_end();
    }
    while(!done) {
        unsigned b; int at_root,grid,desired_tab=-1; const VmpProgress *last;
        if(refresh) {
            total=0; count=0;
            flat_mode=tab==TAB_IMAGES && !strcmp(path,tab_root);
            if(flat_mode) {
                int access=app_list_images(entries,flat,VMP_MAX_ENTRIES,&total);
                for(size_t i=0;i<total;i++) map[count++]=i;
                qsort(map,count,sizeof(*map),newest_first);
                if(access<0) io_error(L("Sin acceso a las fotos de la consola; se muestran las de la biblioteca","No access to the console's pictures; showing library images"),access);
            } else {
                (void)app_list(path,entries,&total);
                for(size_t i=0;i<total;i++) {
                    if(!strcmp(path,tab_root) && tab==1 && !entries[i].directory) continue;
                    if(!strcmp(path,tab_root) && tab==2 && !(entries[i].regular && vmp_is_mp4(entries[i].name))) continue;
                    map[count++]=i;
                }
            }
            if(descending) for(size_t i=0;i<count/2;i++) { size_t t=map[i]; map[i]=map[count-1-i]; map[count-1-i]=t; }
            if(selected>=count) selected=count?count-1:0;
            focus_recent=0; ui_clear_covers(); refresh=0;
        }
        at_root=!strcmp(path,tab_root); grid=at_root && tab!=TAB_FOLDERS && !list_view;
        last=tab==TAB_IMAGES?NULL:recent(); b=app_buttons();
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
            const VmpEntry *chosen=count?&entries[map[selected]]:NULL;
            if(choice==OPT_NEW_FOLDER && new_folder(path)) refresh=1;
            if(choice==OPT_RENAME) { if(!chosen) app_message(L("No hay nada seleccionado.","Nothing selected.")); else if(rename_entry(path,chosen)) refresh=1; }
            if(choice==OPT_DELETE) { if(!chosen) app_message(L("No hay nada seleccionado.","Nothing selected.")); else if(delete_entry(path,chosen)) refresh=1; }
            if(choice==OPT_REFRESH) { notice[0]=0; refresh=1; }
            if(choice==OPT_SORT) { descending=!descending; refresh=1; }
            if(choice==OPT_EXIT) done=1;
            continue;
        }
        if(desired_tab>=0 || (b&(SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER))) {
            tab=desired_tab>=0?(unsigned)desired_tab:(tab+((b&SCE_CTRL_LTRIGGER)?TABS-1:1))%TABS;
            vmp_copy(tab_root,sizeof(tab_root),VMP_ROOT);
            if(tab==1 && exists(VMP_ROOT"Series/",1)) vmp_copy(tab_root,sizeof(tab_root),VMP_ROOT"Series/");
            if(tab==2 && exists(VMP_ROOT"Peliculas/",1)) vmp_copy(tab_root,sizeof(tab_root),VMP_ROOT"Peliculas/");
            if(tab==TAB_IMAGES) vmp_copy(tab_root,sizeof(tab_root),VMP_PICTURES);
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
            if(old/5!=selected/5) ui_clear_covers();
        }
        if(b&SCE_CTRL_CROSS) {
            char child[VMP_PATH_CAP];
            if(focus_recent && last) { vmp_copy(child,sizeof(child),last->path); notice[0]=0; play(child); refresh=1; continue; }
            if(count && !child_path(child,path,&entries[map[selected]])) {
                notice[0]=0;
                if(entries[map[selected]].directory) { vmp_copy(path,sizeof(path),child); selected=0; refresh=1; continue; }
                if(is_image(&entries[map[selected]])) {
                    ui_clear_covers(); selected=open_images(path,map,count,selected); ui_clear_covers(); continue;
                }
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
            int x=130+(int)i*156; ui_icon(tab_icons[i],x,55,33); ui_text(x+40,78,.8f,i==tab?UI_WHITE:UI_MUTED,tab_name(i),112);
            if(i==tab) vita2d_draw_rectangle((float)x,94,140,3,UI_CYAN);
        }
        if(grid) {
            char caption[256],folder[VMP_PATH_CAP];
            int shift=last?0:-158;
            if(last) {
                ui_text(25,125,.9f,UI_WHITE,L("Seguir viendo","Continue watching"),500);
                ui_panel(24,138,912,119,focus_recent);
                char position[24]; parent_of(folder,last->path); title(caption,sizeof(caption),leaf(last->path));
                ui_cover(last->path,35,148,185,98,"Kirv Studio"); ui_text(241,179,1.1f,UI_WHITE,caption,660);
                ui_time(position,sizeof(position),last->position); ui_text(242,207,.8f,UI_MUTED,position,200);
                ui_bar(242,222,470,last->position,last->duration); ui_icon(UI_PLAY,790,192,40); ui_text(836,219,.75f,UI_WHITE,L("Continuar","Resume"),85);
            }
            snprintf(caption,sizeof(caption),L("%s  ·  %u elementos","%s  ·  %u items"),tab_name(tab),(unsigned)count); ui_text(25,286+shift,.9f,UI_WHITE,caption,850);
            for(size_t i=(selected/4)*4,col=0;i<count && col<4;i++,col++) {
                VmpEntry *e=&entries[map[i]]; char child[VMP_PATH_CAP]; int x=25+(int)col*234;
                if(child_path(child,path,e)) continue;
                if(e->directory) vmp_copy(folder,sizeof(folder),child); else parent_of(folder,child);
                ui_panel(x-2,302+shift,214,139,!focus_recent && i==selected);
                ui_cover(e->directory?folder:child,x+2,306+shift,206,131,kind_label(e));
                title(caption,sizeof(caption),e->name); ui_text(x+2,465+shift,.77f,UI_WHITE,caption,210);
                if(!e->directory && !is_image(e)) { const VmpProgress *p=vmp_progress(&library,child); if(p && p->duration) ui_bar(x+4,434+shift,198,p->position,p->duration); }
            }
            if(!count) ui_text(40,363+shift,.9f,UI_MUTED,L("No hay elementos en esta categoría.","Nothing in this category."),880);
        } else {
            char caption[512];
            if(flat_mode) vmp_copy(caption,sizeof(caption),L("Todas las imágenes, de la más nueva a la más vieja","All images, newest first"));
            else if(!strncmp(path,VMP_ROOT,strlen(VMP_ROOT))) {
                const char *relative=path+strlen(VMP_ROOT);
                snprintf(caption,sizeof(caption),"%s%s%s",L("Mi biblioteca","My library"),relative[0]?" / ":"",relative);
            } else vmp_copy(caption,sizeof(caption),path);
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
                    ui_text(632,355,.95f,UI_WHITE,caption,280);
                    ui_text(632,383,.75f,UI_MUTED,e->directory?L("Carpeta local","Local folder"):is_image(e)?(vmp_path_writable(child)?L("Imagen","Image"):L("Imagen de la consola","Console picture")):L("Vídeo MP4","MP4 video"),280);
                    if(!e->directory && !is_image(e)) { const VmpProgress *p=vmp_progress(&library,child); if(p) { char tm[24]; ui_time(tm,sizeof(tm),p->position); snprintf(caption,sizeof(caption),"%s  %s",p->watched?L("Visto","Watched"):L("En curso","In progress"),tm); ui_text(632,413,.75f,UI_CYAN,caption,280); } }
                }
            } else ui_text(45,203,.9f,UI_MUTED,L("No hay MP4, imágenes o carpetas.","No MP4s, images or folders."),500);
            snprintf(caption,sizeof(caption),L("%u de %u","%u of %u"),count?(unsigned)selected+1:0,(unsigned)count); ui_text(26,466,.75f,UI_MUTED,caption,900);
        }
        draw_notice(); ui_footer(L("O volver             Vista               Opciones               < página          página >","O back               View                Options                < page            page >")); app_frame_end();
    }
    (void)app_save(); free(entries); free(flat); return 0;
}
