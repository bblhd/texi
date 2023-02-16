//code adapted from https://stackoverflow.com/questions/37295904/clipboard-and-xcb and https://github.com/jtanx/libclipboard
//resource: https://www.x.org/releases/X11R7.6/doc/xorg-docs/specs/ICCCM/icccm.html#peer_to_peer_communication_by_means_of_selections

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>

enum ClipboardAtom {CLIPBOARD, TARGETS, STRING, UTF8_STRING, UTF8_PLAINTEXT, ATOM_END};
const char *atomNames[ATOM_END] = {"CLIPBOARD", "TARGETS", "STRING", "UTF8_STRING", "text/plain;charset=utf-8"};
xcb_atom_t atoms[ATOM_END];

xcb_atom_t property;

const enum ClipboardAtom selection = CLIPBOARD;

struct {
	xcb_connection_t *connection;
	xcb_window_t window;
	
	char *source;
	size_t length;
} clipboard;

xcb_atom_t getAtomReply(xcb_intern_atom_cookie_t cookie) {
	xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(clipboard.connection, cookie, NULL);
	xcb_atom_t atom = reply ? reply->atom : XCB_NONE;
	free(reply);
	return atom;
}

void clipboard_init(xcb_connection_t *connection, xcb_window_t window, char *label) {
	clipboard.connection = connection;
	clipboard.window = window;
	clipboard.source = NULL;
	
	xcb_intern_atom_cookie_t propertyCookie = xcb_intern_atom(clipboard.connection, 0, strlen(label), label);
	
	xcb_intern_atom_cookie_t atomCookies[ATOM_END];
	for (int i = 0; i < ATOM_END; i++) {
		atomCookies[i] = xcb_intern_atom(clipboard.connection, 0, strlen(atomNames[i]), atomNames[i]);
	}
	
	for (int i = 0; i < ATOM_END; i++) {
		atoms[i] = getAtomReply(atomCookies[i]);
	}
	
	property = getAtomReply(propertyCookie);
}

//clipboard_get: reads the clipboard from its current owner, copying it to the provided buffer
size_t clipboard_get(char *str, size_t length, size_t offset) {
	xcb_convert_selection(
		clipboard.connection, clipboard.window, atoms[selection], 
		XCB_ATOM_STRING, property,
		XCB_CURRENT_TIME
	);
	xcb_flush(clipboard.connection);
	
	free(xcb_wait_for_event(clipboard.connection));
	
	xcb_get_property_reply_t* reply = xcb_get_property_reply(
		clipboard.connection,
		xcb_get_property(
			clipboard.connection, 0, clipboard.window,
			property, atoms[STRING],
			offset/4, length/4
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
	} else {
		length = 0;
	}
	
	xcb_delete_property(clipboard.connection, clipboard.window, property);
	return length;
}

//clipboard_set: takes ownership of the clipboard and caches the given string, which will later be provided to those requesting data from the clipboard
void clipboard_set(char *str, size_t length) {
	if (clipboard.source) free(clipboard.source);
	clipboard.source = malloc(length);
	clipboard.length = length;
	memcpy(clipboard.source, str, clipboard.length);
	xcb_set_selection_owner(clipboard.connection, clipboard.window, atoms[selection], XCB_CURRENT_TIME);
	xcb_flush(clipboard.connection);
}

//clipboard_selectionRequest: handles a selection request event and passes the clipboard data to the requestor, sends the selection notify event afterwards 
void clipboard_selectionRequest(xcb_selection_request_event_t *event) {
	if (event->property == XCB_NONE) event->property = event->target;
	
	//printf("target atom %u recieved\n", event->target); // used for debugging
	if (event->target == atoms[TARGETS]) {
		xcb_atom_t targets[] = {atoms[TARGETS], atoms[STRING], atoms[UTF8_STRING], atoms[UTF8_PLAINTEXT]};
		xcb_change_property(
			clipboard.connection, XCB_PROP_MODE_REPLACE,
			event->requestor, event->property,
			XCB_ATOM_ATOM, sizeof(xcb_atom_t) * 8, sizeof(targets) / sizeof(xcb_atom_t), targets
		);
	} else if (event->target == atoms[STRING] || event->target == atoms[UTF8_STRING] || event->target == atoms[UTF8_PLAINTEXT]) {
		xcb_change_property(
			clipboard.connection, XCB_PROP_MODE_REPLACE,
			event->requestor, event->property,
			event->target, 8, clipboard.length, clipboard.source
		);
	} else {
		xcb_flush(clipboard.connection);
		return;
	}
	
	xcb_send_event(
		clipboard.connection, 0, event->requestor, 0,
		(char *) &(xcb_selection_notify_event_t) {
			.response_type = XCB_SELECTION_NOTIFY,
			.time = XCB_CURRENT_TIME,
			.requestor = event->requestor,
			.selection = event->selection,
			.target = event->target,
			.property = event->property
		}
	);
	
	xcb_flush(clipboard.connection);
}
