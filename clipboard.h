#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <xcb/xcb.h>
#include <xcb/xproto.h>

void clipboard_init(xcb_connection_t *, xcb_window_t, char *label);
size_t clipboard_get(char *str, size_t length, size_t offset);
void clipboard_set(char *str, size_t length);
void clipboard_selectionRequest(xcb_selection_request_event_t *event);

#endif
