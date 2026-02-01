#include <termios.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

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

// hex 0x1f = 0001 1111 (in binary) = 31 (in decimal)
#define CTRL_KEY(k) ((k) & 0x1f) // Simple macro for better understanding

// Convenient struct to store everything related to our terminal settings
struct editorConfig {
    struct termios orig_termios;
};

struct editorConfig E;


// error handling function
void die(const char *s) {
    // clear screen before exiting
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

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

/*** Functions for terminal output ***/
void editorDrawRows() { // draws '~' on 24 rows. VIM style
    int y;
    for (y = 0; y < 24; y++) {
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

void editorRefreshScreen() {
    write(STDOUT_FILENO, "\x1b[2J", 4); // Clear terminal display 
    write(STDOUT_FILENO, "\x1b[H", 3); // Put cursor to the beginning
    
    editorDrawRows();
    
    write(STDOUT_FILENO, "\x1b[H", 3); // put cursor back after drawing '~'
}


/*** Functions to process input  ***/
void editorProcessKeypress() {
    char c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'): //exit if ctrl+Q is inputted
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

/*** Main ***/
int main(){
    enableRawMode();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}