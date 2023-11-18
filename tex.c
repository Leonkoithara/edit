#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT { NULL, 0, 0 }
#define ARROW_UP write(STDIN_FILENO, "\x1b[A", 4)
#define ARROW_DOWN write(STDIN_FILENO, "\x1b[B", 4)
#define ARROW_RIGHT write(STDIN_FILENO, "\x1b[C", 4)
#define ARROW_LEFT write(STDIN_FILENO, "\x1b[D", 4)

/*--------data-------------*/
typedef struct
{
    char *b;
    int len;
    int max_len;
} abuf;
struct editorConfig
{
    int screenrows;
    int screencols;
    abuf *active_screen_content;
    struct termios orig_termios;
};
struct editorConfig E;


/*--------abuf---------*/
void abuf_append(abuf *buf, char *s, int length)
{
    char *new_str = NULL;
    if (buf->len + length > buf->max_len)
    {
        int size_increase = 20;
        while (buf->len + length > buf->max_len + size_increase)
            size_increase += 20;

        new_str = (char*)realloc(buf->b, buf->max_len + size_increase);
        buf->max_len += size_increase;
        buf->b = new_str;
    }

    memcpy(&buf->b[buf->len], s, length);
    buf->len += length;
}

/*--------terminal---------*/
void die(const char *s)
{
    write(STDIN_FILENO, "\x1b[2J", 4);
    write(STDIN_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode()
{
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode()
{
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    tcgetattr(STDIN_FILENO, &raw);

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); 

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

char editorReadKey()
{
    int nread;
    char c;

    while((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if(nread == -1 && errno == EAGAIN)
            die("read");
    }

    return c;
}

int getWindowSize(int *row, int *col)
{
    struct winsize ws;

    if(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
        return -1;
    else
    {
        *row = ws.ws_row;
        *col = ws.ws_col;
    }

    return 0;
}

/*-----------output----------*/

void editorDrawRows()
{
    int y;

    for(y = 0;y < E.screencols;y++)
    {
        write(STDIN_FILENO, "~", 1);

        if(y < E.screencols-1)
            write(STDIN_FILENO, "\r\n", 2);
    }
}

void editorRefreshScreen()
{
    write(STDIN_FILENO, "\x1b[2J", 4);
    write(STDIN_FILENO, "\x1b[H", 3);

    editorDrawRows();
    write(STDIN_FILENO, "\x1b[H", 3);
}

/*-----------input----------*/

void editorProcessKeyPress()
{
    char c = editorReadKey();

    switch(c)
    {
        case CTRL_KEY('q'):
            write(STDIN_FILENO, "\x1b[2J", 4);
            write(STDIN_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case CTRL_KEY('k'):
            ARROW_UP;
            break;
        case CTRL_KEY('j'):
            ARROW_DOWN;
            break;
        case CTRL_KEY('h'):
            ARROW_LEFT;
            break;
        case CTRL_KEY('l'):
            ARROW_RIGHT;
            break;
        case '\r':
            abuf_append(E.active_screen_content, "\n\r", 2);
            write(STDIN_FILENO, "\n\r", 2);
            break;
        default:
            abuf_append(E.active_screen_content, &c, 1);
            write(STDIN_FILENO, &c, 1);
            break;
    }
}

/*-----------init----------*/

void initEditor()
{
    if(getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");

    abuf *init = (abuf*)malloc(sizeof(abuf));
    *init = (abuf) ABUF_INIT;
    E.active_screen_content = init;
}
int main()
{
    enableRawMode();
    initEditor();
    editorRefreshScreen();

    while(1)
    {
        editorProcessKeyPress();
    }
    return 0;
}
