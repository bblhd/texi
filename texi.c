#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_keysyms.h>

#define XK_MISCELLANY
#define XK_LATIN1
#include <X11/keysymdef.h>

#include "clipboard.h"

#define DARKMODE

typedef struct Document doc_t;

typedef void (*event_handler_t)(xcb_generic_event_t *);

struct Document {
	char *path, *data;
	int length, size;
	int scroll, cursor, selection;
};

void setup();
void cleanup();
void events();
void draw(doc_t *document);

void glyph(char c, int x, int y);
int advance(char c);

void handleClientMessage(xcb_client_message_event_t *event);
void handleButtonPress(xcb_button_press_event_t *event);
void handleButtonRelease(xcb_button_release_event_t *event);
void handleKeyPress(xcb_key_press_event_t *event);

void action_quit(doc_t *);
void action_selectAll(doc_t *);

void action_copy(doc_t *);
void action_paste(doc_t *);
void action_cut(doc_t *);

void action_save(doc_t *);
void action_reload(doc_t *);

void action_selectLeft(doc_t *);
void action_selectRight(doc_t *);
void action_selectUp(doc_t *);
void action_selectDown(doc_t *);

void action_cursorLeft(doc_t *);
void action_cursorRight(doc_t *);
void action_cursorUp(doc_t *);
void action_cursorDown(doc_t *);

void action_backspace(doc_t *);
void action_newline(doc_t *);
void action_tab(doc_t *document);

doc_t *load(doc_t *document, char *path);
void save(doc_t *document);

int lengthen(doc_t *document, int length);
void moveCursor(doc_t *document, int where);
void moveSelection(doc_t *document, int where);

void insert(doc_t *document, char *data, int length);
void doInsertAction(doc_t *document, int where, int length, char *data);
void doDeleteAction(doc_t *document, int from, int to);

void copyFromClipboardTo(doc_t *document);
void copyToClipboardFrom(doc_t *document);

int isPositionOutsideBounds(doc_t *document, int p);
int findPositionIn(doc_t *document, int mx, int y);

void scrollUp(doc_t *);
void scrollDown(doc_t *);
int moveLineUp(char *d, int i, int length);
int moveLineDown(char *d, int i, int length);

int advanceSubline(char *d, int i, int length);
int findWhitespaceFrom(char *d, int i);
int startOfLine(char *d, int i);
int getSubline(char *d, int i);

void setColor(uint32_t fg, uint32_t bg);
xcb_keysym_t getKeysym(xcb_keycode_t keycode);

char asciiupper(char c);
void msleep(unsigned long ms);
void die(char *msg);

int dontExit = 1;

xcb_connection_t *connection;
xcb_gcontext_t graphics;
xcb_window_t root;
xcb_key_symbols_t *keySymbols;
xcb_window_t window;
xcb_atom_t wm_delete_window_atom;
uint32_t bg, fg;

char *defaultstr = "This is a scratch document, it isn't from a file, and thus will not be saved.";

uint16_t lineoffset = 0;
uint16_t lineheight = 0;
uint16_t advanceLookupTable[95];
// advanceLookupTable starts from 0x20 'space'

uint16_t winwidth, winheight;

const event_handler_t eventHandlers[] = {
	[XCB_CLIENT_MESSAGE] = (event_handler_t) handleClientMessage,
	[XCB_BUTTON_PRESS] = (event_handler_t) handleButtonPress,
	[XCB_BUTTON_RELEASE] = (event_handler_t) handleButtonRelease,
	[XCB_KEY_PRESS] = (event_handler_t) handleKeyPress,
	[XCB_SELECTION_REQUEST] = (event_handler_t) clipboard_selectionRequest,
};

struct Keybinding {
	void (*action)(doc_t *);
	xcb_keysym_t sym;
	bool shift;
	bool control;
} keys[] = {
	{action_quit, .control=true, .sym = XK_q},
	{action_selectAll, .control=true, .sym = XK_a},
	{action_copy, .control=true, .sym = XK_c},
	{action_paste, .control=true, .sym = XK_v},
	{action_cut, .control=true, .sym = XK_x},
	{action_save, .control=true, .sym = XK_s},
	{action_reload, .control=true, .sym = XK_r},
	
	{action_cursorLeft, .sym = XK_Left},
	{action_cursorRight, .sym = XK_Right},
	{action_cursorUp, .sym = XK_Up},
	{action_cursorDown, .sym = XK_Down},
	
	{action_selectLeft, .shift=true, .sym = XK_Left},
	{action_selectRight, .shift=true, .sym = XK_Right},
	{action_selectUp, .shift=true, .sym = XK_Up},
	{action_selectDown, .shift=true, .sym = XK_Down},
	
	{action_backspace, .sym = XK_Delete},
	{action_newline, .sym = XK_Return},
	{action_tab, .sym = XK_Tab},
	
	{NULL}
};

doc_t *globalDocument;

int main(int argc, char **argv) {
	doc_t *document = load(NULL,argc > 1 ? argv[1] : NULL);
	globalDocument = document;
	setup(argc > 1 ? argv[1] : "scratch file");
	while (dontExit) events();
	cleanup();
	return 0;
}

void setup(char *windowTitle) {
	connection = xcb_connect(NULL, NULL);
	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
	
	root = screen->root;
	#ifdef DARKMODE
	bg = screen->black_pixel;
	fg = screen->white_pixel;
	#else
	bg = screen->white_pixel;
	fg = screen->black_pixel;
	#endif
	
	xcb_intern_atom_reply_t* atom_reply = xcb_intern_atom_reply(
		connection, xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW"), 0
	);
	wm_delete_window_atom = atom_reply->atom;
	free(atom_reply);
	
	graphics = xcb_generate_id(connection);
	
	char *fontname = "-b&h-lucida-medium-r-normal-sans-10------iso10646-1";
	//char *fontname = "fixed";
	xcb_font_t font = xcb_generate_id(connection);
	xcb_open_font(connection, font, strlen(fontname), fontname);
	
	xcb_create_gc(
		connection, graphics, root,
		XCB_GC_FOREGROUND | XCB_GC_BACKGROUND
			| XCB_GC_FONT | XCB_GC_GRAPHICS_EXPOSURES,
		(uint32_t[]) {fg, bg, font, 1}
	);
	
	xcb_query_text_extents_cookie_t advancesCookies[95];
	for (char c = 0x20; c < 0x7F; c++) {
		advancesCookies[c-0x20] = xcb_query_text_extents(
			connection, font, 1, (xcb_char2b_t[]){{0,c}}
		);
	}
	xcb_close_font(connection, font);
	for (char c = 0x20; c < 0x7F; c++) {
		xcb_query_text_extents_reply_t *reply = xcb_query_text_extents_reply(
			connection, advancesCookies[c-0x20], NULL
		);
		advanceLookupTable[c-0x20] = reply->overall_width;
		if (lineoffset < reply->font_ascent) lineoffset = reply->font_ascent;
		if (lineheight < reply->font_ascent + reply->font_descent) {
			lineheight = reply->font_ascent + reply->font_descent;
		}
		free(reply);
	}
	
	keySymbols = xcb_key_symbols_alloc(connection);
	if (!keySymbols) die("Could not access key symbols!");
	
	window = xcb_generate_id(connection);
	xcb_create_window(
		connection, XCB_COPY_FROM_PARENT,
		window, screen->root,
		0, 0, 150, 150, 10,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
		XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
		(uint32_t[]) {
			bg,
			XCB_EVENT_MASK_EXPOSURE
			| XCB_EVENT_MASK_KEY_PRESS
			| XCB_EVENT_MASK_BUTTON_PRESS
			| XCB_EVENT_MASK_BUTTON_RELEASE
			
		}
	);
	xcb_map_window(connection, window);
	
	xcb_change_property (
		connection, XCB_PROP_MODE_REPLACE, window,
		XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
		strlen(windowTitle), windowTitle
	);
	
	clipboard_init(connection, window, "TEXI_CLIPBOARD");
	
	xcb_flush(connection);
}

void cleanup() {
	xcb_key_symbols_free(keySymbols);
	xcb_destroy_window(connection, window);
	
	xcb_flush(connection);
	xcb_disconnect(connection);
}

static inline char hexdigit(unsigned char c) {return (c&0xf)>=10 ? (c&0xf)-10+'a' : (c&0xf)+'0';}

void glyph(char c, int x, int y) {
	if (c == '\t') {
		xcb_image_text_8(
			connection, 1, window, graphics,
			x, lineoffset+y, " "
		);
	} else if (c >= 0x20 && c < 0x7F) {
		xcb_image_text_8(
			connection, 1, window, graphics,
			x, lineoffset+y, &c
		);
	} else {
		char buffer[4];
		buffer[0] = '[';
		buffer[1] = hexdigit(((unsigned char)c)<<4);
		buffer[2] = hexdigit(c);
		buffer[3] = ']';
		xcb_image_text_8(
			connection, 4, window, graphics,
			x, lineoffset+y, buffer
		);
	}
}

int advance(char c) {
	if (c == '\t') {
		return 24;
	} else if (c >= 0x20 && c < 0x7F) {
		return advanceLookupTable[c-0x20];
	} else {
		return advanceLookupTable[hexdigit(c)-0x20]
			+ advanceLookupTable[hexdigit(((unsigned char)c)<<4)-0x20]
			+ advanceLookupTable['['-0x20] + advanceLookupTable[']'-0x20];
	}
}

void events() {
	xcb_generic_event_t *event = xcb_wait_for_event(connection);
	do {
		uint8_t evtype = event->response_type & ~0x80;
		if (evtype < sizeof(eventHandlers)/sizeof(event_handler_t) && eventHandlers[evtype]) {
			eventHandlers[evtype](event);
		}
		free(event);
		xcb_flush(connection);
	} while ((event = xcb_poll_for_event(connection)));
	
	xcb_get_geometry_reply_t* geom = xcb_get_geometry_reply(
		connection, xcb_get_geometry(connection, window), 0
	);
	winwidth = geom->width;
	winheight = geom->height;
	free(geom);
	
	draw(globalDocument);
	xcb_flush(connection);
	msleep(25);
}

void drawCursor(uint16_t x, uint16_t y) {
	xcb_poly_line(
		connection, 0, window, graphics, 2,
		(const xcb_point_t[]) {{x, y}, {x, y+lineheight}}
	);
}

void draw(doc_t *document) {
	xcb_clear_area(connection, 0, window, 0, 0, 0, 0);
	int x = 0, y = 0;
	char *d = document->data;
	
	int cur = document->cursor, sel = document->selection;
	if (cur > sel) {int _t = sel; sel = cur; cur = _t;}
	
	if (cur < document->scroll && sel > document->scroll) setColor(bg, fg);
	else setColor(fg, bg);
	
	int i = document->scroll;
	while (i < document->length && y <= winheight) {
		bool drawc = false;
		if (i == cur && cur == sel) drawc = true;
		else if (i == cur) setColor(bg, fg);
		else if (i == sel) setColor(fg, bg);
		if (d[i] == '\n' || x + advance(d[i]) >= winwidth) {
			if (d[i] == '\n') {
				glyph(' ', x, y);
				if (drawc) drawCursor(x,y);
				i++;
			}
			y += lineheight;
			x = 0;
		} else {
			glyph(d[i], x, y);
			if (drawc) drawCursor(x,y);
			x += advance(d[i++]);
		}
	}
	if (i == cur && cur == sel) drawCursor(x,y);
}

void handleClientMessage(xcb_client_message_event_t *event) {
	dontExit = event->data.data32[0] != wm_delete_window_atom;
}

void handleButtonPress(xcb_button_press_event_t *event) {
	if (event->detail == 1) {
		moveCursor(globalDocument,
			findPositionIn(globalDocument, event->event_x, event->event_y)
		);
	} else if (event->detail == 5) {
		scrollDown(globalDocument);
		scrollDown(globalDocument);
	} else if (event->detail == 4) {
		scrollUp(globalDocument);
		scrollUp(globalDocument);
	}
}

void handleKeyPress(xcb_key_press_event_t *event) {
	xcb_keysym_t keysym = xcb_key_symbols_get_keysym(keySymbols, event->detail, 0);
	int initialSelection = globalDocument->selection;
	
	bool control = event->state & XCB_MOD_MASK_CONTROL;
	bool shift = event->state & (XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_LOCK);
	
	if (!control && keysym >= XK_space && keysym <= XK_asciitilde) {
		char c = shift ? asciiupper(keysym) : (char) keysym;
		insert(globalDocument, &c, 1);
	} else for (struct Keybinding *key = keys; key->action; key++) {
		if (keysym == key->sym && control == key->control && shift == key->shift) {
			key->action(globalDocument);
		}
	}
	
	if (
		initialSelection != globalDocument->selection
		&& isPositionOutsideBounds(globalDocument, globalDocument->selection)
	) globalDocument->scroll = startOfLine(
		globalDocument->data, globalDocument->selection
	);
}

void handleButtonRelease(xcb_button_release_event_t *event) {
	if (event->detail == 1) {
		moveSelection(globalDocument, 
			findPositionIn(globalDocument, event->event_x, event->event_y)
		);
	}
}

void action_quit(doc_t *document) {(void) document; dontExit = 0;}

void action_selectAll(doc_t *doc) {moveCursor(doc, 0); moveSelection(doc, doc->length);}

void action_copy(doc_t *document) {copyToClipboardFrom(document);}
void action_paste(doc_t *document) {copyFromClipboardTo(document);}
void action_cut(doc_t *doc) {copyToClipboardFrom(doc); insert(doc, "", 0);}

void action_save(doc_t *document) {save(document);}
void action_reload(doc_t *document) {load(document, NULL);}

void action_selectLeft(doc_t *doc) {moveSelection(doc, doc->selection-1);}
void action_cursorLeft(doc_t *doc) {moveCursor(doc, doc->cursor-1);}
void action_selectRight(doc_t *doc) {moveSelection(doc, doc->selection+1);}
void action_cursorRight(doc_t *doc) {moveCursor(doc, doc->cursor+1);}

void action_selectUp(doc_t *doc) {
	moveSelection(doc, moveLineUp(doc->data, doc->selection, doc->length));
}
void action_cursorUp(doc_t *doc) {
	moveCursor(doc, moveLineUp(doc->data, doc->cursor, doc->length));
}
void action_selectDown(doc_t *doc) {
	moveSelection(doc, moveLineDown(doc->data, doc->selection, doc->length));
}
void action_cursorDown(doc_t *doc) {
	moveCursor(doc, moveLineDown(doc->data, doc->cursor, doc->length));
}

void action_backspace(doc_t *doc) {
	if (doc->cursor == doc->selection) moveSelection(doc, doc->selection-1);
	insert(doc, "", 0);
}

void action_newline(doc_t *doc) {
	int where = doc->cursor < doc->selection ? doc->cursor : doc->selection;
	where = startOfLine(doc->data, where);
	insert(doc, "\n", 1);
	insert(doc, doc->data+where, findWhitespaceFrom(doc->data, where));
}

void action_tab(doc_t *document) {
	insert(document, "\t", 1);
}
doc_t *load(doc_t *document, char *path) {
	if (!document) document = calloc(1,sizeof(doc_t));
	document->scroll = 0;
	document->cursor = 0;
	document->selection = 0;
	if (document) {
		if (!path && !document->path) {
			if (lengthen(document, strlen(defaultstr))) {
				memcpy(document->data, defaultstr, strlen(defaultstr));
				return document;
			}
		} else {
			if (!document->path) document->path = path;
			FILE *file = fopen(document->path, "r");
			if (file) {
				fseek(file, 0, SEEK_END);
				if (lengthen(document, ftell(file))) {
					rewind(file);
					fread(
						document->data, sizeof(char),
						document->length, file
					);
					fclose(file);
				}
			} else if (!lengthen(document, 0)) {
				die("Unable to create document!");
			}
			return document;
		}
	}
	die("Unable to create document!");
	return NULL;
}

void save(doc_t *document) {
	if (!document->path) return;
	FILE *file = fopen(document->path, "w");
	if (!file) return;
	fwrite(document->data, sizeof(char), document->length, file);
	fclose(file);
}

int lengthen(doc_t *document, int length) {
	document->length += length;
	if (document->length > document->size) {
		document->size = (document->length + 4095) & ~4095;
		document->data = realloc(document->data, document->size);
		if (!document->data) return 0;
	} else if (document->size == 0 || !document->data) {
		document->size = 4096;
		document->data = realloc(document->data, document->size);
		if (!document->data) return 0;
	}
	return document->size;
}

void moveCursor(doc_t *document, int where) {
	document->selection = where;
	document->cursor = where;
}

void moveSelection(doc_t *document, int where) {
	document->selection = where;
}

void insert(doc_t *document, char *data, int length) {
	if (document->cursor != document->selection) {
		doDeleteAction(document, document->cursor, document->selection);
	}
	if (data && length > 0) {
		doInsertAction(document, document->cursor, length, data);
	}
}

void doInsertAction(doc_t *document, int where, int length, char *data) {
	lengthen(document, length);
	memmove(
		document->data + where + length,
		document->data + where,
		document->length - where - length
	);
	memcpy(document->data + where, data, length);
	if (document->cursor >= where) document->cursor += length;
	if (document->selection >= where) document->selection += length;
}

void doDeleteAction(doc_t *document, int from, int to) {
	int where = from < to ? from : to;
	int length = from < to ? to-from : from-to;
	memmove(
		document->data + where,
		document->data + where + length,
		document->length - where - length
	);
	lengthen(document, -length);
	if (document->cursor >= where+length) document->cursor -= length;
	else if (document->cursor >= where) document->cursor = where;
	if (document->selection >= where+length) document->selection -= length;
	else if (document->selection >= where) document->selection = where;
}

void copyToClipboardFrom(doc_t *document) {
	int from = document->cursor;
	int to = document->selection;
	clipboard_set(
		document->data + (from < to ? from : to),
		from < to ? to-from : from-to
	);
}

void copyFromClipboardTo(doc_t *document) {
	char buffer[1024];
	int length,offset=0;
	do {
		length = clipboard_get(buffer, 1024, offset);
		offset += length;
		if (length > 0) insert(document, buffer, length);
	} while (length == 1024);
}

int findPositionIn(doc_t *document, int mx, int y) {
	int x = 0;
	char *d = document->data;
	int i = document->scroll;
	while (i < document->length && (
		y >= lineheight || (y >= 0 && d[i]!='\n' && mx >= x + advance(d[i]))
	)) {
		x += advance(d[i]);
		if (d[i] == '\n' || x >= winwidth) {
			if (d[i] == '\n') i++;
			y -= lineheight;
			x = 0;
		} else i++;
	}
	return i;
}

int isPositionOutsideBounds(doc_t *document, int p) {
	int x = 0, y = 0;
	char *d = document->data;
	int i = document->scroll;
	while (i < document->length && y < winheight && i != p) {
		x += advance(d[i]);
		if (d[i] == '\n' || x >= winwidth) {
			if (d[i] == '\n') i++;
			y += lineheight;
			x = 0;
		} else i++;
	}
	return i != p;
}

void scrollDown(doc_t *document) {
	document->scroll = advanceSubline(document->data, document->scroll, document->length);
}

void scrollUp(doc_t *document) {
	if (document->scroll == 0) return;
	char *d = document->data;
	int i = document->scroll;
	int subline = getSubline(d, i);
	i = startOfLine(d, i);
	if (subline == 0) {
		if (i > 0) {
			subline = getSubline(d, i-1);
			i = startOfLine(d, i-1);
		}
	} else subline--;
	while (subline > 0) {
		i = advanceSubline(d, i, document->length);
		subline--;
	}
	document->scroll = i;
}

int moveLineUp(char *d, int i, int length) {
	int old = i;
	int subline = getSubline(d, i);
	i = startOfLine(d, i);
	int j = i, w = 0;
	while (j < old) {
		w += advance(d[j]);
		if (w >= winwidth) w = 0;
		else j++;
	}
	if (w + advance(d[j]) >= winwidth) w = 0;
	if (subline == 0) {
		if (i > 0) {
			subline = getSubline(d, i-1);
			i = startOfLine(d, i-1);
		}
	} else subline--;
	while (subline > 0) {
		i = advanceSubline(d, i, length);
		subline--;
	}
	int x = 0;
	while (i < length && d[i] != '\n' && x < w) {
		x += advance(d[i]);
		if (x >= winwidth) {
			x = 0;
		} else i++;
	}
	return i;
}

int moveLineDown(char *d, int i, int length) {
	int old = i;
	i = startOfLine(d, i);
	int w = 0;
	while (i < old) {
		w += advance(d[i]);
		if (w >= winwidth) w = 0;
		else i++;
	}
	if (w + advance(d[i]) >= winwidth) w = 0;
	int x = w;
	do {
		x += advance(d[i]);
		if (d[i] == '\n' || x >= winwidth) {
			if (d[i] == '\n') i++;
			x = 0;
		} else i++;
	} while (x != 0);
	while (i < length && d[i] != '\n' && x < w) {
		x += advance(d[i]);
		if (x >= winwidth) {
			x = 0;
		} else i++;
	}
	return i;
}

int getSubline(char *d, int i) {
	int sublines = 0;
	int x = 0;
	if (d[i] == '\n') i--;
	while (i > 0 && d[i] != '\n') {
		x += advance(d[i]);
		if (x >= winwidth) {
			sublines++;
			x = 0;
		} else i--;
	}
	return sublines;
}

int startOfLine(char *d, int i) {
	int x = 0;
	if (d[i] == '\n') i--;
	while (i >= 0 && d[i] != '\n') {
		x += advance(d[i]);
		if (x >= winwidth) {
			x = 0;
		} else i--;
	}
	return i+1;
}

int findWhitespaceFrom(char *d, int i) {
	int old = i;
	while (d[i] == '\t' || d[i] == ' ') i++;
	return i-old;
}

int advanceSubline(char *d, int i, int length) {
	int old = i;
	int x = 0;
	while (i < length && d[i] != '\n' && x + advance(d[i]) < winwidth) {
		x += advance(d[i++]);
	}
	if (i >= length) return old;
	else if (d[i] == '\n') return i+1;
	else return i;
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

void setColor(uint32_t fg, uint32_t bg) {
	xcb_change_gc(
		connection, graphics,
		XCB_GC_FOREGROUND | XCB_GC_BACKGROUND,
		(uint32_t[]) {fg, bg}
	);
}

void msleep(unsigned long ms) {
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000;
	nanosleep(&ts, &ts);
}

void die(char *msg) {
	fprintf(stderr, "%s", msg);
	exit(EXIT_FAILURE);
}