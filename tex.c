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

/*--------data-------------*/
struct editorConfig
{
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
	}
}

/*-----------init----------*/

void initEditor()
{
	if(getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
}
int main()  
{
	enableRawMode();
	initEditor();

	while(1)
	{
		editorRefreshScreen();
		editorProcessKeyPress();
	}
	return 0;
}
