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

typedef struct Document doc_t;

typedef void (*event_handler_t)(xcb_generic_event_t *);

struct Document {
	char *path, *data;
	int length, size;
	int scroll, cursor, selection;
};

doc_t *load(doc_t *document, char *path);
void save(doc_t *document);

void insert(doc_t *document, char *data, int length);
void moveCursor(doc_t *document, int where);
void moveSelection(doc_t *document, int where);
int lengthen(doc_t *document, int length);
int scrollByLines(doc_t *document, int position, int lines);

void setup();
void cleanup();
void events();
void draw(doc_t *document);
int findPositionIn(doc_t *document, int x, int y);

void handleClientMessage(xcb_client_message_event_t *event);
void handleButtonPress(xcb_button_press_event_t *event);
void handleButtonRelease(xcb_button_release_event_t *event);
void handleKeyPress(xcb_key_press_event_t *event);
void handleSelectionRequest(xcb_selection_request_event_t *event);

void setColor(uint32_t fg, uint32_t bg);
xcb_keysym_t getKeysym(xcb_keycode_t keycode);

char asciiupper(char c);
void msleep(unsigned long ms);
void die(char *msg);

int dontExit = 1;

xcb_connection_t *connection;
xcb_gcontext_t graphics;
xcb_font_t font;
xcb_window_t root;
xcb_window_t window;
xcb_atom_t wm_delete_window_atom;
xcb_key_symbols_t *keySymbols;
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
	bg = screen->white_pixel;
	fg = screen->black_pixel;
	
	xcb_intern_atom_reply_t* atom_reply = xcb_intern_atom_reply(
		connection, xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW"), 0
	);
	wm_delete_window_atom = atom_reply->atom;
	free(atom_reply);
	
	graphics = xcb_generate_id(connection);
	
	char *fontname = "-b&h-lucida-medium-r-normal-sans-10------iso10646-1";
	//char *fontname = "fixed";
	font = xcb_generate_id(connection);
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
	};
	xcb_close_font(connection, font);
	for (char c = 0x20; c < 0x7F; c++) {
		xcb_query_text_extents_reply_t *reply = xcb_query_text_extents_reply(
			connection, advancesCookies[c-0x20], NULL
		);
		advanceLookupTable[c-0x20] = reply->overall_width;
		if (lineoffset < reply->font_ascent) lineoffset = reply->font_ascent;
		if (lineheight < reply->font_ascent + reply->font_descent) {
			lineheight = reply->font_ascent + reply->font_descent;
		};
		free(reply);
	};
	
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
	
	keySymbols = xcb_key_symbols_alloc(connection);
	
	clipboard_init(connection, window, "TEXI_CLIPBOARD");
	
	xcb_flush(connection);
}

void cleanup() {
	xcb_key_symbols_free(keySymbols);
	xcb_close_font(connection, font);
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
	};
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
	};
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
	};
	return document->size;
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
			};
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
				};
			} else if (!lengthen(document, 0)) {
				die("Unable to create document!");
			}
			return document;
		};
	};
	die("Unable to create document!");
}

void save(doc_t *document) {
	if (!document->path) return;
	FILE *file = fopen(document->path, "w");
	if (!file) return;
	fwrite(document->data, sizeof(char), document->length, file);
	fclose(file);
}

void moveCursor(doc_t *document, int where) {
	document->selection = where;
	document->cursor = where;
}

void moveSelection(doc_t *document, int where) {
	document->selection = where;
}

void doInsertAction(doc_t *document, int where, int length, char *data);
void doDeleteAction(doc_t *document, int from, int to);

void insert(doc_t *document, char *data, int length) {
	if (document->cursor != document->selection) {
		doDeleteAction(document, document->cursor, document->selection);
	};
	if (data && length > 0) {
		doInsertAction(document, document->cursor, length, data);
	};
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
	};
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
	};
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
	};
	if (i >= length) return old;
	else if (d[i] == '\n') return i+1;
	else return i;
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
		};
	} else subline--;
	while (subline > 0) {
		i = advanceSubline(d, i, document->length);
		subline--;
	};
	document->scroll = i;
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
		};
	};
	if (i == cur && cur == sel) drawCursor(x,y);
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
	};
	return i;
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
	};
}

void handleButtonRelease(xcb_button_release_event_t *event) {
	if (event->detail == 1) {
		moveSelection(globalDocument, 
			findPositionIn(globalDocument, event->event_x, event->event_y)
		);
	};
}

void copyToClipboard(char *d, int from, int to) {
	clipboard_set(
		d + (from < to ? from : to),
		from < to ? to-from : from-to
	);
}

void copyFromClipboard(doc_t *document) {
	char buffer[1024];
	int length,offset=0;
	do {
		length = clipboard_get(buffer, 1024, offset);
		offset += length;
		if (length > 0) insert(document, buffer, length);
	} while (length == 1024);
}

void handleKeyPress(xcb_key_press_event_t *event) {
	xcb_keysym_t keysym = getKeysym(event->detail);
	int initialSelected = globalDocument->selection;
	
	bool control = event->state & XCB_MOD_MASK_CONTROL;
	bool shift = event->state & (XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_LOCK);
	
	if (control) {
		if (keysym == XK_a) {
			moveCursor(globalDocument, 0);
			moveSelection(globalDocument, globalDocument->length);
		} else if (keysym == XK_c) {
			copyToClipboard(globalDocument->data, globalDocument->cursor, globalDocument->selection);
		} else if (keysym == XK_v) {
			copyFromClipboard(globalDocument);
		} else if (keysym == XK_s) {
			save(globalDocument);
		} else if (keysym == XK_r) {
			load(globalDocument, NULL);
		}
	} else {
		if (keysym == XK_Left) {
			if (shift) moveSelection(globalDocument, globalDocument->selection-1);
			else moveCursor(globalDocument, globalDocument->cursor-1);
		} else if (keysym == XK_Right) {
			if (shift) moveSelection(globalDocument, globalDocument->selection+1);
			else moveCursor(globalDocument, globalDocument->cursor+1);
		} else if (keysym == XK_BackSpace) {
			if (globalDocument->cursor == globalDocument->selection) {
				moveSelection(globalDocument, globalDocument->selection-1);
			}; insert(globalDocument, "", 0);
		} else if (keysym == XK_Return) {
			int where = globalDocument->cursor < globalDocument->selection
				? globalDocument->cursor : globalDocument->selection;
			where = startOfLine(globalDocument->data, where);
			int indent = findWhitespaceFrom(globalDocument->data, where);
			insert(globalDocument, "\n", 1);
			insert(globalDocument, globalDocument->data+where, indent);
		} else if (keysym == XK_Tab) {
			insert(globalDocument, "\t", 1);
		} else if (keysym >= XK_space && keysym <= XK_asciitilde) {
			char c = shift ? asciiupper(keysym) : keysym;
			insert(globalDocument, &c, 1);
		};
	};
}

void handleSelectionRequest(xcb_selection_request_event_t *event) {
	(void) event;
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
	};
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