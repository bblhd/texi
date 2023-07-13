# texi
A minimalist lightweight ascii text editor, an attempt to
solve some issues I had with other text editors.

## Usage
Open a text file with `texi <file>`. Type text to insert. 
Use arrows to move cursor, and hold shift while doing it
to change the selection. You can also move the cursor by
clicking on text, and change the selection by releasing
somewhere else. You can save changes by pressing `ctrl + s`,
select all with `ctrl + a`, copy with `ctrl + c`, cut with
`ctrl + x`, and paste with `ctrl + v`. You can also reload the
file with `ctrl + r` discarding unsaved changes, and quit
with `ctrl + q`.

Please note that texi **lacks undo functionality**, so be
careful when making changes you might want to undo.
In some cases using a version control system may be a
good substitute.

## Installing
Compile using `make` and then install it to /usr/local/bin
by running `make install` as root. It can be uninstalled with
`make uninstall`, also run as root.

### Requirements
- xcb
- xcb-keysyms

## Attributions
- Thanks to to [jtanx](https://github.com/jtanx) for their
[libclipboard](https://github.com/jtanx/libclipboard)
library which I borrowed liberally from.
- And also to tale-cohomology for their Stack Overflow
[answer](https://stackoverflow.com/a/72977399) about
getting started with the clipboard.
