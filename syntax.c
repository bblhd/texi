#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "ctheme.h"
#include "syntax.h"

typedef _Bool bool;
#define TRUE 1
#define FALSE 0

enum SourceFiletype sourcetype = F_None;

color_t retainedFG, retainedBG;
size_t retainedLength = 0;

color_t fg, bg;
size_t remainingLengthOfToken = 0;

void syntax_step_Plaintext();
void syntax_step_C();

void (*syntaxes[F_None])(char *) = {syntax_step_Plaintext, syntax_step_C};

void syntax_init(enum SourceFiletype type) {
	sourcetype = type;
}

void syntax_move(char *start, size_t where) {
	remainingLengthOfToken = 0;
	fg = 0;
	bg = 0;
	for (char *s = start; s < start+where; s++) {
		syntax_step(s);
	}
	retainedFG = fg;
	retainedBG = bg;
	retainedLength = remainingLengthOfToken;
}

void syntax_clear() {
	remainingLengthOfToken = retainedLength;
	fg = retainedFG;
	bg = retainedBG;
}

color_t syntax_fg() {
	return fg;
}

color_t syntax_bg() {
	return bg;
}

void syntax_step(char *string) {
	if (remainingLengthOfToken == 0 && sourcetype != F_None) {
		syntaxes[sourcetype](string);
	}
	if (remainingLengthOfToken > 0) {
		remainingLengthOfToken--;
	}
}

void syntax_span(size_t length, colorscheme_id_t id) {
	remainingLengthOfToken = length;
	fg = ctheme_get(id, 1, BGR);
	bg = ctheme_get(id, 2, BGR);
}

void syntax_step_Plaintext(char *string) {
	(void) string;
	syntax_span((size_t) -1, COLORSCHEME_DEFAULT);
}

const char *ckeywords[] = {
	"break", "case", "continue", "default", "do", "else",
	"extern", "for", "goto", "if", "inline", "register", "restrict", "return",
	"sizeof", "static", "switch", "typedef", "volatile", "while", NULL
};

const char *ctypeNames[] = {
	"auto", "char", "const", "double",  "enum",
	"float", "int", "long","short", "signed",
	"struct", "union", "unsigned", "void", 
	"_Bool", "bool", "uint", NULL
};

bool isCKeyword(char *string, size_t length) {
	for (int i = 0; ckeywords[i]; i++) {
		if (length == strlen(ckeywords[i])) {
			if (strncmp(string, ckeywords[i], length)==0) {
				return TRUE;
			}
		}
	}
	return FALSE;
}

bool isCType(char *string, size_t length) {
	if (length>2 && string[length-2]=='_' && string[length-1]=='t') return TRUE;
	for (int i = 0; ctypeNames[i]; i++) {
		if (length == strlen(ctypeNames[i])) {
			if (strncmp(string, ctypeNames[i], length)==0) {
				return TRUE;
			}
		}
	}
	return FALSE;
}

void syntax_step_C(char *string) {
	if (string[0]=='/' && string[1]=='/') {
		int n = 2;
		while (string[n] && string[n] != '\n') n++;
		
		syntax_span(n, COLORSCHEME_COMMENTS);
	} else if (string[0]=='/' && string[1]=='*') {
		int n = 2;
		while (string[n] && !(string[n] == '*' && string[n+1] == '/')) n++;
		if (string[n]) n+=2;
		
		syntax_span(n, COLORSCHEME_COMMENTS);
	} else if (string[0]=='"') {
		int n = 1;
		while (string[n] && string[n] != '"') {
			if (string[n++] == '\\') n++;
		}
		if (string[n]) n++;
		
		syntax_span(n, COLORSCHEME_STRINGS);
	} else if (string[0]=='\'') {
		int n = 1;
		while (string[n] && string[n] != '\'') {
			if (string[n++] == '\\') n++;
		}
		if (string[n]) n++;
		
		syntax_span(n, COLORSCHEME_STRINGS);
	} else if (string[0] == '#') {
		int n = 1;
		while (isalnum(string[n]) || string[n]=='_') n++;
		syntax_span(n, COLORSCHEME_SPECIAL);
	} else if (isdigit(string[0])) {
		int n = 0;
		if (string[0]=='0' && string[1]=='b') {
			n+=2;
			while (string[n] && string[n]>='0' && string[n]<='1') n++;
		} else if (string[0]=='0' && string[1]=='x') {
			n+=2;
			while (isxdigit(string[n])) n++;
		} else {
			while (isdigit(string[n])) n++;
		}
		syntax_span(n, COLORSCHEME_NUMBERS);
	} else if (isalpha(string[0]) || string[0]=='_') {
		int n = 1;
		while (isalnum(string[n]) || string[n]=='_') n++;
		if (isCKeyword(string, n)) syntax_span(n, COLORSCHEME_KEYWORDS);
		else if (isCType(string, n)) syntax_span(n, COLORSCHEME_CLASSES);
		else syntax_span(n, COLORSCHEME_DEFAULT);
	} else if (ispunct(string[0])) {
		syntax_span(1, COLORSCHEME_OPERATORS);
	} else {
		syntax_span((size_t) 1, COLORSCHEME_DEFAULT);
	}
}
