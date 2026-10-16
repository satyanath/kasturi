/* util.h - memory, strings, containers, UTF-8 and portable file/process helpers */
#ifndef KASTURI_UTIL_H
#define KASTURI_UTIL_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

void *xmalloc(size_t n);
void *xcalloc(size_t count, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *xformat(const char *fmt, ...);
char *xvformat(const char *fmt, va_list ap);

/* growable string buffer */
typedef struct StrBuf {
    char *data;
    size_t len, cap;
} StrBuf;

void sb_init(StrBuf *sb);
void sb_putc(StrBuf *sb, char c);
void sb_putn(StrBuf *sb, const char *s, size_t n);
void sb_puts(StrBuf *sb, const char *s);
void sb_printf(StrBuf *sb, const char *fmt, ...);
void sb_free(StrBuf *sb);

/* growable vector of pointers */
typedef struct Vec {
    void **items;
    int len, cap;
} Vec;

void vec_push(Vec *v, void *item);

/* string -> pointer hash map */
typedef struct StrMap {
    char **keys;
    void **vals;
    size_t cap, count;
} StrMap;

void *map_get(const StrMap *m, const char *key);
void map_put(StrMap *m, const char *key, void *val);

/* UTF-8 */
int utf8_decode(const char *s, size_t len, unsigned *cp); /* returns bytes used (>= 1) */
int utf8_encode(unsigned cp, char *out);                 /* returns bytes written (1..4) */
bool str_is_ascii(const char *s);

/* files and paths (all paths are UTF-8) */
char *read_file(const char *path, size_t *len);
bool write_file(const char *path, const char *data, size_t len);
bool file_exists(const char *path);
bool dir_exists(const char *path);
bool remove_file(const char *path);
bool move_file(const char *from, const char *to);
bool make_dir(const char *path);
bool remove_dir(const char *path);
char *path_dirname(const char *path);
char *path_basename(const char *path);
char *path_strip_ext(const char *path);
char *path_join(const char *a, const char *b);
char *path_absolute(const char *path);
bool path_is_absolute(const char *path);
bool str_ends_with(const char *s, const char *suffix);
char *ascii_safe_path(const char *path); /* 8.3 short path on Windows when needed */
char *temp_directory(void);
char *exe_path(const char *argv0);
char *find_in_path(const char *program);
char *get_env(const char *name);
int current_pid(void);

/* processes */
int run_command(const char *cmd);              /* runs through the shell */
char *capture_command(const char *cmd);         /* first line of stdout or NULL */

/* console output that renders UTF-8 correctly on every platform */
void con_write(FILE *f, const char *s, size_t n);
void con_printf(FILE *f, const char *fmt, ...);

/* command line arguments as UTF-8 */
char **utf8_args(int argc, char **argv, int *out_argc);

#endif
