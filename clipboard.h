#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <xcb/xcb.h>
#include <xcb/xproto.h>

void clipboard_init(xcb_connection_t *, xcb_window_t, char *label);
int clipboard_get(char *str, int length, int offset);
void clipboard_set(char *str, int length);
void clipboard_selectionRequest(xcb_selection_request_event_t *event);

#endif
