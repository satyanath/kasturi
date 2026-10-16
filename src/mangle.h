/* mangle.h - maps Kannada identifiers to unique ASCII C identifiers */
#ifndef KASTURI_MANGLE_H
#define KASTURI_MANGLE_H

/* prefix: "k_" for variables and functions, "ks_" for structs,
 * "f_" for struct fields. The same (prefix, name) always gives the same
 * result and different names never collide. */
const char *mangle(const char *prefix, const char *name);

/* Plain transliteration, e.g. "ಮುಖ್ಯ" -> "mukhya". */
char *transliterate(const char *name);

#endif
