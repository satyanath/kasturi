/* diag.h - source locations and diagnostics (messages are in Kannada) */
#ifndef KASTURI_DIAG_H
#define KASTURI_DIAG_H

#include <stddef.h>

typedef struct SrcFile {
    char *path;  /* path shown in messages */
    char *text;  /* UTF-8 source text */
    size_t len;
} SrcFile;

typedef struct Loc {
    SrcFile *file;
    int line;
    int col; /* in Unicode code points, 1-based */
} Loc;

void diag_error(Loc loc, const char *fmt, ...);
void diag_warning(Loc loc, const char *fmt, ...);
void diag_note(Loc loc, const char *fmt, ...);
int diag_error_count(void);
void diag_set_warnings(int enabled);
void diag_set_warnings_as_errors(int enabled);
void fatal(const char *fmt, ...);

#endif
