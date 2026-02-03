#include <termios.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/ioctl.h>

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

// hex 0x1f = 0001 1111 (in binary) = 31 (in decimal)
#define CTRL_KEY(k) ((k) & 0x1f) // Simple macro for better understanding

// Convenient struct to store everything related to our terminal settings
struct editorConfig {
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

// 'dynamic' string struct
struct abuf {
    char *b;
    int len;
};
// constructor for our abuf struct
#define ABUF_INIT {NULL, 0} // pointer to null, length = 0

void die(const char *s);
void disableRawMode();
void enableRawMode();
char editorReadKey();
void editorDrawRows();
void clearScreen();
void editorRefreshScreen();
void editorProcessKeypress();
int getWindowSize(int *rows, int *cols);


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
    
    raw.c_cc[VMIN] = 0; // Minimum amount of bytes until read returnss
    raw.c_cc[VTIME] = 5; // wait 0.4 seconds for input

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr"); // sets terminal settings
}

char editorReadKey() {
    int nread; // variable to store the return of read
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) 
            die("read"); // if error --> print error and exit the program 
    }

    return c;
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
void editorDrawRows(struct abuf *ab) { // draws '~' VIM style 
    int y;
    for (y = 0; y < E.screenrows; y++) {
        if(y == E.screenrows / 3){ // If on 1/3 part of the screen print welcome message
            char welcome[80];
            int welcomeLen = snprintf(welcome, sizeof(welcome),
            "Texit editor -- version %s", TEXIT_VERSION); // welcome message

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

        abAppend(ab, "\x1b[K", 3);
        // The last "\r\n" causes the terminal to scroll, pushing 
        // the final ~ off the screen, so we skip printing it on the last row.
        if(y < E.screenrows - 1){
            abAppend(ab, "\r\n", 2);
        }
    }
}

void clearScreen(){
    write(STDOUT_FILENO, "\x1b[2J", 4); // Clear terminal display 
    write(STDOUT_FILENO, "\x1b[H", 3); // Put cursor to the beginning
}

void editorRefreshScreen(){
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // hide the cursor --> no potential flickering effect

    abAppend(&ab, "\x1b[H", 3); // put cursor to the beginning
    
    editorDrawRows(&ab);

    abAppend(&ab, "\x1b[H", 3); // put cursor back after drawing '~'
    abAppend(&ab, "\x1b[?25h", 6); // show the cursor back again

    
    write(STDOUT_FILENO, ab.b, ab.len); // one write call instead of multiple ones
    abFree(&ab);
}


/*** Functions to process input  ***/
void editorProcessKeypress(){
    char c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'): //exit if ctrl+Q is inputted
            clearScreen();
            exit(0);
            break;
    }
}

/*** Main ***/

// initializes the Editor
void initEditor() {
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) // checks if errored
    die("getWindowSize");
}

int main(){
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}