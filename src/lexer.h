/* lexer.h - tokens of the Kasturi language */
#ifndef KASTURI_LEXER_H
#define KASTURI_LEXER_H

#include <stdbool.h>

#include "diag.h"

typedef enum TokKind {
    T_EOF,
    T_ID,
    T_INT,
    T_FLOAT,
    T_STR,
    T_CHAR,

    /* type keywords */
    K_INT,    /* ಸಂಖ್ಯೆ */
    K_LONG,   /* ದೀರ್ಘ */
    K_DOUBLE, /* ದಶಾಂಶ */
    K_CHAR,   /* ಅಕ್ಷರ */
    K_STRING, /* ಪದ */
    K_BOOL,   /* ತಾರ್ಕಿಕ */
    K_VOID,   /* ಶೂನ್ಯ */
    K_VALIST, /* ಚರಪಟ್ಟಿ */

    /* declarations */
    K_STRUCT,  /* ರಚನೆ */
    K_FUNC,    /* ಕ್ರಿಯೆ */
    K_RETURNS, /* ಫಲಿತಾಂಶ */
    K_CONST,   /* ಸ್ಥಿರ */
    K_EXTERN,  /* ಬಾಹ್ಯ */
    K_IMPORT,  /* ಆಮದು */

    /* statements */
    K_IF,       /* ಆದರೆ */
    K_ELSE,     /* ಇಲ್ಲವಾದರೆ */
    K_WHILE,    /* ಸಮಯವರೆಗೆ */
    K_DO,       /* ಮಾಡು */
    K_FOR,      /* ಎಣಿಕೆ */
    K_BREAK,    /* ನಿಲ್ಲಿಸು */
    K_CONTINUE, /* ಮುಂದುವರಿಸು */
    K_RETURN,   /* ಮರಳಿ */
    K_SWITCH,   /* ಆಯ್ಕೆ */
    K_CASE,     /* ಸಂದರ್ಭ */
    K_DEFAULT,  /* ಪೂರ್ವನಿಯೋಜಿತ */
    K_PRINT,    /* ಮುದ್ರಿಸು */
    K_PRINTLN,  /* ಸಾಲುಮುದ್ರಿಸು */
    K_READ,     /* ಓದು */
    K_TO,       /* ಗೆ */
    K_SET,      /* ನಿಗದಿ */

    /* word operators */
    K_OR,  /* ಅಥವಾ */
    K_AND, /* ಮತ್ತು */
    K_NOT, /* ಅಲ್ಲ */
    K_EQ,  /* ಸಮಾನ */
    K_NE,  /* ಸಮಾನವಲ್ಲ */
    K_GT,  /* ಹೆಚ್ಚು */
    K_LT,  /* ಕಡಿಮೆ */
    K_GE,  /* ಹೆಚ್ಚಾಗಲಿ */
    K_LE,  /* ಕಡಿಮೆಯಾಗಲಿ */
    K_ADD, /* ಸೇರಿಸು */
    K_SUB, /* ಕಡಿತ */
    K_MUL, /* ಗುಣಿಸು */
    K_DIV, /* ಭಾಗಿಸು */
    K_MOD, /* ಶೇಷ */

    /* other expression keywords */
    K_SIZEOF, /* ಗಾತ್ರ */
    K_TRUE,   /* ಸತ್ಯ */
    K_FALSE,  /* ಅಸತ್ಯ */
    K_NULL,   /* ಖಾಲಿ */
    K_VA_START, /* ಚರ_ಆರಂಭ */
    K_VA_ARG,   /* ಚರ_ಮೌಲ್ಯ */
    K_VA_END,   /* ಚರ_ಅಂತ್ಯ */
    K_VA_COPY,  /* ಚರ_ನಕಲು */

    /* punctuation */
    P_LPAREN, P_RPAREN, P_LBRACE, P_RBRACE, P_LBRACKET, P_RBRACKET,
    P_SEMI, P_COMMA, P_DOT, P_ARROW, P_ELLIPSIS, P_COLON, P_QUESTION,
    P_PLUS, P_MINUS, P_STAR, P_SLASH, P_PERCENT, P_AMP, P_PIPE, P_CARET,
    P_TILDE, P_BANG, P_ASSIGN, P_EQ, P_NE, P_LT, P_GT, P_LE, P_GE,
    P_ANDAND, P_OROR, P_SHL, P_SHR, P_INC, P_DEC,
    P_PLUSEQ, P_MINUSEQ, P_STAREQ, P_SLASHEQ, P_PERCENTEQ,
    P_AMPEQ, P_PIPEEQ, P_CARETEQ, P_SHLEQ, P_SHREQ,

    TOK_COUNT
} TokKind;

typedef struct Token {
    TokKind kind;
    Loc loc;
    char *text;      /* T_ID: name, T_STR: decoded bytes, T_FLOAT: ASCII spelling */
    size_t len;      /* length of text (strings may contain NUL bytes) */
    long long ival;  /* T_INT, T_CHAR */
    bool wide;       /* T_CHAR holding a non-ASCII code point */
} Token;

/* Splits a source file into tokens. The array ends with a T_EOF token. */
Token *lex_file(SrcFile *file, int *count);

/* Spelling of a token kind for messages. */
const char *tok_spelling(TokKind k);

#endif
