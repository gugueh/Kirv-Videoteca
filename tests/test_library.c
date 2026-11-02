#include "library.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    VmpLibrary a,b,before; unsigned char bytes[4096]; size_t n; FILE *f; char path[512];
    vmp_library_defaults(&a); assert(a.resume && a.subtitles && a.volume==100);
    assert(vmp_seek_seconds(&a)==10 && vmp_hide_seconds(&a)==3);
    assert(!vmp_remember(&a,VMP_ROOT"Series/01.mp4",12345,24000,0));
    assert(!vmp_remember(&a,VMP_ROOT"Series/02.mp4",24000,24000,1));
    assert(!vmp_remember(&a,VMP_ROOT"Series/01.mp4",13000,24000,0));
    assert(a.count==2 && !strcmp(a.items[0].path,VMP_ROOT"Series/01.mp4"));
    f=tmpfile(); assert(f); assert(!vmp_library_write(f,&a)); rewind(f);
    n=fread(bytes,1,sizeof(bytes),f); fclose(f); assert(n>16 && n<sizeof(bytes));
    f=tmpfile(); assert(f); assert(fwrite(bytes,1,n,f)==n); rewind(f);
    assert(!vmp_library_read(f,&b)); fclose(f); assert(!memcmp(&a,&b,sizeof(a)));
    for(size_t length=0;length<n;length++) {
        vmp_library_defaults(&b); before=b; f=tmpfile(); assert(f);
        assert(fwrite(bytes,1,length,f)==length); rewind(f);
        assert(vmp_library_read(f,&b)<0); assert(!memcmp(&b,&before,sizeof(b))); fclose(f);
    }
    for(unsigned offset=8;offset<16;offset++) {
        unsigned char old=bytes[offset]; bytes[offset]=255; f=tmpfile(); assert(f);
        assert(fwrite(bytes,1,n,f)==n); rewind(f); assert(vmp_library_read(f,&b)<0); fclose(f); bytes[offset]=old;
    }
    f=tmpfile(); assert(f); assert(fwrite(bytes,1,n,f)==n); fputc(1,f); rewind(f);
    assert(vmp_library_read(f,&b)<0); fclose(f);
    before=a;
    assert(vmp_remember(&a,"ux0:data/other.mp4",0,0,0)<0);
    assert(vmp_remember(&a,VMP_ROOT"../bad.mp4",0,0,0)<0);
    assert(vmp_remember(&a,VMP_ROOT"x.mp4",50,20,0)<0);
    assert(vmp_remember(&a,VMP_ROOT"x.mp4",UINT64_MAX,0,0)<0);
    assert(!memcmp(&a,&before,sizeof(a)));
    for(unsigned i=0;i<80;i++) { snprintf(path,sizeof(path),VMP_ROOT"%u.mp4",i); assert(!vmp_remember(&a,path,i,100,0)); }
    assert(a.count==VMP_HISTORY && !vmp_progress(&a,VMP_ROOT"0.mp4"));
    assert(vmp_progress(&a,VMP_ROOT"79.mp4")->position==79);
    puts("PASS: history round trip, every truncation, malformed options, trailing data, path boundaries, MRU eviction.");
    return 0;
}
