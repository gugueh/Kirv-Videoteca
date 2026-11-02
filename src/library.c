#include "library.h"
#include <stdlib.h>
#include <string.h>
#define TIME_LIMIT UINT64_C(604800000)
static const unsigned seek_values[] = {5,10,30};
static const unsigned hide_values[] = {3,5,10};
unsigned vmp_seek_seconds(const VmpLibrary *s) { return seek_values[s->seek_index < 3 ? s->seek_index : 1]; }
unsigned vmp_hide_seconds(const VmpLibrary *s) { return hide_values[s->hide_index < 3 ? s->hide_index : 0]; }
void vmp_library_defaults(VmpLibrary *s) { memset(s,0,sizeof(*s)); s->resume=1; s->subtitles=1; s->seek_index=1; s->volume=100; }
static int options_valid(const VmpLibrary *s) {
    return s->resume<=1 && s->autoplay<=1 && s->seek_index<3 && s->hide_index<3 &&
        s->subtitles<=1 && s->fill<=1 && s->volume<=100 && s->count<=VMP_HISTORY;
}
static int progress_valid(const VmpProgress *p) {
    return memchr(p->path,0,sizeof(p->path)) && vmp_path_valid(p->path,0) && vmp_is_mp4(p->path) &&
        p->position<=TIME_LIMIT && p->duration<=TIME_LIMIT && (!p->duration || p->position<=p->duration) && p->watched<=1;
}
const VmpProgress *vmp_progress(const VmpLibrary *s, const char *path) {
    for(size_t i=0;i<s->count;i++) if(!strcmp(s->items[i].path,path)) return &s->items[i];
    return NULL;
}
int vmp_remember(VmpLibrary *s,const char *path,uint64_t pos,uint64_t duration,unsigned watched) {
    VmpProgress item; size_t found;
    memset(&item,0,sizeof(item));
    if(vmp_copy(item.path,sizeof(item.path),path)) return -1;
    item.position=pos; item.duration=duration; item.watched=watched;
    if(!progress_valid(&item) || s->count>VMP_HISTORY) return -1;
    for(found=0;found<s->count;found++) if(!strcmp(s->items[found].path,path)) break;
    if(found==s->count && s->count<VMP_HISTORY) s->count++;
    if(found>=VMP_HISTORY) found=VMP_HISTORY-1;
    if(found) memmove(s->items+1,s->items,found*sizeof(item));
    s->items[0]=item;
    return 0;
}
static int write_u64(FILE *f,uint64_t value) {
    unsigned char b[8]; for(unsigned i=0;i<8;i++) b[i]=(unsigned char)(value>>(8*i));
    return fwrite(b,1,8,f)==8 ? 0 : -1;
}
static int read_u64(FILE *f,uint64_t *value) {
    unsigned char b[8]; if(fread(b,1,8,f)!=8) return -1;
    *value=0; for(unsigned i=0;i<8;i++) *value|=(uint64_t)b[i]<<(8*i); return 0;
}
int vmp_library_write(FILE *f,const VmpLibrary *s) {
    unsigned char options[8];
    if(!f || !options_valid(s)) return -1;
    for(size_t i=0;i<s->count;i++) if(!progress_valid(&s->items[i])) return -1;
    options[0]=(unsigned char)s->resume; options[1]=(unsigned char)s->autoplay;
    options[2]=(unsigned char)s->seek_index; options[3]=(unsigned char)s->hide_index;
    options[4]=(unsigned char)s->subtitles; options[5]=(unsigned char)s->fill;
    options[6]=(unsigned char)s->volume; options[7]=(unsigned char)s->count;
    if(fwrite("KIRVUI01",1,8,f)!=8 || fwrite(options,1,8,f)!=8) return -1;
    for(size_t i=0;i<s->count;i++) {
        const VmpProgress *p=&s->items[i]; size_t n=strlen(p->path);
        if(fputc((int)(n&255),f)==EOF || fputc((int)(n>>8),f)==EOF || fwrite(p->path,1,n,f)!=n ||
            write_u64(f,p->position) || write_u64(f,p->duration) || fputc((int)p->watched,f)==EOF) return -1;
    }
    return ferror(f) ? -1 : 0;
}
int vmp_library_read(FILE *f,VmpLibrary *s) {
    unsigned char head[16]; VmpLibrary *tmp; int result=-1;
    if(!f || fread(head,1,16,f)!=16 || memcmp(head,"KIRVUI01",8)) return -1;
    tmp=calloc(1,sizeof(*tmp)); if(!tmp) return -1;
    tmp->resume=head[8]; tmp->autoplay=head[9]; tmp->seek_index=head[10]; tmp->hide_index=head[11];
    tmp->subtitles=head[12]; tmp->fill=head[13]; tmp->volume=head[14]; tmp->count=head[15];
    if(!options_valid(tmp)) goto done;
    for(size_t i=0;i<tmp->count;i++) {
        VmpProgress *p=&tmp->items[i]; int a=fgetc(f), b=fgetc(f), watched; unsigned n;
        if(a==EOF || b==EOF) goto done;
        n=(unsigned)a|((unsigned)b<<8);
        if(!n || n>=VMP_PATH_CAP || fread(p->path,1,n,f)!=n || memchr(p->path,0,n)) goto done;
        p->path[n]=0;
        if(read_u64(f,&p->position) || read_u64(f,&p->duration)) goto done;
        watched=fgetc(f); if(watched==EOF) goto done; p->watched=(unsigned)watched;
        if(!progress_valid(p)) goto done;
        for(size_t j=0;j<i;j++) if(!strcmp(p->path,tmp->items[j].path)) goto done;
    }
    if(fgetc(f)!=EOF || ferror(f)) goto done;
    *s=*tmp; result=0;
done:
    free(tmp); return result;
}
