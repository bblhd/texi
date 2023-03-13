#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_keysyms.h>

#include "ctheme.h"
#include "clipboard.h"
#include "syntax.h"

#define XK_MISCELLANY
#define XK_LATIN1
#include <X11/keysymdef.h>

typedef _Bool bool;
#define TRUE 1
#define FALSE 0

#define DOC_SIZE_STEP_SIZE 4096

xcb_connection_t *connection;
xcb_gcontext_t graphics;
xcb_window_t root;
xcb_drawable_t window;
xcb_key_symbols_t *keySymbols;
xcb_atom_t wm_delete_window_atom;

char *documentPath = NULL;
size_t documentAllocatedSize = 0;
size_t documentCachedLength = 0;
char *document = NULL;
size_t scroll = 0;
int cachedScrollLine = 0;
int cursor = 0;
int selected = 0;

color_t defaultFG;
color_t defaultBG;
color_t edgeColour;
color_t selectedFG;
color_t selectedBG;

bool hasSetColorAtLeastOnce = FALSE;
color_t oldFG;
color_t oldBG;

struct {
	uint16_t width, height;
} dimensions;

uint16_t linegutter = 0;
uint16_t lineoffset = 0;
uint16_t lineheight = 0;

struct AsciiEntry {
	uint16_t advance;
	xcb_char2b_t string[7];
	uint8_t length;
} ascii[0x81];

void die(char *msg);

void loadDocument(char *path);
void loadDocumentFromString(char *string);

void loadFont(char *fontname);

void handleButtonPress(xcb_button_press_event_t *event);
void handleButtonRelease(xcb_button_release_event_t *event);
void handleKeypress(xcb_key_press_event_t *event);

void drawNumbers(char *text, int line);
void drawText(char *text, int cursor, int selected);
size_t lengthOfDisplayedText(char *text);
size_t getMouseOnText(char *text, uint16_t mx, uint16_t my);

bool shouldFrameUpdate();
void clear();
void getDimensions();
void setColor(uint32_t fg, uint32_t bg);

int main(int argc, char **argv) {
	connection = xcb_connect(NULL, NULL);
	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
	root = screen->root;
	
	xcb_intern_atom_reply_t* atom_reply = xcb_intern_atom_reply(connection, xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW"), 0);
	wm_delete_window_atom = atom_reply->atom;
	free(atom_reply);
	
	ctheme_clear();
	if (!ctheme_load(NULL)) {
		ctheme_set(COLORSCHEME_DEFAULT, 1, 0x000000, BGR);
		ctheme_set(COLORSCHEME_DEFAULT, 2, 0xffffff, BGR);
		ctheme_set(COLORSCHEME_SELECTED, 1, 0x000000, BGR);
		ctheme_set(COLORSCHEME_SELECTED, 2, 0xabffab, BGR);
		
		ctheme_set(COLORSCHEME_STRINGS, 1, 0x000000, BGR);
		ctheme_set(COLORSCHEME_STRINGS, 2, 0xffffaa, BGR);
		ctheme_set(COLORSCHEME_NUMBERS, 1, 0x000000, BGR);
		ctheme_set(COLORSCHEME_NUMBERS, 2, 0xbbffff, BGR);
		ctheme_set(COLORSCHEME_COMMENTS, 1, 0x000000, BGR);
		ctheme_set(COLORSCHEME_COMMENTS, 2, 0xdddddd, BGR);
		ctheme_set(COLORSCHEME_OPERATORS, 1, 0xababab, BGR);
		ctheme_set(COLORSCHEME_OPERATORS, 2, 0xffffff, BGR);
		ctheme_set(COLORSCHEME_CLASSES, 1, 0x0000ff, BGR);
		ctheme_set(COLORSCHEME_CLASSES, 2, 0xffffff, BGR);
		ctheme_set(COLORSCHEME_KEYWORDS, 1, 0xff0000, BGR);
		ctheme_set(COLORSCHEME_KEYWORDS, 2, 0xffffff, BGR);
		ctheme_set(COLORSCHEME_SPECIAL, 1, 0xff0000, BGR);
		ctheme_set(COLORSCHEME_SPECIAL, 2, 0xffffff, BGR);
	}
	defaultFG  = ctheme_get(COLORSCHEME_DEFAULT, 1, BGR);
	defaultBG  = ctheme_get(COLORSCHEME_DEFAULT, 2, BGR);
	edgeColour = ctheme_get(COLORSCHEME_DEFAULT, 3, BGR);
	selectedFG = ctheme_get(COLORSCHEME_SELECTED, 1, BGR);
	selectedBG = ctheme_get(COLORSCHEME_SELECTED, 2, BGR);
	
	graphics = xcb_generate_id(connection);
	xcb_create_gc(
		connection, graphics, screen->root,
		XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_GRAPHICS_EXPOSURES,
		(uint32_t[]) {defaultFG, defaultBG, 1}
	);
	//loadFont("-xos4-terminus-bold-r-normal--14-140-72-72-c-80-iso10646-1");
	//loadFont("-xos4-terminus-medium-r-normal--14-140-72-72-c-80-iso10646-1");
	//loadFont("-xos4-terminus-medium-r-normal--12-120-72-72-c-60-iso10646-1");
	//loadFont("-xos4-terminus-medium-r-normal--12-120-72-72-c-60-iso10646-1");
	//loadFont("-adobe-helvetica-bold-r-normal--12-120-72-72-c-60-iso10646-1");
	loadFont("-*-lucida-bold-r-normal-sans-14-140-*-*-*-*-iso10646-*");
	//loadFont("-b&h-lucida-medium-r-normal-sans-17-120-100-100-p-96-iso10646-1");
	
	window = xcb_generate_id(connection);
	xcb_create_window(
		connection, XCB_COPY_FROM_PARENT,
		window, screen->root,
		0, 0, 150, 150, 10,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
		XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
		(uint32_t[]) {
			defaultBG,
			XCB_EVENT_MASK_EXPOSURE
			| XCB_EVENT_MASK_KEY_PRESS
			| XCB_EVENT_MASK_BUTTON_PRESS
			| XCB_EVENT_MASK_BUTTON_RELEASE
			
		}
	);
	xcb_map_window(connection, window);
	
	if (argc > 1) documentPath = argv[1];
	if (documentPath) loadDocument(documentPath);
	else loadDocumentFromString("This is a scratch document, it isn't from a file, and thus will not be saved.");
	
	char *windowTitle = documentPath ? documentPath : "scratch";
	xcb_change_property (
		connection, XCB_PROP_MODE_REPLACE, window,
		XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
		strlen(windowTitle), windowTitle
	);
	
	clipboard_init(connection, window, "TEXI_CLIPBOARD");
	keySymbols = xcb_key_symbols_alloc(connection);
	xcb_flush(connection);
	
	bool dontExit = 1;
	xcb_generic_event_t *event;
	while (dontExit && (event = xcb_wait_for_event(connection))) {
		getDimensions();
		switch (event->response_type & ~0x80) {
			case XCB_EXPOSE:
			break;
			
			case XCB_CLIENT_MESSAGE:
			if (((xcb_client_message_event_t *) event)->data.data32[0] == wm_delete_window_atom) {
				dontExit = FALSE;
			}
			break;
			
			case XCB_KEY_PRESS:
			handleKeypress((xcb_key_press_event_t *) event);
			break;
			
			case XCB_BUTTON_PRESS:
			handleButtonPress((xcb_button_press_event_t *) event);
			break;
			
			case XCB_BUTTON_RELEASE:
			handleButtonRelease((xcb_button_release_event_t *) event);
			break;
			
			case XCB_SELECTION_REQUEST:
			clipboard_selectionRequest((xcb_selection_request_event_t *) event);
			break;
		}
		free(event);
		xcb_flush(connection);
		if (shouldFrameUpdate()) {
			clear();
			syntax_move(document, scroll);
			drawText(document+scroll, cursor-scroll, selected-scroll);
			drawNumbers(document+scroll, cachedScrollLine+1);
		}
		xcb_flush(connection);
	}
	
	free(document);
	
	xcb_key_symbols_free(keySymbols);
	
	xcb_destroy_window(connection, window);
	xcb_flush(connection);
	xcb_disconnect(connection);

	return 0;
}

long lasttime = 0;
bool shouldFrameUpdate() {
	struct timeval time;
	gettimeofday(&time, NULL);
	long currenttime = time.tv_sec * 1000 + time.tv_usec / 1000;
	
	bool yes = lasttime == 0 || currenttime >= lasttime + 1000/45;
	if (yes) lasttime = currenttime;
	return yes;
}

void setAscii(unsigned char c, size_t n, ...) {
	int g = c;
	if (g < 0) g = 0x80;
	ascii[g].length = n;
	va_list parts;
	va_start(parts, n);
	for (size_t i = 0; i < n && i < 7; i++) {
		int part = va_arg(parts, int);
		ascii[g].string[i] = (xcb_char2b_t) {(part>>8)&0xff, part&0xff};
	}
	va_end(parts);
}

void loadFont(char *fontname) {
	setAscii(0x80, 1, 0xa4);
	for (char c = 0; c < 0x20; c++) setAscii(c, 1, 0xa4);
	for (char c = 0x20; c < 0x7f; c++) setAscii(c, 1, c);
	setAscii('\0', 1, ' ');
	setAscii('\n', 2, ' ', ' ');
	setAscii('\t', 3, ' ', ' ', ' ');

	xcb_font_t font = xcb_generate_id(connection);
	xcb_open_font(connection, font, strlen(fontname), fontname);
    
	xcb_change_gc(connection, graphics, XCB_GC_FONT, (uint32_t[]) {font});
	
	xcb_query_text_extents_cookie_t advancesCookies[0x81];
	for (char c = 0; c >= 0; c++) {
		advancesCookies[c] = xcb_query_text_extents(connection, font, ascii[c].length, ascii[c].string);
	}
	
	for (char c = 0; c >= 0; c++) {
		xcb_query_text_extents_reply_t *reply = xcb_query_text_extents_reply(connection, advancesCookies[c], NULL);
		ascii[c].advance = reply->overall_width;
		if (lineoffset < reply->font_ascent) {
			lineoffset = reply->font_ascent;
		}
		if (lineheight < reply->font_ascent + reply->font_descent) {
			lineheight = reply->font_ascent + reply->font_descent;
		}
		free(reply);
	}
	xcb_close_font(connection, font);
	
	ascii['\t'].advance = 32;
};

void clear() {
	xcb_clear_area(connection, 0, window, 0, 0, 0, 0);
}

void glyph(char c, uint16_t x, uint16_t y) {
	unsigned char g = (unsigned char) c;
	if (c < 0) g = 0x80;
	xcb_image_text_16(connection, ascii[g].length, window, graphics, x, lineoffset+y, ascii[g].string);
}

uint16_t advance(char c) {
	unsigned char g = (unsigned char) c;
	if (c < 0) g = 0x80;
	return ascii[g].advance;
}

uint16_t getNumGap() {
	return 5 * advance('8');
}

void allocateDocument(size_t newSize) {
	if (newSize > documentAllocatedSize) {
		newSize = (newSize + (DOC_SIZE_STEP_SIZE - 1)) / DOC_SIZE_STEP_SIZE * DOC_SIZE_STEP_SIZE;
		char *newDocument = realloc(document, newSize);
		if (newDocument) {
			document = newDocument;
			documentAllocatedSize = newSize;
		}
	}
}

enum SourceFiletype getSourceTypeOfFile(char *path) {
	size_t length = strlen(path);
	if (length >= 2 && path[length-2] == '.' && (path[length-1] == 'c' || path[length-1] == 'h')) {
		return F_C;
	} else return F_Plaintext;
}

void loadDocument(char *path) {
	syntax_init(getSourceTypeOfFile(path));
	FILE *file = fopen(path, "rb");
	if (file) {
		fseek(file, 0, SEEK_END);
		documentCachedLength = ftell(file);
		if (document) {
			free(document);
			documentAllocatedSize = 0;
		}
		allocateDocument(documentCachedLength+1);
		if (documentCachedLength > 0) {
			rewind(file);
			fread(document, documentCachedLength, 1, file);
			document[documentCachedLength] = 0;
		}
		fclose(file);
	} else {
		documentCachedLength = 0;
		allocateDocument(documentCachedLength+1);
		document[documentCachedLength] = 0;
	}
}

void loadDocumentFromString(char *string) {
	syntax_init(F_Plaintext);
	documentCachedLength = strlen(string);
	allocateDocument(documentCachedLength+1);
	strcpy(document, string);
}

void saveDocument(char *path) {
	FILE *file = fopen(path, "w");
	if (file) {
		if (documentCachedLength > 0) {
			fwrite(document, documentCachedLength, 1, file);
		}
		fclose(file);
	}
}

int document_previousLine(int c);
int document_nextLine(int c);

void scrollup();
void scrolldown();

void handleButtonPress(xcb_button_press_event_t *event) {
	if (event->detail == 1) {
		cursor = scroll + getMouseOnText(document + scroll, event->event_x, event->event_y);
		selected = cursor;
	} else if (event->detail == 5) {
		scrolldown();
		scrolldown();
	} else if (event->detail == 4) {
		scrollup();
		scrollup();
	}
}

void handleButtonRelease(xcb_button_release_event_t *event) {
	if (event->detail == 1) {
		selected = scroll + getMouseOnText(document + scroll, event->event_x, event->event_y);
	}
}

xcb_keysym_t getKeysym(xcb_keycode_t keycode);
char asciiupper(char c);

int documentInsert(size_t where, size_t size, char *what);
int documentInsertChar(size_t where, char what);
int documentDelete(size_t where, size_t size);
int documentDeleteSelection();

int offsetUp(int c);
int offsetDown(int c);

void pasteWholeBuffer() {
	char buffer[1024];
	size_t length=0, offset=0;
	do {
		length = clipboard_get(buffer, 1024, offset);
		offset += length;
		selected = cursor = documentDeleteSelection();
		if (length > strlen(buffer)) length = strlen(buffer);
		if (length > 0) {
			selected = cursor = documentInsert(cursor, length, buffer);
		}
	} while (length == 1024);
}

void handleKeypress(xcb_key_press_event_t *event) {
	xcb_keysym_t keysym = getKeysym(event->detail);
	
	if (event->state & XCB_MOD_MASK_CONTROL) {
		if (keysym == XK_a) {
			cursor = 0;
			selected = documentCachedLength;
		} else if (keysym == XK_c) {
			clipboard_set(document+(cursor<selected ? cursor : selected), selected>cursor ? selected-cursor : cursor-selected);
		} else if (keysym == XK_v) {
			pasteWholeBuffer();
		} else if (keysym == XK_x) {
			clipboard_set(document+(cursor<selected ? cursor : selected), selected>cursor ? selected-cursor : cursor-selected);
			documentDeleteSelection();
		} else if (keysym == XK_s) {
			if (documentPath) saveDocument(documentPath);
		} else if (keysym == XK_r) {
			//if (documentPath) loadDocument(documentPath);
		}
	} else if (keysym == XK_Up) {
		if (event->state & XCB_MOD_MASK_SHIFT) {
			selected = offsetUp(selected);
		} else {
			selected = cursor = offsetUp(cursor);
		}
	} else if (keysym == XK_Down) {
		if (event->state & XCB_MOD_MASK_SHIFT) {
			selected = offsetDown(selected);
		} else {
			selected = cursor = offsetDown(cursor);
		}
	} else if (keysym == XK_Left) {
		if (event->state & XCB_MOD_MASK_SHIFT) {
			if (selected > 0) selected--;
		} else {
			if (cursor > 0) cursor--;
			selected = cursor;
		}
	} else if (keysym == XK_Right) {
		if (event->state & XCB_MOD_MASK_SHIFT) {
			if (document[selected]) selected++;
		} else {
		if (document[cursor]) cursor++;
			selected = cursor;
		}
	} else if (keysym == XK_BackSpace) {
		if (cursor == selected) cursor=documentDelete(cursor, 1);
		else documentDeleteSelection();
		selected = cursor;
	} else if (keysym == XK_Return) {
		int lineStart = cursor;
		int indentLength = 0;
		do {lineStart--;} while (
			lineStart >= 0
			&& document[lineStart] != '\n'
		);
		lineStart++;
		while (document[lineStart+indentLength] == '\t' || document[lineStart+indentLength] == ' ') indentLength++;
		cursor=documentInsert(documentInsertChar(documentDeleteSelection(), '\n'), indentLength, document+lineStart);
		selected = cursor;
	} else if (keysym == XK_Tab) {
		cursor=documentInsertChar(documentDeleteSelection(), '\t');
		selected = cursor;
	} else if (keysym >= XK_space && keysym <= XK_asciitilde) {
		cursor=documentInsertChar(documentDeleteSelection(), event->state & (XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_LOCK) ? asciiupper(keysym) : (char) keysym);
		selected = cursor;
	}
}

void setColor(uint32_t fg, uint32_t bg);

void drawnum(int n, uint16_t x, uint16_t y) {
	char buffer[12];
	snprintf(buffer, 12, "%i", n);
	for (int i = 0; buffer[i]; i++) {
		glyph(buffer[i], x, y);
		x += advance(buffer[i]);
	}
}

void advanceByOneWrappedLine(char **text, uint16_t *y) {
	uint16_t x = linegutter;
	*y += lineheight;
	
	while (**text && **text != '\n') {
		uint16_t dx = advance(**text);
		if (x+dx >= dimensions.width) {
			x = linegutter;
			*y += lineheight;
		}
		x += dx;
		(*text)++;
	}
}

int drawLineNumber(int line, char **text, uint16_t *y) {
	drawnum(line++, 0, *y);
	advanceByOneWrappedLine(text, y);
	return *y+lineheight < dimensions.height && **text ? line : 0;
}

void drawNumbers(char *text, int line) {
	linegutter = getNumGap();
	uint16_t y = 0;
	setColor(defaultFG, defaultBG);
	while ((line = drawLineNumber(line, &text, &y))) {
		if (*text == '\n') text++;
	}
}

void drawCursor(uint16_t x, uint16_t y) {
	setColor(edgeColour, edgeColour);
	xcb_poly_line(
		connection, 0, window, graphics, 2,
		(const xcb_point_t[]) {{x, y}, {x, y+lineheight}}
	);
	setColor(syntax_fg(),syntax_bg());
}

void drawText(char *text, int cursor, int selected) {
	if (selected < cursor) {
		int temp = cursor;
		cursor = selected;
		selected = temp;
	}
	linegutter = getNumGap();
	
	setColor(edgeColour, edgeColour);
	xcb_poly_line(
		connection, 0, window, graphics, 2,
		(const xcb_point_t[]) {{linegutter-4, 0}, {linegutter-4, dimensions.height}}
	);
	
	if (cursor < 0 && selected > 0) setColor(selectedFG, selectedBG);
	
	syntax_clear();
	
	uint16_t x=linegutter, y=0;
	int n;
	for (n = 0; text[n] && y+lineheight < dimensions.height; n++) {
		syntax_step(text+n);
		
		if (n == cursor && selected > cursor) setColor(selectedFG, selectedBG);
		if (n < cursor || n == selected) setColor(syntax_fg(),syntax_bg());
		
		uint16_t dx = advance(text[n]);
		if (x+dx >= dimensions.width) {
			x = linegutter;
			y += lineheight;
		}
		glyph(text[n], x, y);
		if (n == cursor && cursor == selected) drawCursor(x,y);
		if (text[n] == '\n') {
			x = linegutter;
			y += lineheight;
		} else {
			x += dx;
		}
	}
	setColor(defaultFG, defaultBG);
	if (y+lineheight < dimensions.height) {
		glyph(text[n], x, y);
		if (n == cursor) drawCursor(x,y);
	}
}

size_t lengthOfDisplayedText(char *text) {
	linegutter = getNumGap();
	uint16_t x=linegutter, y=0;
	size_t n;
	for (n = 0; text[n] && y+lineheight < dimensions.height; n++) {
		uint16_t dx = advance(text[n]);
		if (x+dx >= dimensions.width) {
			x = linegutter;
			y += lineheight;
		}
		if (text[n] == '\n') {
			x = linegutter;
			y += lineheight;
		} else {
			x += dx;
		}
	}
	return n;
}

size_t getMouseOnText(char *text, uint16_t mx, uint16_t my) {
	linegutter = getNumGap();
	uint16_t x=linegutter, y=0;
	if (mx<x && my>=y && my<y+lineheight) {
		return 0;
	}
	size_t n;
	for (n = 0; text[n] && y+lineheight < dimensions.height; n++) {
		uint16_t dx = advance(text[n]);
		if (x+dx >= dimensions.width) {
			if (mx>=x && my>=y && my<y+lineheight) {
				return n;
			}
			x = linegutter;
			y += lineheight;
			if (mx<x && my>=y && my<y+lineheight) {
				return n;
			}
		}
		if (text[n] == '\n') {
			if (mx>=x && my>=y && my<y+lineheight) {
				return n;
			}
			x = linegutter;
			y += lineheight;
			if (mx<x && my>=y && my<y+lineheight) {
				return n+1;
			}
		} else {
			if (mx>=x && mx<x+dx && my>=y && my<y+lineheight) {
				return n;
			}
			x += dx;
		}
	}
	return n;
}

xcb_keysym_t getKeysym(xcb_keycode_t keycode) {
	return keySymbols ? xcb_key_symbols_get_keysym(keySymbols, keycode, 0) : 0;
}

char asciiupper(char c) {
	//this is only valid for 'us' keyboard layout, change if needed
	if (c >= 'a' && c <= 'z') return c-'a'+'A';
	else if (c >= '0' && c <= '9') return ")!@#$%^&*("[c-'0'];
	else switch (c) {
		case '`': return '~';
		case '-': return '_';
		case '=': return '+';
		case '[': return '{';
		case ']': return '}';
		case '\\': return '|';
		case ';': return ':';
		case '\'': return '"';
		case ',': return '<';
		case '.': return '>';
		case '/': return '?';
		default: return c;
	}
}

uint16_t document_findXPosOf(int c) {
	uint16_t x = linegutter;
	while (c>0 && document[--c] != '\n') {
		x+=advance(document[c]);
	}
	return x;
}

uint16_t document_forwardWidth(int c, uint16_t s) {
	uint16_t x = linegutter;
	uint16_t dx;
	while (x + ((dx=advance(document[c]))/2) < s && document[c] && document[c] != '\n') {
		x+=dx;
		c++;
	}
	return c;
}

int document_previousLine(int c) {
	if (c>0) do c--; while (c>0 && document[c] != '\n');
	while (c > 0 && document[c-1] != '\n') c--;
	return c;
}

int document_nextLine(int c) {
	while (c>0 && document[c-1] != '\n') c--;
	int start = c;
	while (document[c] && document[c] != '\n') c++;
	if (document[c]) return c+1;
	else return start;
}

int offsetUp(int c) {
	return document_forwardWidth(document_previousLine(c), document_findXPosOf(c));
}

int offsetDown(int c) {
	return document_forwardWidth(document_nextLine(c), document_findXPosOf(c));
}

void scrollup() {
	size_t c = document_previousLine(scroll);
	if (scroll != c) cachedScrollLine--;
	scroll = c;
}

void scrolldown() {
	size_t c = document_nextLine(scroll);
	if (scroll != c) cachedScrollLine++;
	scroll = c;
}

int documentInsert(size_t where, size_t size, char *what) {
	allocateDocument(documentCachedLength+size+1);
	if (documentCachedLength+size+1 <= documentAllocatedSize) {
		memmove(document+where+size, document+where, strlen(document+cursor)+1);
		memcpy(document+where, what, size);
		where+=size;
		documentCachedLength+=size;
	}
	return where;
}

int documentInsertChar(size_t where, char what) {
	return documentInsert(where, 1, &what);
}

int documentDelete(size_t where, size_t size) {
	size = size > where ? where : size;
	
	int offset = 0;
	for (char *s = document+where-size; s < document+where && s < document+scroll; s++) {
		if (*s == '\n') cachedScrollLine--;
		offset++;
	}
	scroll-=offset;
	if (size > 0) {
		memmove(document+where-size, document+where, strlen(document+cursor)+1);
		where-=size;
		documentCachedLength-=size;
	}
	while (scroll>0 && document[scroll-1]!='\n') scroll--;
	return where;
}

int documentDeleteSelection() {
	cursor = documentDelete(selected>cursor ? selected : cursor, selected>cursor ? selected-cursor : cursor-selected);
	selected = cursor;
	return cursor;
}

void getDimensions() {
	xcb_get_geometry_reply_t* geom = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, window), 0);
	dimensions.width = geom->width;
	dimensions.height = geom->height;
	free(geom);
}

void setColor(uint32_t fg, uint32_t bg) {
	if (!hasSetColorAtLeastOnce || (bg != oldBG && fg != oldFG)) {
		xcb_change_gc(connection, graphics, XCB_GC_FOREGROUND | XCB_GC_BACKGROUND, (uint32_t[]) {fg, bg});
	} else if (bg != oldBG) {
		xcb_change_gc(connection, graphics, XCB_GC_BACKGROUND, (uint32_t[]) {bg});
	} else if (fg != oldFG) {
		xcb_change_gc(connection, graphics, XCB_GC_FOREGROUND, (uint32_t[]) {fg});
	}
	oldBG = bg;
	oldFG = fg;
	hasSetColorAtLeastOnce = TRUE;
}

void die(char *msg) {
	fprintf(stderr, "ERROR: %s", msg);
	exit(EXIT_FAILURE);
}
