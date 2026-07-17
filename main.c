#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

//defines
#define CTRL_KEY(k) ((k) & 0x1f)

#define YED_VERSION "0.0.1"

//data
struct editor_config {
	int cx,cy;
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

void ab_append(struct abuf *ab, const char *s,int len) {
	char *new = realloc(ab->b,ab->len+len);

	if (new==NULL) {
		return;
	}

	memcpy(&new[ab->len],s,len);
	ab->b = new;
	ab->len += len;
}

void ab_free(struct abuf *ab) {
	free(ab->b);
}

//output
void editor_draw_rows(struct abuf *ab) {
	int y;
	for(y=0;y<E.screen_rows;y++){
		if (y==E.screen_rows/3) {
			char welcome[80];
			int welcome_len=snprintf(welcome,sizeof(welcome),
		"YED editor -- version %s",YED_VERSION);

			if (welcome_len > E.screen_cols) {
				welcome_len = E.screen_cols;
			}

			int padding = (E.screen_cols - welcome_len)/2;
			if (padding) {
				ab_append(ab,"~",1);
				padding--;
			}

			while(padding--){
				ab_append(ab," ",1);
			}

			ab_append(ab,welcome,welcome_len);
		} else {
			ab_append(ab,"~",1);
		}
		
		
		ab_append(ab,"\x1b[K",3); //clear from right of the cursor side
		if (y < E.screen_rows-1) {
			ab_append(ab,"\r\n",2);
		}
	}
}

void editor_refresh_screen() {
	struct abuf ab = ABUF_INIT;
	
	ab_append(&ab,"\x1b[?25l",6); //hide cursor
	ab_append(&ab,"\x1b[H",3);//setting cursor position

	editor_draw_rows(&ab);

	char buf[32];
	snprintf(buf,sizeof(buf),"\x1b[%d;%dH",E.cy+1,E.cx+1);
	ab_append(&ab,buf,strlen(buf));

	ab_append(&ab,"\x1b[?25h",6); //show cursor

	write(STDOUT_FILENO,ab.b, ab.len); //cursor move to the top-left position
	ab_free(&ab);
}




//input
void editor_move_cursor(char key) {
	switch (key) {
		case 'a':
			E.cx--;
			break;

		case 'd':
			E.cx++;
			break;
		case 'w':
			E.cy--;
			break;
		case 's':
			E.cy++;
			break;
	}
}

void editor_process_keypress() {
	char c = editor_readkey();

	switch (c) {
		case CTRL_KEY('q'): //mapping ctrl-q to exit
			write(STDOUT_FILENO,"\x1b[2J",4);
			write(STDOUT_FILENO,"\x1b[H",3);
			exit(0);
			break;

		case 'w':
		case 's':
		case 'a':
		case 'd':
			editor_move_cursor(c);
			break;
	}
}

//init
void init_editor() {

	E.cx = 0;
	E.cy = 0;

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
