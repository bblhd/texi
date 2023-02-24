#ifndef SYNTAX_H
#define SYNTAX_H

#include "ctheme.h"

enum SourceFiletype {
	F_Plaintext, F_C, F_None
};

void syntax_init(enum SourceFiletype);
void syntax_move(char *start, size_t where);
void syntax_clear();
void syntax_step(char *string);
color_t syntax_fg();
color_t syntax_bg();

#endif