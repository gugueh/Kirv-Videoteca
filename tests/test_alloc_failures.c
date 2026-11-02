/* Copyright (c) 2025-2026 gugueh (Kirv Studio). All rights reserved. */
/* Compile the actual portable implementation with an injected realloc failure.
 * The production binary is compiled normally and never includes this hook. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned calls, fail_on;
static void *fault_realloc(void *ptr, size_t bytes)
{
    if (++calls == fail_on) return NULL;
    return realloc(ptr, bytes);
}
#define realloc fault_realloc
#include "../src/core.c"
#undef realloc

int main(void)
{
    unsigned iterations = 0;
    for (fail_on = 1; fail_on <= 6; ++fail_on) {
        FILE *file = tmpfile();
        VmpSubtitles subtitles = {0};
        int result;
        assert(file);
        for (unsigned i = 0; i < 200; ++i) {
            unsigned a = i*2, b = a+1;
            fprintf(file,"00:%02u:%02u,000 --> 00:%02u:%02u,000\ntext\n\n",a/60,a%60,b/60,b%60);
        }
        rewind(file); calls = 0;
        result = vmp_srt_read(file, &subtitles);
        if (fail_on <= 5) assert(result < 0 && !subtitles.cues && !subtitles.count);
        else assert(!result && subtitles.count == 200);
        vmp_srt_free(&subtitles);
        assert(!fclose(file));
        ++iterations;
    }
    printf("PASS: %u allocation-failure scenarios.\n",iterations);
    return 0;
}
