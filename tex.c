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
/*
  b       := string data
  len     := length of string data
  max_len := maximum length allocated to the string
 */
typedef struct
{
    char *b;
    int len;
    int max_len;
} abuf;
/*
  x                           := current cursor x position
  y                           := current cursor y position
  screenrows                  := terminal screen maximum rows
  screencols                  := terminal screen maximum cols
  active_screen_content       := array of abufs each abuf a row
  content_rows                := number of rows
  content_alloc_rows          := maximum number of rows allocated
  active_screen_content_dirty := if current buffer dirty then 1
  active_screen_filename      := buffer filename
  orig_termios                := original terminal flags
 */
struct editorConfig
{
    int x;
    int y;
    int cur_y;
    int first_row;
    int screenrows;
    int screencols;
    abuf *active_screen_content;
    int content_rows;
    int content_alloc_rows;
    int active_screen_content_dirty;
    char *active_screen_filename;
    struct termios orig_termios;
};
struct editorConfig E;


void scroll_screen(int direction)
{
    printf("Scroll in %d direction", direction);
}

/*--------abuf---------*/
void abuf_append(abuf *buf, char *s, int length)
{
    if (buf->len + length > buf->max_len)
    {
        int size_increase = buf->max_len + 1;
        while (buf->len + length > buf->max_len + size_increase)
            size_increase *= 2;

        char *new_str = (char*)realloc(buf->b, buf->max_len + size_increase);
        buf->max_len += size_increase;
        buf->b = new_str;
    }

    memcpy(&buf->b[buf->len], s, length);
    buf->len += length;
}

void new_line()
{
    E.x = 0;
    E.y++;
    write(STDIN_FILENO, "\x1b[K", 4);
    write(STDIN_FILENO, "\n\r", 2);
    if (E.content_rows >= E.content_alloc_rows)
    {
        E.content_alloc_rows *= 2;
        E.active_screen_content = (abuf*)realloc(E.active_screen_content, E.content_alloc_rows*sizeof(abuf));
        memset(&E.active_screen_content[E.content_rows], 0, (E.content_alloc_rows-E.content_rows)*sizeof(abuf));
    }
    E.content_rows++;
}

void insert_char(char c)
{
    if (E.x >= E.active_screen_content[E.y].len)
        abuf_append(&E.active_screen_content[E.y], &c, 1);
    else
        E.active_screen_content[E.y].b[E.x] = c;

    E.x++;
    if (E.x%E.screencols == 0)
        E.cur_y++;

    write(STDIN_FILENO, &c, 1);
}

/*--------fileops----------*/
void write_file()
{
    int fd = open(E.active_screen_filename, O_RDWR | O_CREAT, 0664);
    abuf *b = E.active_screen_content;
    for (int i=0; i<E.content_rows; i++)
    {
        write(fd, b->b, b->len);
        write(fd, "\n", 1);
        b++;
    }
    close(fd);
}

void open_file()
{
    char *buff;

    E.cur_y = -1;                                                                    // If file has only 1 line it gets incremented to 0
    int fd = open(E.active_screen_filename, O_RDONLY);

    off_t size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    buff = (char*)malloc(size+1);

    read(fd, buff, size);

    for (int i=0; i<size;)
    {
        int nl = i;
        while(buff[nl] != '\n' && nl<size)
            nl++;
        abuf_append(&E.active_screen_content[E.content_rows-1], &buff[i], nl-i);
        E.cur_y += (nl - i) / E.screencols + 1;
        if (E.cur_y < E.screenrows-1)
        {
            write(STDIN_FILENO, &buff[i], nl-i);
            E.x += nl-i;
            if (buff[nl] == '\n')
                new_line();
        }
        i = nl+1;
    }
    E.x = 0;
    E.y = 0;
    E.cur_y = 0;
    write(STDIN_FILENO, "\x1b[H", 3);

    close(fd);
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

void editor_update_status_bar()
{
    char bottom[5], status_bar[20];
    sprintf(bottom, "\x1b[%dB", E.screenrows);
    sprintf(status_bar, "(%d, (%d, %d))", E.x, E.y, E.cur_y);

    write(STDIN_FILENO, "\x1b[s", 3);
    write(STDIN_FILENO, "\x1b[H", 3);
    write(STDIN_FILENO, bottom, 5);
    write(STDIN_FILENO, "\x1b[K", 4);
    write(STDIN_FILENO, status_bar, strlen(status_bar));
    write(STDIN_FILENO, "\x1b[u", 3);
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

void editorDrawFreshScreen()
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
            if (E.active_screen_content_dirty == 1)
                write_file();

            write(STDIN_FILENO, "\x1b[2J", 4);
            write(STDIN_FILENO, "\x1b[H", 3);
            printf("%d", E.cur_y);
            exit(0);
            break;
        case CTRL_KEY('k'):
            if (E.cur_y != 0)
            {
                ARROW_UP;
                E.cur_y--;
                if (E.active_screen_content[E.y].len > E.screencols)
                {
                    if (E.x >= E.screencols)
                        E.x -= E.screencols;
                    else
                        E.y--;
                }
                else
                {
                    E.y--;
                    if (E.active_screen_content[E.y].len > E.screencols)
                        E.x += (E.active_screen_content[E.y].len / E.screencols) * E.screencols;
                }

                while (E.x > E.active_screen_content[E.y].len)
                {
                    E.x--;
                    ARROW_LEFT;
                }
            }
            break;
        case CTRL_KEY('j'):
            if (E.y < E.screenrows && E.y < E.content_rows-1)
            {
                ARROW_DOWN;
                E.cur_y++;
                if (E.active_screen_content[E.y].len > E.screencols)
                {
                    if (E.x+E.screencols < (E.active_screen_content[E.y].len/E.screencols+1)*E.screencols)
                        E.x += E.screencols;
                    else
                    {
                        while(E.x >= E.screencols)
                            E.x -= E.screencols;
                        E.y++;
                    }
                }
                else
                {
                    E.y++;
                }

                while (E.x > E.active_screen_content[E.y].len)
                {
                    E.x--;
                    ARROW_LEFT;
                }
            }
            break;
        case CTRL_KEY('h'):
            if (E.x != 0)
            {
                if (E.active_screen_content[E.y].len > E.screencols && E.x == (E.x/E.screencols)*E.screencols)
                {
                    E.cur_y--;
                    ARROW_UP;
                    for (int i=0; i<E.screencols; i++)
                        ARROW_RIGHT;
                }
                else
                    ARROW_LEFT;

                E.x--;
            }
            break;
        case CTRL_KEY('l'):
            if (E.x < E.active_screen_content[E.y].len)
            {
                if (E.active_screen_content[E.y].len > E.screencols && E.x == (E.x/E.screencols+1)*E.screencols-1)
                {
                    E.cur_y++;
                    ARROW_DOWN;
                    for (int i=0; i<((E.x+1)/E.screencols)*E.screencols; i++)
                        ARROW_LEFT;
                }
                else
                    ARROW_RIGHT;

                E.x++;
            }
            break;
        case '\r':
            E.active_screen_content_dirty = 1;
            E.cur_y++;
            new_line();
            break;
        default:
            E.active_screen_content_dirty = 1;
            insert_char(c);
            break;
    }
}

/*-----------init----------*/

void initEditor()
{
    if(getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");

    E.active_screen_content = (abuf*)malloc(sizeof(abuf));
    *E.active_screen_content = (abuf) ABUF_INIT;
    E.x = 0;
    E.y = 0;
    E.first_row = 0;
    E.cur_y = 0;
    E.content_rows = 1;
    E.content_alloc_rows = 1;
    E.active_screen_content_dirty = 0;
    E.active_screen_filename = (char*)malloc(20*sizeof(char));
}
int main(int argc, char *argv[])
{
    enableRawMode();
    initEditor();
    if (argc > 1)
        strcpy(E.active_screen_filename, argv[1]);
    else
        strcpy(E.active_screen_filename, "/tmp/editfile");

    editorDrawFreshScreen();
    open_file();

    while(1)
    {
        editor_update_status_bar();
        editorProcessKeyPress();
    }
    return 0;
}
