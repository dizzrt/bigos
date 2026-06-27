/* BigOS user libc: ASCII/C-locale-style ctype helpers. */
#include "libc.h"

static int as_uchar(int c) {
    return c & 0xff;
}

int isdigit(int c) {
    c = as_uchar(c);
    return c >= '0' && c <= '9';
}

int isupper(int c) {
    c = as_uchar(c);
    return c >= 'A' && c <= 'Z';
}

int islower(int c) {
    c = as_uchar(c);
    return c >= 'a' && c <= 'z';
}

int isalpha(int c) {
    return isupper(c) || islower(c);
}

int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

int isspace(int c) {
    c = as_uchar(c);
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

int isprint(int c) {
    c = as_uchar(c);
    return c >= 0x20 && c <= 0x7e;
}

int isxdigit(int c) {
    c = as_uchar(c);
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int iscntrl(int c) {
    c = as_uchar(c);
    return c < 0x20 || c == 0x7f;
}

int isgraph(int c) {
    c = as_uchar(c);
    return c > 0x20 && c < 0x7f;
}

int ispunct(int c) {
    c = as_uchar(c);
    return isgraph(c) && !isalnum(c);
}

int isblank(int c) {
    c = as_uchar(c);
    return c == ' ' || c == '\t';
}

int tolower(int c) {
    if (isupper(c))
        return as_uchar(c) - 'A' + 'a';
    return c;
}

int toupper(int c) {
    if (islower(c))
        return as_uchar(c) - 'a' + 'A';
    return c;
}
