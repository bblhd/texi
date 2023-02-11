//code adapted from https://stackoverflow.com/questions/37295904/clipboard-and-xcb and https://github.com/jtanx/libclipboard
//resource: https://www.x.org/releases/X11R7.6/doc/xorg-docs/specs/ICCCM/icccm.html#peer_to_peer_communication_by_means_of_selections

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>

struct {
  xcb_connection_t *connection;
  xcb_window_t window;
  
  xcb_atom_t selection;
  xcb_atom_t property;
  
  char *source;
  size_t length;
} clipboard;

void clipboard_init(xcb_connection_t *connection, xcb_window_t window, char *label) {
	clipboard.connection = connection;
	clipboard.window = window;
	clipboard.source = NULL;

	xcb_intern_atom_cookie_t cookie_selection = xcb_intern_atom(clipboard.connection, 0, 9, "CLIPBOARD");
	xcb_intern_atom_cookie_t cookie_property = xcb_intern_atom(clipboard.connection, 0, 9, label);

	xcb_intern_atom_reply_t* reply_selection  = xcb_intern_atom_reply(clipboard.connection, cookie_selection, NULL);
	xcb_intern_atom_reply_t* reply_property   = xcb_intern_atom_reply(clipboard.connection, cookie_property, NULL);

	clipboard.selection = reply_selection->atom;
	clipboard.property = reply_property->atom;

	free(reply_selection);
	free(reply_property);
}

//clipboard_get: reads the clipboard from its current owner, copying it to the provided buffer
size_t clipboard_get(char *str, size_t length) {
	xcb_convert_selection(
		clipboard.connection, clipboard.window, clipboard.selection, 
		XCB_ATOM_STRING, clipboard.property,
		XCB_CURRENT_TIME
	);
	xcb_flush(clipboard.connection);
	
	free(xcb_wait_for_event(clipboard.connection));
	
	xcb_get_property_reply_t* reply = xcb_get_property_reply(
		clipboard.connection,
		xcb_get_property(
			clipboard.connection, 0, clipboard.window,
			clipboard.property, XCB_ATOM_STRING,
			0, length
		),
		NULL
	);
	
	if (reply && xcb_get_property_value_length(reply) > 0) {
		length = length < (size_t) xcb_get_property_value_length(reply) ? length : (size_t) xcb_get_property_value_length(reply);
		memcpy(str, xcb_get_property_value(reply), length);
		free(reply);
	} else if (clipboard.source) {
		length = length < clipboard.length ? length : clipboard.length;
		memcpy(str, clipboard.source, length);
	}
	
	xcb_delete_property(clipboard.connection, clipboard.window, clipboard.property);
	return length;
}

//clipboard_set: takes ownership of the clipboard and caches the given string, which will later be provided to those requesting data from the clipboard
void clipboard_set(char *str, size_t length) {
	if (clipboard.source) free(clipboard.source);
	clipboard.source = malloc(length);
	clipboard.length = length;
	memcpy(clipboard.source, str, clipboard.length);
	xcb_set_selection_owner(clipboard.connection, clipboard.window, clipboard.selection, XCB_CURRENT_TIME);
	xcb_flush(clipboard.connection);
}

//clipboard_selectionRequest: handles a selection request event and passes the clipboard data to the requestor, sends the selection notify event afterwards 
void clipboard_selectionRequest(xcb_selection_request_event_t *event) {
	//todo: handle different targets correctly with proper timestamp support
	if (event->property == XCB_NONE) event->property = event->target;
    
	xcb_change_property(
		clipboard.connection, XCB_PROP_MODE_REPLACE,
		event->requestor, event->property,
		XCB_ATOM_STRING, 8, clipboard.length, clipboard.source
	);
    
	xcb_send_event(
		clipboard.connection, 0, event->requestor, XCB_EVENT_MASK_PROPERTY_CHANGE,
		(char *) &(xcb_selection_notify_event_t) {
			.response_type = XCB_SELECTION_NOTIFY,
			.time = XCB_CURRENT_TIME,
			.requestor = event->requestor,
			.selection = event->selection,
			.target = XCB_ATOM_STRING,
			.property = event->property
		}
	);
	
	xcb_flush(clipboard.connection);
}
