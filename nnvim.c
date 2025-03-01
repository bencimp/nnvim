// includes
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

//defines
#define CTRL_KEY(k) ((k) & 0x1f)
#define NNVIM_VERSION "0.0.1"
#define ABUF_INIT {NULL, 0}

// variables

// structs
struct editorConfig E;

struct termios orig_termios;

typedef struct {
    char *b;
    int len;
} abuf;

typedef struct {
    int len;
    char *row;
} erow;

struct editorConfig {
    int cx;
    int cy;
    int numrows;
    int terminalRows;
    int terminalCols;
    int scrollRow;
    int scrollCol;
    erow *rows;
    struct termios orig_termios;
};

// enums
enum editorKey{
    ARROW_LEFT = 1917,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
};

// function signatures
int editorReadKey();

int getWindowSize(int *rows, int *cols);
int getCursorPosition(int *rows, int *cols);

void abAppend(abuf *buf, const char* in, int lenIn);
void abFree(abuf *buf);
void die(const char *s);
void disableRawMode();
void editorAppendRow(char *s, size_t len);
void editorDrawRows(abuf *buf);
void editorOpenFile(char* filename);
void editorProcessKeypressViewMode();
void editorRefreshScreen();
void editorSetOffset();
void enableRawMode();
void initEditor();
void moveCursor(int x, int y);

// functions
int editorReadKey(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b'){
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '['){
            if (seq[1] >= '0' && seq[1] <= '9'){
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]){
                        case '1':
                            return HOME_KEY;
                        case '3':
                            return DEL_KEY;
                        case '4':
                            return END_KEY;
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                        case '7':
                            return HOME_KEY;
                        case '8':
                            return END_KEY;
                    }
                }
            }
            else if (seq[0] == 'O') {
                switch (seq[1]){
                    case 'H':
                        return HOME_KEY;
                    case 'E':
                        return END_KEY;
                }
            }
            else{
                switch(seq[1]){
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'E': return END_KEY;
                }
            }
        }
        return '\x1b';
    }
    else{
        return c;
    }
}

int getCursorPosition(int *rows, int *cols){
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) printf("hit line 54");
    char c;
    char buf[32];
    unsigned int i = 0;
    while (i < sizeof(buf) - 1) {
        // printf("reading");
        if (read(STDIN_FILENO, &c, 1) != 1) break;
        printf("%d %c\r\n", c, c);
        buf[i] = c;
        if (c == 'R'){
            buf[i+1] = '\0';
            break;
        }
        i ++;
    }
    sscanf(&buf[2], "%d;%dR", rows, cols);
    return 0;
}

int getWindowSize(int *rows, int *cols){
    struct winsize ws; 
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    }
    else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
    return -1;
}

void abAppend(abuf *buf, const char* in, int lenIn){
    char *new = realloc(buf->b, buf->len + lenIn);

    // ensure we got a return 
    if (new == NULL) return;
    memcpy(&new[buf->len], in, lenIn);
    buf->b = new;
    buf->len += lenIn;
}

void abFree(abuf *buf){
    free(buf->b);
}

void die(const char *s){
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode(){
    if (tcsetattr(STDERR_FILENO, TCSAFLUSH, &E.orig_termios) == -1){
        die("tcsetattr");
    }
}

void editorAppendRow(char *s, size_t len){
    E.rows = realloc(E.rows, sizeof(erow) * (E.numrows + 1));

    // append new row from input, null-terminate it
    int pos = E.numrows;
    E.rows[pos].len = len;
    E.rows[pos].row = malloc(len + 1);
    memcpy(E.rows[pos].row, s, len);
    E.rows[pos].row[len] = '\0';
    E.numrows ++;
}

void editorDrawRows(abuf *buf){
    int printOffset;
    for (int y = 0; y < E.terminalRows; y++){
        printOffset = y + E.scrollRow;
        if (y != E.terminalRows - 1) abAppend(buf, "~", 1);
        if (printOffset >= E.numrows){
            if (y == E.terminalRows - 3){
                char xPos[10];
                int xPosLen = snprintf(xPos, sizeof(xPos), "x:%d", E.cx);
                abAppend(buf, xPos, xPosLen);
            }
            if (y == E.terminalRows - 2){
                char yPos[10];
                int yPosLen = snprintf(yPos, sizeof(yPos), "y:%d", E.cy);
                abAppend(buf, yPos, yPosLen);
            }
            if (y == E.terminalRows - 1){
                int count = E.terminalCols;
                while (count --){
                    abAppend(buf, "\u2588", 4);
                }
            }
            if (y == E.terminalRows/3 && E.numrows == 0){
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "NNVIM %s", NNVIM_VERSION);
                if (welcomelen > E.terminalCols) welcomelen = E.terminalCols;
                int diffCalc = (E.terminalCols - welcomelen) / 2;
                while (diffCalc >= 0){
                    diffCalc --; 
                    abAppend(buf, " ", 1);
                }
                abAppend(buf, welcome, welcomelen);
            }
        }
        else {
            int len = E.rows[printOffset].len - E.scrollCol;
            if (len < 0) len = 0;
            if (len > E.terminalCols) len = E.terminalCols;
            abAppend(buf, &E.rows[printOffset].row[E.scrollCol], len);
        }
        
        abAppend(buf, "\x1b[K", 3);
        if (y < E.terminalRows - 1){
            abAppend(buf, "\r\n", 2);
        }
    }
}

void editorOpenFile(char* filename){
    FILE *file = fopen(filename, "r");
    if (!file) die("fopen");
    
    char* line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, file)) != -1) {        
        while (linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r')){
            linelen --;
        }
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(file); 
}

void editorProcessKeypressViewMode(){
    int c = editorReadKey();
    switch (c){
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        // move the cursor to the left
        case 'j':
            moveCursor(-1, 0);
            break;
        // move the cursor to the right
        case 'k':
            moveCursor(1, 0);
            break;
        // move the cursor to down
        case 'l':
            moveCursor(0, 1);
            break;
        // move the cursor to up
        case ';':
            moveCursor(0, -1);
            break;
    }
}

void editorRefreshScreen(){
    write(STDOUT_FILENO, "\x1b[?25l", 6);
    abuf buf = ABUF_INIT;
    abAppend(&buf, "\x1b[H", 3);

    editorDrawRows(&buf);

    abAppend(&buf, "\x1b[H", 3);
    char cBuf[32];
    snprintf(cBuf, sizeof(cBuf), "\x1b[%d;%dH", E.cy+1, E.cx+1);
    abAppend(&buf, cBuf, strlen(cBuf));
    write(STDOUT_FILENO, buf.b, buf.len);
    write(STDOUT_FILENO, "\x1b[?25h", 6);
    abFree(&buf);
}

void editorSetOffset(){
    
}

void enableRawMode(){
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1){
        die("tcsetattr");  
    }
    atexit(disableRawMode);
    struct termios raw = E.orig_termios;
    tcgetattr(STDIN_FILENO, &raw);
    
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr:");
}

void initEditor(){
    if (getWindowSize(&E.terminalRows, &E.terminalCols) == -1) die("getWindowSize");
    E.cx = 1;
    E.cy = 0;
    E.numrows = 0;
    E.scrollCol = 0;
    E.scrollRow = 0;
    write(STDOUT_FILENO, "\x1b[2J", 4);
}

void moveCursor(int x, int y){
    // this code is futureproofed in case I want to implement a "go fast in a direction in a line" that isn't just snapping to part of a line.
    // if x > 0, we want to move the cursor right (i.e., away from x=0, which is the left side of the screen)
    if (x > 0){
        // if we would move off the right of the current screen
        if (E.cx + 1 > E.terminalCols - 1){
            // adjust scrollCol here
            // if (E.cy <= (E.terminalRows - 1)){
            //     E.cx = 1;
            //     E.cy ++;
            // } this code commented out for now since we aren't adding a hard edge limit, you can position the cursor outside of the file
            E.scrollCol ++;
        }
        else {
            E.cx ++;
        }
    }
    // if x < 0, we want to move the cursor left (i.e., towards x=0, which is the left side of the screen)
    if (x < 0){
        // if we would move off the left of the current screen
        if (E.cx - 1 < 1){
            // adjust scrollCol here
            // if (E.cy > 0) E.cx = E.terminalCols - 1;
            // E.cy = (E.cy - 1 < 0) ? 0 : E.cy - 1; one, why did I write it this way, 2, commented out for now as we don't care about left to right scrolling
            if (E.scrollCol > 0) E.scrollCol --;
        }
        else {
            E.cx --;
        }
    }
    // if y > 0, we want to move the cursor down (i.e., away from y=0, which is the top of the screen)
    if (y > 0){
        // if we would move off the bottom of the current screen (this is -2 instead of -1 so that we don't scroll onto the status bar)
        if (E.cy + 1 <= (E.terminalRows - 2)){
            E.cy ++;
        }
        else {
            E.scrollRow ++;
        }
    }
    // if y < 0, we want to move the cursor up (i.e., towards y=0, which is the top of the screen)
    if (y < 0){
        // if we would move off the top of the current screen
        if (E.cy - 1 >= 0){
            // adjust scrollRow here
            E.cy --;
        }
        else {
            if (E.scrollRow > 0) E.scrollRow --;
        }
    }
}

// main function
int main(int argc, char* argv[]){
    enableRawMode();
    initEditor();
    
    if (argc >= 2) {
        editorOpenFile(argv[1]);
    }

    while (1){
        editorRefreshScreen();
        editorProcessKeypressViewMode();
    }
    return 0;
}