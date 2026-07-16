#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

//defines
#define CTRL_KEY(k) ((k) & 0x1f)

//data
struct editor_config {
	int screen_rows;
	int screen_cols;
	struct termios orig_termios;
	
};

struct editor_config E;

//terminal

void die(const char *s) {
	write(STDOUT_FILENO,"\x1b[2J",4);
	write(STDOUT_FILENO,"\x1b[H",3);

	perror(s);
	exit(1);
}

void disable_rawmode() {
	if (tcsetattr(STDIN_FILENO,TCSAFLUSH,&E.orig_termios)==-1) {
		die("tcsetattr");
	}
}

void enable_rawmode() {
	//get terminal input
	if (tcgetattr(STDIN_FILENO,&E.orig_termios)==-1){
		die("tcgetattr");
	} 
	atexit(disable_rawmode); //when program exit, disable raw mode

	
	struct termios raw = E.orig_termios;

	raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | ICRNL | IXON); //turn off ctrl-s and ctrl-q, and translate '\r' to '\n'
	raw.c_oflag &= ~(OPOST); //turn off output processing，'\r\n' to '\n'

	raw.c_cflag |= (CS8);
	//turn off echo characters,turn off canonical mode,turn off ctrl-c and ctrl-z signals
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN| ISIG); 

	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH,&raw)) {
		die("tcsetattr");
	}
}

char editor_readkey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO,&c,1))!=1) {
		if (nread==-1 && errno != EAGAIN) {
			die("read");
		}
	}

	return c;
}

int get_cursor_position(int *rows,int *cols) {
	char buf[32];
	unsigned int i =0;
	
	if (write(STDOUT_FILENO,"\xb[6n",4)!=4) {
		return -1;
	}

	while (i<sizeof(buf)-1) {
		if (read(STDIN_FILENO,&buf[i],1)!=1) {
			break;
		}
		if (buf[i]=='R') {
			break;
		}
		i++;
	}
	buf[i] = '\0';

	//non-escape sequence
	if (buf[0] != '\x1b' || buf[1] !='[') {
		return -1;
	}
	//parse the cursor position from escape sequence
	//format: <esc>[<rows>;<cols>
	if (sscanf(&buf[2],"%d;%d",rows,cols)!=2) {
		return -1;
	}
	
	return 0;
}

int get_window_size(int *rows,int *cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws)==-1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO,"\x1b[999C\x1b[999B",12)!=12) {
			return -1;
		}
		return get_cursor_position(rows,cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}

	return -1;
}


// append buffer
struct abuf {
	char *b;
	int len;
};

#define ABUF_INIT {NULL,0}

//output
void editor_draw_rows() {
	int y;
	for(y=0;y<E.screen_rows;y++){
		write(STDOUT_FILENO,"~",1);

		if (y < E.screen_rows-1) {
			write(STDOUT_FILENO,"\r\n",2);
		}
	}
}

void editor_refresh_screen() {
	write(STDOUT_FILENO,"\x1b[2J",4);
	write(STDOUT_FILENO,"\x1b[H",3);

	editor_draw_rows();

	write(STDOUT_FILENO,"\x1b[H",3); //cursor move to the top-left position
}




//input
void editor_process_keypress() {
	char c = editor_readkey();

	switch (c) {
		case CTRL_KEY('q'): //mapping ctrl-q to exit
			write(STDOUT_FILENO,"\x1b[2J",4);
			write(STDOUT_FILENO,"\x1b[H",3);
			exit(0);
			break;
	}
}

//init
void init_editor() {
	if (get_window_size(&E.screen_rows,&E.screen_cols)==-1) {
		die("get_window_size");
	}
}

int main() {

	enable_rawmode();
	init_editor();

	while (1) {
		editor_refresh_screen();
		editor_process_keypress();
	}

    
    return 0;
}
