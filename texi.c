#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_keysyms.h>

#include "clipboard.h"

#define XK_MISCELLANY
#define XK_LATIN1
#include <X11/keysymdef.h>

xcb_key_symbols_t *keySymbols;

#define DOC_SIZE_STEP_SIZE 4096

char *documentPath = NULL;
size_t documentAllocatedSize = 0;
size_t documentCachedLength = 0;
char *document = NULL;
size_t scroll = 0;
int cachedScrollLine = 0;
int cursor = 0;
int selected = 0;

xcb_connection_t *connection;
xcb_gcontext_t graphics;
xcb_window_t root;
xcb_drawable_t window;

xcb_atom_t wm_delete_window_atom;

const uint32_t fgDefault = 0xff000000;
const uint32_t bgDefault = 0xffffffff;

const uint32_t fgCursor = bgDefault;
const uint32_t bgCursor = fgDefault;

const uint32_t fgSelected = 0xffffffff;
const uint32_t bgSelected = 0xff0000ff;

uint32_t oldBG = 0;
uint32_t oldFG = 0;

#define LINEBUFFER 38
#define LINEHEIGHT 14

void die(char *msg);

void loadDocument(char *path);
void loadDocumentFromString(char *string);

void handleButtonPress(xcb_button_press_event_t *event);
void handleButtonRelease(xcb_button_release_event_t *event);
void handleKeypress(xcb_key_press_event_t *event);

void drawNumbers(char *text, int line);
void drawText(char *text, int cursor, int selected);
size_t lengthOfDisplayedText(char *text);
size_t getMouseOnText(char *text, uint16_t mx, uint16_t my);

int main(int argc, char **argv) {
	connection = xcb_connect(NULL, NULL);
	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
	root = screen->root;
	
	xcb_intern_atom_reply_t* atom_reply = xcb_intern_atom_reply(connection, xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW"), 0);
	wm_delete_window_atom = atom_reply->atom;
	free(atom_reply);
	
	const char *fontname = "-xos4-terminus-medium-r-normal--14-140-72-72-c-0-iso10646-1";
	xcb_font_t font = xcb_generate_id(connection);
    xcb_open_font(connection, font, strlen(fontname), fontname);
    
	graphics = xcb_generate_id(connection);
	xcb_create_gc(
		connection, graphics, screen->root,
		XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT | XCB_GC_GRAPHICS_EXPOSURES,
		(uint32_t[]) {screen->black_pixel, screen->white_pixel, font, 1}
	);
	
	xcb_close_font(connection, font);
	
	window = xcb_generate_id(connection);
	xcb_create_window(
		connection, XCB_COPY_FROM_PARENT,
		window, screen->root,
		0, 0, 150, 150, 10,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
		XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
		(uint32_t[]) {screen->white_pixel,
			XCB_EVENT_MASK_EXPOSURE
			| XCB_EVENT_MASK_KEY_PRESS
			| XCB_EVENT_MASK_BUTTON_PRESS
			| XCB_EVENT_MASK_BUTTON_RELEASE
			
		}
	);
	xcb_map_window(connection, window);
	
	if (argc > 1) documentPath = argv[1];
	char *windowTitle = documentPath ? documentPath : "scratch";
	xcb_change_property (
		connection, XCB_PROP_MODE_REPLACE, window,
		XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
		strlen(windowTitle), windowTitle
	);
	
	clipboard_init(connection, window, "TEXI_CLIPBOARD");
	
	xcb_flush(connection);
	
	keySymbols = xcb_key_symbols_alloc(connection);
	
	int dontExit = 1;
	
	if (argc > 1) loadDocument(documentPath);
	else loadDocumentFromString("This is a scratch document, it isn't from a file, and thus wont be saved anywhere.");
	
	xcb_generic_event_t *event;
	while (dontExit && (event = xcb_wait_for_event(connection))) {
		switch (event->response_type & ~0x80) {
			case XCB_EXPOSE:
			break;
			
			case XCB_CLIENT_MESSAGE:
			if (((xcb_client_message_event_t *) event)->data.data32[0] == wm_delete_window_atom) {
				dontExit = 0;
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
		drawText(document+scroll, cursor-scroll, selected-scroll);
		drawNumbers(document+scroll, cachedScrollLine);
		xcb_flush(connection);
	}
	
	xcb_key_symbols_free(keySymbols);
	
	xcb_destroy_window(connection, window);
	xcb_flush(connection);
	xcb_disconnect(connection);

	return 0;
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

void loadDocument(char *path) {
	FILE *file = fopen(path, "rb");
	if (file) {
		fseek(file, 0, SEEK_END);
		documentCachedLength = ftell(file);
		allocateDocument(documentCachedLength+1);
		if (documentCachedLength > 0) {
			rewind(file);
			fread(document, documentCachedLength, 1, file);
			document[documentCachedLength] = 0;
		}
		fclose(file);
	}
}

void loadDocumentFromString(char *string) {
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

void scrollup();
void scrolldown();

void handleButtonPress(xcb_button_press_event_t *event) {
	// change scroll behaviours here
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

void handleKeypress(xcb_key_press_event_t *event) {
	xcb_keysym_t keysym = getKeysym(event->detail);
	
	if (event->state & XCB_MOD_MASK_CONTROL) {
		if (keysym == XK_c) {
			clipboard_set(document+(cursor<selected ? cursor : selected), selected>cursor ? selected-cursor : cursor-selected);
		} else if (keysym == XK_v) {
			char buffer[1024];
			selected = cursor = documentInsert(documentDeleteSelection(), clipboard_get(buffer, 1024), buffer);
		} else if (keysym == XK_x) {
			clipboard_set(document+(cursor<selected ? cursor : selected), selected>cursor ? selected-cursor : cursor-selected);
			documentDeleteSelection();
		} else if (keysym == XK_s) {
			saveDocument(documentPath);
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


uint16_t advance(char c);
void glyph(char c, uint16_t x, uint16_t y);
void glyph16(int c, uint16_t x, uint16_t y);
void glyphtext(char *str, uint16_t x, uint16_t y);

void getDimensions(uint16_t *width, uint16_t *height);
void setColor(uint32_t fg, uint32_t bg);

void drawNumbers(char *text, int line) {
	char buffer[12];
	
	uint16_t x = LINEBUFFER;
	uint16_t y = LINEHEIGHT;
	uint16_t width = 0;
	uint16_t height = 0;
	getDimensions(&width, &height);
	int n = 0;
	
	line++;
	snprintf(buffer, 12, "%i", line);
	glyphtext(buffer, 0, y);
	while (y < height-LINEHEIGHT && text[n]) {
		uint16_t dx = advance(text[n]);
		if (x + dx >= width) {
			x = LINEBUFFER;
			y += LINEHEIGHT;
		}
		if (text[n] == '\n') {
			x = LINEBUFFER;
			y += LINEHEIGHT;
			
			line++;
			snprintf(buffer, 12, "%i", line);
			glyphtext(buffer, 0, y);
		}
		
		x += dx;
		n++;
	}
}

void drawText(char *text, int cursor, int selected) {
	if (selected < cursor) {
		int c = cursor;
		cursor = selected;
		selected = c;
	}
	
	uint16_t x = LINEBUFFER;
	uint16_t y = LINEHEIGHT;
	uint16_t width = 0;
	uint16_t height = 0;
	getDimensions(&width, &height);
	int n = 0;
	
	setColor(bgDefault, fgDefault);
	xcb_poly_fill_rectangle(connection, window, graphics, 1, (xcb_rectangle_t[]) {{0,0,width,height}});
	
	setColor(fgDefault, bgDefault);
	
	if (cursor < 0 && selected >= 0) {
		setColor(fgSelected, bgSelected);
	}
	
	while (y < height-LINEHEIGHT && text[n]) {
		if (n == cursor) {
			if (cursor==selected) {
				setColor(fgCursor, bgCursor);
			} else {
				setColor(fgSelected, bgSelected);
			}
		}
		
		uint16_t dx = advance(text[n]);
		if (x + dx >= width) {
			x = LINEBUFFER;
			y += LINEHEIGHT;
		}
		if (text[n] == '\n') {
			glyph16(0x21b5, x, y);
			x = LINEBUFFER;
			y += LINEHEIGHT;
		} else if (text[n] == '\t') {
			//glyph16(0x2192, x, y);
			glyph(' ', x, y);
		} else if (text[n] == ' ') {
			glyph(' ', x, y);
		} else {
			glyph(text[n], x, y);
		}
		
		x += dx;
		
		if ((cursor==selected && n == selected) || n == selected-1) {
			setColor(fgDefault, bgDefault);
		}
		
		n++;
	}
	
	if (n == cursor) {
		setColor(fgCursor, bgCursor);
	}
	glyph(' ', x, y);
	setColor(fgDefault, bgDefault);
}

size_t lengthOfDisplayedText(char *text) {
	uint16_t x = LINEBUFFER;
	uint16_t y = LINEHEIGHT;
	uint16_t width = 0;
	uint16_t height = 0;
	getDimensions(&width, &height);
	size_t n = 0;
	
	while (y < height-LINEHEIGHT && text[n]) {
		uint16_t dx = advance(text[n]);
		if (x + dx >= width) {
			x = LINEBUFFER;
			y += LINEHEIGHT;
		}
		if (text[n] == '\n') {
			x = LINEBUFFER;
			y += LINEHEIGHT;
		} else if (text[n] == '\t') {
			x += dx;
		} else {
			x += dx;
		}
		n++;
	}
	return n;
}

size_t getMouseOnText(char *text, uint16_t mx, uint16_t my) {
	uint16_t x = LINEBUFFER;
	uint16_t y = LINEHEIGHT;
	uint16_t width = 0;
	uint16_t height = 0;
	getDimensions(&width, &height);
	size_t n = 0;
	
	while (y < height && text[n]) {
		uint16_t dx = advance(text[n]);
		if (x + dx >= width) {
			if (my>=x && my>=y-LINEHEIGHT && my<y) {
				return n;
			}
			x = LINEBUFFER;
			y += LINEHEIGHT;
		}
		if (text[n] == '\n') {
			if (mx>=x && my>=y-LINEHEIGHT && my<y) {
				return n;
			}
			x = LINEBUFFER;
			y += LINEHEIGHT;
		} else if (text[n] == '\t') {
			if (mx>=x && mx<x+dx && my>=y-LINEHEIGHT && my<y) {
				return n;
			}
		} else {
			if (mx>=x && mx<x+dx && my>=y-LINEHEIGHT && my<y) {
				return n;
			}
		}
		x += dx;
		n++;
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

void scrollup() {
	if (scroll>1) cachedScrollLine--;
	if (scroll>1 && document[scroll-1] == '\n') scroll-=2;
	while (scroll>0 && document[scroll] != '\n') scroll--;
	if (scroll>0 && document[scroll]) scroll++;
}

void scrolldown() {
	if (lengthOfDisplayedText(document+scroll) >= documentCachedLength-scroll) return;
	while (document[scroll] && document[scroll] != '\n') scroll++;
	if (document[scroll]) scroll++;
	if (document[scroll]) cachedScrollLine++;
}

int offsetUp(int c) {
	int n = 0;
	if (c-n>0 && document[c-n] == '\n') n++;
	while (c-n>0 && document[c-n] != '\n') n++;
	if (c-n>0 && document[c-n]) n--;
	c-=n;
	
	if (c>1) c-=2;
	while (c>0 && document[c] != '\n') c--;
	if (c>0 && document[c]) c++;
	
	while (n-- && document[c] && document[c] != '\n') c++;
	return c;
}

int offsetDown(int c) {
	int n = 0;
	if (c-n>0 && document[c-n] == '\n') n++;
	while (c-n>0 && document[c-n] != '\n') n++;
	if (c-n>0 && document[c-n]) n--;
	
	while (document[c] && document[c] != '\n') c++;
	if (document[c]) c++;
	
	while (n-- && document[c] && document[c] != '\n') c++;
	return c;
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
	if (size > 0) {
		memmove(document+where-size, document+where, strlen(document+cursor)+1);
		where-=size;
		documentCachedLength-=size;
	}
	return where;
}

int documentDeleteSelection() {
	cursor = documentDelete(selected>cursor ? selected : cursor, selected>cursor ? selected-cursor : cursor-selected);
	selected = cursor;
	return cursor;
}

void getDimensions(uint16_t *width, uint16_t *height) {
	xcb_get_geometry_reply_t* geom = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, window), 0);
	if (width) *width = geom->width;
	if (height) *height = geom->height;
	free(geom);
}

uint16_t advance(char c) {
	if (c == '\t') return 32;
	else if (c == '\n') return 0;
	else return 8;
}

void glyph(char c, uint16_t x, uint16_t y) {
	xcb_image_text_8(connection, 1, window, graphics, x, y, (char[]) {c, 0});
}

void glyph16(int c, uint16_t x, uint16_t y) {
	xcb_image_text_16(connection, 1, window, graphics, x, y, (const xcb_char2b_t[]) {{(c>>8)&0xff, c&0xff}, {0, 0}});
}

void glyphtext(char *str, uint16_t x, uint16_t y) {
	xcb_image_text_8(connection, strlen(str), window, graphics, x, y, str);
}

void setColor(uint32_t fg, uint32_t bg) {
	if (bg != oldBG && fg != oldFG && bg != 0 && fg != 0) {
		xcb_change_gc(connection, graphics, XCB_GC_FOREGROUND | XCB_GC_BACKGROUND, (uint32_t[]) {fg, bg});
	} else if (bg != oldBG && bg != 0) {
		xcb_change_gc(connection, graphics, XCB_GC_BACKGROUND, (uint32_t[]) {bg});
	} else if (fg == oldFG && fg != 0) {
		xcb_change_gc(connection, graphics, XCB_GC_FOREGROUND, (uint32_t[]) {fg});
	}
	oldBG = bg;
	oldFG = fg;
}

void die(char *msg) {
	fprintf(stderr, "ERROR: %s", msg);
	exit(EXIT_FAILURE);
}
