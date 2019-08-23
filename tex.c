#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}

#define ARROW_UP write(STDIN_FILENO, "\x1b[A", 4)
#define ARROW_DOWN write(STDIN_FILENO, "\x1b[B", 4)
#define ARROW_RIGHT write(STDIN_FILENO, "\x1b[C", 4)
#define ARROW_LEFT write(STDIN_FILENO, "\x1b[D", 4)

/*--------data-------------*/
FILE *file;

struct editorConfig
{
	int cx;
	int cy;
	int curr_row;
	int writtenrows;
	struct abuf *curr_buff;
	int lastpage;
	int rowsreserved;
	int screenrows;
	int screencols;
	struct termios orig_termios;
};
struct editorConfig E;
 
struct abuf
{
	char *b;
	int len;
};


/*--------abuf---------*/
void AbufAppend(struct abuf *buf, const char *s, int length)
{
	char *new_str = (char*)realloc(buf->b, buf->len + length);

	if(new_str == NULL)
		return;
	
	memcpy(&new_str[buf->len], s, length);
	buf->b = new_str;
	buf->len += length;
}

void AbufFree(struct abuf *buf)
{
	free(buf->b);
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

/*-----------file i/o----------*/
void openfile(char *filename)
{
	file = fopen(filename, "r");

	char *line = NULL;
	int n=0;

	size_t linelen = 0;

	if(!file)
		die("fopen");


	while(n != -1 && E.writtenrows <= E.screenrows-1)
	{
		if(E.writtenrows == E.rowsreserved-1)
		{
			E.rowsreserved *= 2;
			E.curr_buff = (struct abuf*)realloc(E.curr_buff, E.rowsreserved * sizeof(struct abuf));
		}

		int n = getline(&line, &linelen, file);

		if(n == -1)
			break;
		if(E.writtenrows == E.screenrows-1)
			n--;

		AbufAppend(&E.curr_buff[E.writtenrows], "\x1b[K", 3);
		AbufAppend(&E.curr_buff[E.writtenrows], line, n);
		AbufAppend(&E.curr_buff[E.writtenrows], "\r", 1);
		write(STDIN_FILENO, E.curr_buff[E.writtenrows].b, E.curr_buff[E.writtenrows].len);
		E.writtenrows++;
	}

	write(STDIN_FILENO, "\x1b[H", 3);

}

/*-----------output----------*/
void editorDrawRows(struct abuf *ab)
{
	int y;

	for(y = 0;y < E.screencols;y++)
	{
		AbufAppend(ab, "~", 1);

		if(y < E.screencols-1)
			AbufAppend(ab, "\r\n", 2);
	}
}

void editorRefreshScreen()
{
	struct abuf ab = ABUF_INIT;

	AbufAppend(&ab, "\x1b[?25l", 6);
	AbufAppend(&ab, "\x1b[2J", 4);
	AbufAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);
	AbufAppend(&ab, "\x1b[H", 3);

	AbufAppend(&ab, "\x1b[?25h", 6);
	write(STDIN_FILENO, ab.b, ab.len);
	AbufFree(&ab);
}

void editorScroll(int direction)
{
	struct abuf buff = ABUF_INIT;
	size_t linelen = 0;
	char *line = NULL;
	int n = 0, lines=0;

	if(direction)
	{
		E.curr_row = E.curr_row+E.screenrows;
		editorRefreshScreen();
		write(STDIN_FILENO, "\x1b[H", 3);

		while(E.writtenrows < E.curr_row+E.screenrows && E.lastpage != E.curr_row)
		{
			if(E.writtenrows == E.rowsreserved-1)
			{
				E.rowsreserved *= 2;
				E.curr_buff = (struct abuf*)realloc(E.curr_buff, E.rowsreserved * sizeof(struct abuf));
			}

			n = getline(&line, &linelen, file);

			if(n == -1)
			{
				E.lastpage = E.curr_row;
				break;
			}
			if(E.writtenrows == E.curr_row+E.screenrows-1)
				n--;

			AbufAppend(&E.curr_buff[E.writtenrows], "\x1b[K", 3);
			AbufAppend(&E.curr_buff[E.writtenrows], line, n);
			AbufAppend(&E.curr_buff[E.writtenrows], "\r", 1);
			AbufAppend(&buff, E.curr_buff[E.writtenrows].b, E.curr_buff[E.writtenrows].len);
			E.writtenrows++;
			lines++;
		}
		if(!lines && E.lastpage == -1)
		{
			while(lines < E.screenrows)
			{
				AbufAppend(&buff, E.curr_buff[lines+E.curr_row].b, E.curr_buff[lines+E.curr_row].len);
				lines++;
			}
		}
		if(!lines && E.lastpage != -1)
		{
			while(lines < E.writtenrows-E.curr_row)
			{
				AbufAppend(&buff, E.curr_buff[lines+E.curr_row].b, E.curr_buff[lines+E.curr_row].len);
				lines++;
			}
		}

		write(STDIN_FILENO, buff.b, buff.len);
		E.cy = 0;
		write(STDIN_FILENO, "\x1b[H", 3);
	}
	else
	{
		if(E.curr_row < E.screenrows)
			return;

		E.curr_row = E.curr_row-E.screenrows;
		write(STDIN_FILENO, "\x1b[H", 3);

		while(lines < E.screenrows)
		{
			AbufAppend(&buff, E.curr_buff[lines+E.curr_row].b, E.curr_buff[lines+E.curr_row].len);
			lines++;
		}

		write(STDIN_FILENO, buff.b, buff.len);
		E.cy = E.screenrows-1;
	}
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
			if(E.cy > 0)
			{
				E.cy--;
				ARROW_UP;
			}
			else
			{
				editorScroll(0);
			}

			break;
		case CTRL_KEY('j'):
			if(E.cy < E.screenrows-1 && (E.lastpage == -1 || E.cy < E.writtenrows-E.curr_row-1))
			{
				E.cy++;
				ARROW_DOWN;
			}
			else if(E.curr_row+E.cy < E.lastpage || E.lastpage == -1)
			{
				editorScroll(1);
			}
			 
			break;
		case CTRL_KEY('h'):
			if(E.cx > 0)
			{
				E.cx--;
				ARROW_LEFT;
			}
 
			break;
		case CTRL_KEY('l'):
			if(E.cx < E.screencols-1 && E.cx < E.curr_buff[E.curr_row+E.cy].len-5)
			{
				E.cx++;
				ARROW_RIGHT;
			}
 
			break;
		default:
			E.cx++;
			write(STDIN_FILENO, &c, 1);
			break;
	}
}

/*-----------init----------*/

void initEditor()
{
	if(getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");

	E.cx = 0;
	E.cy = 0;
	E.curr_row = 0;
	E.writtenrows = 0;
	E.lastpage = -1;
	E.rowsreserved = 10;

	E.curr_buff = (struct abuf*)malloc(E.rowsreserved * sizeof(struct abuf));
}
int main(int argc, char *argv[])  
{
	enableRawMode();
	initEditor();

	editorRefreshScreen();

	if(argc > 1)
		openfile(argv[1]);

	while(1)
	{
		editorProcessKeyPress();
	}
	return 0;
}
