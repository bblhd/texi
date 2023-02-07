# texi
Minimalist ascii text editor, an attempt to solve some
issues I had with other text editors.

## How to compile
Compile using `make` or `make compile`, and then install
it to /usr/local/bin by running `make install` as root.

## How to use
Open a text file with `texi <file>`. Type text to insert. 
Use arrows to move cursor, and hold shift while doing it
to change the selection. You can also move the cursor by
clicking on text, and change the selection by releasing
somewhere else. You can save changes by pressing `ctrl + s`,
copy with `ctrl + c`, and paste with `ctrl + c`. 

### Problems/caveats
- Pasting works fine, although copying is mostly broken,
save for within the same file and a select few programs.
- When the screen redraws, there is some degree of flickering, hopefully not too much.
- texi lacks undo functionality, although this is not a bug.
