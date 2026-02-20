/*** includes ***/

// There is a possibility, depending on the compiler, it may copmlain about getline()
// This is why these macros were added, code is more portable
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <termios.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>
#include <stdarg.h>

// Disabled flags: ECHO, ICANON, ISIG, IXON, IEXTEN, ICRNL, OPOST.
// These correspond to specific CTRL operations.
// ECHO: flag responsible for echoing the characters we type in the terminal
// without it we can't see what we type
// ICANON - canonical/cooked mode flag.
// ISIG: CTRL+C & CTRL+Z functionality
// IXON: CTRL+S & CTRL+Q functionality
// IEXTEN: CTRL+V on Linux & Windows, CTRL+O on MacOS
// ICRNL: CTRL+M functionality
// OPOST: flag resopnsible for the output processing, that is done by the terminal.
// Basically, as you know the terminal translates our certain inputs like CTRL+S,
// same is done on its output side. Which is why we turn off the OPOST flag

#define TEXIT_VERSION "0.0.1"
#define TEXIT_TAB_STOP 4

// hex 0x1f = 0001 1111 (in binary) = 31 (in decimal)
#define CTRL_KEY(k) ((k) & 0x1f) // Simple macro for better understanding

// row = y; column = x
enum editorKey { // important functional keys
    BACKSPACE = 127,
    ARROW_LEFT = 1000, // choosing large enough number, so there are no conflicts
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,

    PAGE_UP,
    PAGE_DOWN,

    HOME_KEY,
    END_KEY,

    DEL_KEY
};


typedef struct erow{
    int size; // file row char size
    int rsize; // screen row char size
    char* chars; // file row chars content
    char* render; // the row character content that will actually be displayed on the screen
                  // basically what will be drawn on the screen, not the direct file contents
}erow;

// Convenient struct to store everything related to our terminal settings
// IMPORTANT!: cx and cy use 0-based indexing, even though terminals are 1-based indexed
struct editorConfig {
    int cx, cy; // cursor placement. Zero-based indexing for coordinates
    int rx;
    int rowoff; // row offset - this will keep track of the top file row that's on screen
    int coloff; // column offset = same logic as row offset, but for columns

    int screenrows;
    int screencols;

    int numrows; // number of the rows in the file that we open
    // an array that will contain all the rows of the opened file
    erow *row;
    struct termios orig_termios;

    char *filename; // the name of our file that we open
    char statusmsg[80];
    time_t statusmsg_time;
};

struct editorConfig E;

// 'dynamic' string struct
struct abuf {
    char *b;
    int len;
};
// constructor for our abuf struct
#define ABUF_INIT {NULL, 0} // pointer to null, length = 0


/*** Prototypes ***/
void die(const char *s);
void disableRawMode();
void enableRawMode();
int editorReadKey();
void editorDrawRows();
void clearScreen();
void editorRefreshScreen();
void editorProcessKeypress();
int getWindowSize(int *rows, int *cols);
void editorMoveCursor(int key);
void editorOpen(char* filename);
void editorAppendRow(char *s, size_t len);
void editorScroll();
void editorUpdateRow(erow *row);
void editorSetStatusMessage(const char *fmt, ...);


// error handling function
void die(const char *s) {
    // clear screen before exiting
    clearScreen();

    perror(s);
    exit(1);
}


/***  Base terminal functions  ***/
void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode(){
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr"); // gets current terminal settings
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;

    // Disabling various flags mentioned above
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    raw.c_cc[VMIN] = 1; // Minimum amount of bytes until read returnss
    raw.c_cc[VTIME] = 0; // wait 0.4 seconds for input

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr"); // sets terminal settings
}

int editorReadKey() {
    int nread; // variable to store the return of read
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) { // if the one byte wasn't read
        if (nread == -1 && errno != EAGAIN)
            die("read"); // if error --> print error and exit the program
    }

    // if we stumble upon an escape sequence.
    // This can be verified by checking that the 1st byte is the ESC char
    if(c == '\x1b'){
        // buffer to store what is left after the escape char, as it is more than 1 byte
        char seq[3];
        // We make the seq buffer is 3 bytes long because
        // we will be handling longer escape sequences in the future

        // If reads time out (no more bytes after ESC char), then return the ESC and quit
        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if(seq[0] == '['){
            if(seq[1] >= '0' && seq[1] <= '9'){ // If ESC [ num, where num is btwn 0 and 9

                // if read times out
                if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';

                if(seq[2] == '~'){
                    switch (seq[1]){
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;   // ESC [ 3 ~
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;   // ESC [ 5 ~
                        case '6': return PAGE_DOWN; // ESC [ 6 ~
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }

            }
            else{
                switch (seq[1]){
                    case 'A': return ARROW_UP;    // ESC [ A --> Up arrow key
                    case 'B': return ARROW_DOWN;  // ESC [ B --> Down arrow key
                    case 'C': return ARROW_RIGHT; // ESC [ C --> Right arrow key
                    case 'D': return ARROW_LEFT;  // ESC [ D --> Left arrow key

                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        }
        else if(seq[0] == 'O'){
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    }
    else return c;
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    // The command sent to get the cursor position
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        // read each byte into the buffer one by one
        // until we hit 'R', bcos the reply ends with 'R'
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }

    buf[i] = '\0'; // indicate the end of the buffer string using null terminator
    if (buf[0] != '\x1b' || buf[1] != '[') return -1; // check that we got a correct reply
    // Start reading the input string stored in the buffer from the 3rd character
    // Then we store the row and column amounts into the specified variables
    // This is done by matching the buffer string against the format string
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;

        return getCursorPosition(rows, cols);
    }
    else{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** Row operations ***/

int editorRowCxToRx(erow *row, int cx) {
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (TEXIT_TAB_STOP - 1) - (rx % TEXIT_TAB_STOP);
        rx++;
    }
    return rx;
}

// Function to account for special characters that may appear in text, like tabs for example
void editorUpdateRow(erow *row) {
    // Firstly we count the amount of tabs, to allocated enough space for chars
    // as 1 tab = 4 spaces
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    // Rebuilding the render from scratch, we don't care about old screen representation
    free(row->render);
    row->render = malloc(row->size + tabs*(TEXIT_TAB_STOP-1) + 1); // account space for tabs as well

    // index of the render chars
    int idx = 0;
    // Copy the characters
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % TEXIT_TAB_STOP != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx; // setting the size of the render chars
}

void editorAppendRow(char *s, size_t len) {
    // We reallocate the array memory space each row
    // The last realloc call will be the amount of rows there are in the file
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1)); // Initially E.numrows = 0
    int at = E.numrows; // index of the current row in the array of all rows

    E.row[at].size = len; // set the length of the current row
    E.row[at].chars = malloc(len + 1); // allocate memory for the row. (+1 for '\0')

    memcpy(E.row[at].chars, s, len); // copy the row
    E.row[at].chars[len] = '\0'; // null-terminate

    // Initialize the to be displayed row contents
    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);

    E.numrows++; // increment to indicate another row that was appended
}

void editorRowInsertChar(erow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    // allocate new memory for the inserted char
    row->chars = realloc(row->chars, row->size + 2); // +2 for new char & null terminator 
    // copy chars starting from the index [at] to the index [at+1]
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);

    row->size++; // update row size, as a char was inserted
    row->chars[at] = c; // insert the character
    editorUpdateRow(row);
}

/*** editor operations ***/
void editorInsertChar(int c){
    if(E.cy == E.numrows){
        editorAppendRow("", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

/***  File I/O functions ***/

char* editorRowsToString(int *buflen){
    int totlen = 0; // total file string length
    int j;
    for(j = 0; j < E.numrows; j++)
        totlen += E.row[j].size +1; // for each row +1 considering the addition of newline '\n'
    *buflen = totlen; // total length of the file

    char* buf = malloc(totlen); // allocate enough string space
    char* p = buf;
    // Copies each row into the buffer, and adds the newline at the end
    for(j = 0; j < E.numrows; j++){
        memcpy(p, E.row[j].chars, E.row[j].size); 
        p += E.row[j].size;
        *p = '\n';
        p++; // after adding '\n' go to next line
    }

    return buf;
}


void editorOpen(char* filename){
    free(E.filename);
    E.filename = strdup(filename);

    // open the given file
    FILE* fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    // while loop through each row in the FILE.
    while((linelen = getline(&line, &linecap, fp)) != -1){ // not errored
        while (linelen > 0 && (line[linelen-1] == '\n' ||
                              line[linelen-1] == '\r'))
        linelen--; // decrease the row's length to not use '\n' & '\r' which are the end

        // line - our char row from the file | linelen - length of line row
        editorAppendRow(line, linelen);
    }

    free(line);
    fclose(fp);

}

void editorSave(){
    if (E.filename == NULL) return; // If no file was opened

    int len;
    char *buf = editorRowsToString(&len); // get the string with full file contents

    // open the file
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    // error checking
    if(fd != -1){
        // truncate the file by increasing or decreasing depending on the length len
        if(ftruncate(fd, len) != -1){ // if truncating was successful
            if(write(fd, buf, len) == len){ // if write successful
                close(fd);
                free(buf);
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/***  Dynamic string functions  ***/

// appends the given message from the given s string with the length len
// into our dynamic string array ab
void abAppend(struct abuf *ab, const char *s, int len) {
    // reallocate memory fr the already existing ab pointer
    // given the string length. We just the given length with ab's length
    // This is a temporary variable
    char *new = realloc(ab->b, ab->len + len);

    // if realloc fails, return.
    if (new == NULL) return;

    // so new has the old characters from the previous append
    // we don't touch them, what we do is we travel to our wanted
    // pointer position, and copy the string s starting from that point
    memcpy(new + ab->len, s, len);
    ab->b = new; // we copied the string s to new, so we have to remap ab->b to new
    ab->len += len; // increase the length of the string (array)

    return;
}

void abFree(struct abuf *ab) {
    free(ab->b); // self-explanatory
    return;
}

/*** Functions for terminal output ***/

void editorScroll(){
    E.rx = 0;
    if (E.cy < E.numrows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }

    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if(E.rx < E.coloff){
        E.coloff = E.rx;
    }
    if(E.rx >= E.coloff + E.screencols){
        E.coloff = E.rx - E.screencols + 1;
    }
}


void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff; //
        if(filerow >= E.numrows){
            // If we are on 1/3 part of the screen and there file has no contents
            // print the welcome message
            if(E.numrows == 0 && y == E.screenrows / 3){
                char welcome[128];
                int welcomeLen = snprintf(welcome, sizeof(welcome),
                "Texit editor -- version %s",
                TEXIT_VERSION); // welcome message
                // message shouldn't exceed the terminal column size
                if(welcomeLen > E.screencols) welcomeLen = E.screencols;

                // Centering the welcome message
                // padding - the amount of spaces we add till we append our message
                int padding = (E.screencols - welcomeLen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomeLen);
            }
            else{
                abAppend(ab, "~", 1);
            }
        }
        else{ // If we have a file with contents, then print those
            // We subtract so that we don't cut the row contents halfway
            int len = E.row[filerow].rsize - E.coloff;
            if(len < 0) len = 0; // If we scroll past the row's content/chars
            if(len > E.screencols) len = E.screencols;

            // Append the specific row, starting from the specific character
            // This is bcos the screen may not be able to hold
            // the full content of the row, so when we scroll we have to
            // adjust from which character the row's contents will be displayed
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }

        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

// Prints useful info such as the filename, how many lines are in the file etc.
// As of now it will permanently occupy the last line/row on our terminal screen
void editorDrawStatusBar(struct abuf *ab){
    abAppend(ab, "\x1b[7m", 4); // ESC sequence instruction to change to inverted colors
    char status[80], rstatus[80]; // our buffer to store various info

    // the string length of status (strlen) after writing the message into the buffer
    // Copies the filename and amount of lines in the file. IF no file --> [No Name] & 0 lines
    int len = snprintf(status, sizeof(status), "%.20s - %d lines",
        E.filename ? E.filename : "[No Name]", E.numrows);
    // Copies on which line out of all the lines our cursor currently lies on
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
        E.cy + 1, E.numrows);
    if (len > E.screencols) len = E.screencols; // in case length is longer than colnum
    abAppend(ab, status, len);


    /* After printing the first status string, we want to keep printing spaces
    until we get to the point where if we printed the second status string,
    it would end up against the right edge of the screen. That happens when
    E.screencols - len is equal to the length of the second status string.
    At that point we print the status string and break out of the loop,
    as the entire status bar has now been printed. */
    while (len < E.screencols) {
        if (E.screencols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        }
        else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3); // return back to normal colors
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);

    if (msglen > E.screencols) msglen = E.screencols;
    if (msglen && time(NULL) - E.statusmsg_time < 5)
        abAppend(ab, E.statusmsg, msglen);
}

void clearScreen(){
    write(STDOUT_FILENO, "\x1b[2J", 4); // Clear terminal display
    write(STDOUT_FILENO, "\x1b[H", 3); // Put cursor to the beginning
}

void editorRefreshScreen(){
    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // hide the cursor --> no potential flickering effect

    abAppend(&ab, "\x1b[H", 3); // put cursor to the beginning

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32]; // our string buffer
    // puts the cursor at the position stored in the global editorConfig struct E
    // we + 1 both cy and cx, bcos \x1b[%d;%dH doesn't take zeros as arguments
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
                                              (E.rx - E.coloff) + 1);

    abAppend(&ab, buf, strlen(buf)); // using strlen we find the actual length of the buf string

    abAppend(&ab, "\x1b[?25h", 6); // show the cursor back again


    write(STDOUT_FILENO, ab.b, ab.len); // one write call instead of multiple ones
    abFree(&ab);
}

// sets the editor status message, and updates the status msg time
void editorSetStatusMessage(const char *fmt, ...){
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/*** Functions to process input  ***/

// Function responsible for the primitives up, down, left, right moves
void editorMoveCursor(int key) {
    erow* row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    // now we can move the cursor left, up, down, right using awsd.
    switch (key) {
        // the if checks are so the cursor doesn't end up getting out of the screen
        case ARROW_LEFT:
            if(E.cx != 0){
                E.cx--;
            }
            // pressing ← at the beginning of the line will move
            // the cursor to the end of the previous line.
            else if(E.cy > 0){
                E.cy--; // decrease cy --> go to previous line
                E.cx = E.row[E.cy].size; // set cx to the end of row's chars
            }
            break;
        case ARROW_RIGHT:
            // If there is a row, and we don't go over its size
            if(row && E.cx < row->size){
                E.cx++;
            }
            else if(row && E.cx == row->size){ // snapback
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if(E.cy != 0){
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if(E.cy < E.numrows){
                E.cy++;
            }
            break;
    }

    // Defensive prevention
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy]; // Check again if the row is real
    int rowlen = row ? row->size : 0; // if it is get its length, not set len to zero
    if (E.cx > rowlen) { // if the cx file coordiante exceeds the length snap it back
        E.cx = rowlen;  // by setting the coordinate to the length of the file
    }
}

void editorProcessKeypress(){
    int c = editorReadKey(); // returns the key that was read
    // arrow keys

    switch (c) {
        case '\r': // Enter key
            /* TODO*/
            break;

        case CTRL_KEY('q'): //exit if ctrl+Q is inputted
            clearScreen();
            exit(0);
            break;

        case CTRL_KEY('S'):
            editorSave();
            break;

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            if(E.cy < E.numrows)
                E.cx = E.row[E.cy].size;

            break;

        case BACKSPACE:
        case CTRL_KEY('h'): // sends decimal 8, which is what BackSpace would originally send back in the day
        case DEL_KEY:      // but for whatever reason, in modern computers BackSpace key is mapped to 127
            /* TODO*/
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                // If page up --> put the cursor to the top of the screen
                if (c == PAGE_UP) {
                    E.cy = E.rowoff;
                } // If page down --> put the cursor to the bottom of the screen
                else if (c == PAGE_DOWN) {
                    E.cy = E.rowoff + E.screenrows - 1;
                    if (E.cy > E.numrows) E.cy = E.numrows;
                }

                // Equate page-up / page-down amount to rows of the terminal screen
                int times = E.screenrows;
                while(times--){ // while times != 0  -->  times -= 1
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;

        case CTRL_KEY('l'): // responsible for refreshing screen, not needed as we already do that
        case '\x1b': // escape char 
            break;
        
        default:
            editorInsertChar(c);
            break;
    }
}


/*** Main ***/

// initializes the Editor
void initEditor() {
    // initializing the x and y positions for the cursor to use later
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;

    // pass the E.screenrows and E.screencols for them to get filled with correct values
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) // checks if errored
        die("getWindowSize");

    E.screenrows -= 2;
}

int main(int argc, char *argv[]){
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
