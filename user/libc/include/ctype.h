/* BigOS bounded ctype declarations.
 *
 * ASCII/C-locale-style classification and conversion only. No locale,
 * multibyte, Unicode, wide-character, or host libc behavior is provided. */
#ifndef _BIGOS_USER_CTYPE_H
#define _BIGOS_USER_CTYPE_H

int isalnum(int c);
int isalpha(int c);
int isdigit(int c);
int islower(int c);
int isprint(int c);
int isspace(int c);
int isupper(int c);
int tolower(int c);
int toupper(int c);

#endif /* _BIGOS_USER_CTYPE_H */
