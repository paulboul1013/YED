#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

//defines
#define CTRL_KEY(k) ((k) & 0x1f)

#define YED_VERSION "0.0.1"
#define YED_TAB_STOP 4

enum editor_key {
	BACKSPACE = 127,
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

//data

typedef struct erow {
	int size;
	int rsize;
	char *chars;
	char *render;
} erow;

struct editor_config {
	int cx,cy; //cursor x and y coordinate
	int rx; //render x coordinate
	int rowoff; //cursor row offset
	int coloff; //cursor column offset
	int screen_rows; //screen rows
	int screen_cols; //screen columns
	struct termios orig_termios;
	int numrows;
	erow *row;
	int dirty;
	char *filename;
	char statusmsg[80];
	time_t statusmsg_time;
};

struct editor_config E;

//prototypes
void editor_set_status_message(const char *fmt, ...);

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

int editor_readkey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO,&c,1))!=1) {
		if (nread==-1 && errno != EAGAIN) {
			die("read");
		}
	}

	if (c=='\x1b') { //escape sequence
		char seq[3];

		if (read(STDIN_FILENO,&seq[0],1)!= 1) {
			return '\x1b';
		}
		if (read(STDIN_FILENO,&seq[1],1)!=1) {
			return '\x1b';
		}

		if (seq[0]=='[') {
			if (seq[1]>='0' && seq[1]<='9') {
				if (read(STDIN_FILENO,&seq[2],1)!=1) {
					return '\x1b';
				}

				if (seq[2]=='~') { //parse page up and page down
					switch (seq[1]) {
						case '1' : return HOME_KEY;
						case '3' : return DEL_KEY;
						case '4' : return END_KEY;
						case '5' : return PAGE_UP;
						case '6' : return PAGE_DOWN;
						case '7' : return HOME_KEY;
						case '8' : return END_KEY;
					}
				}
			} else {
				switch (seq[1]) { //parse arrow keys
					case 'A' :return ARROW_UP;
					case 'B' :return ARROW_DOWN;
					case 'C' :return ARROW_RIGHT;
					case 'D' :return ARROW_LEFT;
					case 'H' : return HOME_KEY;
					case 'F' : return END_KEY;
				}
			}

		} else if (seq[0]=='O') {
			switch (seq[1]) {
				case 'H' : return HOME_KEY;
				case 'F' : return END_KEY;
			}
		}

		return '\x1b';
		
	} else {
		return c;
	}
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

//row operations
int editor_row_cx_to_rx(erow *row,int cx) {
	int rx=0;
	int j;
	for(j=0;j<cx;j++) {
		if (row->chars[j]=='\t') {
			//ex: a\t\b
			// meet a and then rx++, rx=1,next is '\t'
			// rx+=4-(1%4)=4-1=3;
			// 1+=4 -> rx=5 and then rx++ -> rx=6
			//so really the b character render index is from 6 start calculate
			rx+=YED_TAB_STOP-(rx%YED_TAB_STOP);
		}
		rx++;
	}
	return rx;
}
void editor_update_row(erow *row) {
	int tabs=0;
	int j;
	for(j=0;j<row->size;j++) {
		if (row->chars[j]=='\t') { //count the number of tabs
			tabs++;
		}
	}

	free(row->render);
	row->render = malloc(row->size+tabs*(YED_TAB_STOP-1)+1);
	
	int idx=0;
	for(j=0;j<row->size;j++){
		if (row->chars[j]=='\t') { //replace tab with spaces
			row->render[idx++] = ' ';
			while(idx%YED_TAB_STOP!=0) row->render[idx++] =' '; //render rest of tabs 
		} else{
			row->render[idx++] = row->chars[j];
		}
		
	}

	row->render[idx] = '\0';
	row->rsize = idx;
}

void editor_append_row(char *s,size_t len) {
	E.row = realloc(E.row,sizeof(erow)*(E.numrows+1));
	
	int at = E.numrows;
	E.row[at].size = len;
	E.row[at].chars = malloc(len+1);
	memcpy(E.row[at].chars,s,len);
	E.row[at].chars[len]='\0';

	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	editor_update_row(&E.row[at]);

	E.numrows++;
	E.dirty++;
}

void editor_row_insert_char(erow *row,int at,int c) {
	if (at < 0 || at > row->size) {
		at= row->size;
	}
	row->chars = realloc(row->chars,row->size+2);
	memmove(&row->chars[at+1],&row->chars[at],row->size-at+1);
	row->size++;
	row->chars[at]=c;
	editor_update_row(row);
	E.dirty++;
}


// editor operations
void editor_insert_char(int c) {
	if (E.cy==E.numrows) { //cursor on the tilde line insert new line
		editor_append_row("",0);
	}
	editor_row_insert_char(&E.row[E.cy],E.cx,c);
	E.cx++;
}

//file i/o
char *editor_rows_to_string(int *buflen) {
	int totlen=0;
	int j;
	for(j=0;j<E.numrows;j++){ //total file text length
		totlen += E.row[j].size+1;
	}
	*buflen = totlen;

	char *buf = malloc(totlen);//allocate buffer for all file text
	char *p= buf;
	for(j=0;j<E.numrows;j++) { //copy all text to buffer
		memcpy(p,E.row[j].chars,E.row[j].size);
		p+=E.row[j].size;
		*p='\n'; //add last character '\n'
		p++; //next line
	}

	return buf;
}

void editor_open(char *filename) {
	free(E.filename);
	E.filename = strdup(filename);

	FILE *fp = fopen(filename,"r");
	if (!fp) {
		die("fopen");
	}

	char *line=NULL;
	size_t linecap = 0;
	ssize_t linelen;

	while ((linelen = getline(&line,&linecap,fp))!=-1) {
		while (linelen > 0 &&  (line[linelen-1]=='\n' ||
		line[linelen-1]=='\r')){ //strip off the '\r' or '\n' at the end of the line
			linelen--;
		}
		editor_append_row(line,linelen);
	}

	free(line);
	fclose(fp);
	E.dirty = 0;
}

void editor_save() {
	if (E.filename==NULL) {
		return;
	}

	int len;
	char *buf = editor_rows_to_string(&len);

	int fd = open(E.filename,O_RDWR|O_CREAT,0644);
	if (fd!=-1){
		if (ftruncate(fd,len)!=-1){ //sets the file size to the specified length
			if (write(fd,buf,len)==len){ //write the file content to the file
				close(fd);
				free(buf);
				E.dirty = 0;
				editor_set_status_message("%d bytes written to disk",len);
				return;
			}
		}
		close(fd);
	}

	free(buf);
	editor_set_status_message("can't save I/O error: %s",strerror(errno));
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
void editor_scroll() {
	E.rx = 0;
	if (E.cy < E.numrows) {
		E.rx = editor_row_cx_to_rx(&E.row[E.cy],E.cx);
	}

	if (E.cy < E.rowoff) { // in the visible region setting offset with current cursor position
		E.rowoff = E.cy;
	}
	if (E.cy >= E.rowoff + E.screen_rows) { //out of bottom screen
		E.rowoff = E.cy - E.screen_rows +1;
	}
	if (E.rx < E.coloff) { //in the visible region
		E.coloff = E.rx;
	}
	if (E.rx >= E.coloff+E.screen_cols) { //out of right screen
		E.coloff = E.rx - E.screen_cols + 1;
	}
}

void editor_draw_rows(struct abuf *ab) {
	int y;
	for(y=0;y<E.screen_rows;y++){
		int filerow = y+E.rowoff;
		if (filerow>=E.numrows) {
			if (E.numrows==0 && y==E.screen_rows/3) {
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

		} else { //render text of file
			int len = E.row[filerow].rsize - E.coloff;
			if (len < 0) len = 0;
			if (len > E.screen_cols) len = E.screen_cols;
			ab_append(ab,&E.row[filerow].render[E.coloff],len);
		}
			
		ab_append(ab,"\x1b[K",3); //clear from right of the cursor side
		
		ab_append(ab,"\r\n",2);
		
	}
}

void editor_draw_statusbar(struct abuf *ab) {
	ab_append(ab,"\x1b[7m",4); //inverting colors
	char status[80],rstatus[80];
	int len = snprintf(status,sizeof(status),"%.20s - %d lines %s",
	E.filename ? E.filename : "[No Name]",E.numrows,
	E.dirty ? "(modified)" : "");
	
	int rlen = snprintf(rstatus,sizeof(rstatus),"%d/%d",E.cy+1,E.numrows);

	if (len > E.screen_cols ){
		len = E.screen_cols;
	}
	ab_append(ab,status,len);
	while (len<E.screen_cols) {
		if (E.screen_cols-len==rlen){
			ab_append(ab,rstatus,rlen);
			break;
		} else{
			ab_append(ab," ",1);
			len++;
		}
	}
	ab_append(ab,"\x1b[m",3); //turn off inverting colors
	ab_append(ab,"\r\n",2);
}

void editor_draw_messagebar(struct abuf *ab) {
	ab_append(ab,"\x1b[K",3); //clear from right of the cursor side for bottom of line
	int msg_len=strlen(E.statusmsg);
	if (msg_len > E.screen_cols){
		msg_len = E.screen_cols;
	}
	if (msg_len && time(NULL)-E.statusmsg_time < 5) { //only show less 5 seconds
		ab_append(ab,E.statusmsg,msg_len);
	} 
}

void editor_refresh_screen() {
	editor_scroll();

	struct abuf ab = ABUF_INIT;
	
	ab_append(&ab,"\x1b[?25l",6); //hide cursor
	ab_append(&ab,"\x1b[H",3);//setting cursor position

	editor_draw_rows(&ab);
	editor_draw_statusbar(&ab);
	editor_draw_messagebar(&ab);

	char buf[32];
	snprintf(buf,sizeof(buf),"\x1b[%d;%dH",(E.cy-E.rowoff+1),(E.rx-E.coloff+1));
	ab_append(&ab,buf,strlen(buf));

	ab_append(&ab,"\x1b[?25h",6); //show cursor

	write(STDOUT_FILENO,ab.b, ab.len); //cursor move to the top-left position
	ab_free(&ab);
}

void editor_set_status_message(const char *fmt, ...){
	va_list ap;
	va_start(ap,fmt);
	vsnprintf(E.statusmsg,sizeof(E.statusmsg),fmt,ap);
	va_end(ap);
	E.statusmsg_time = time(NULL); //get current time;
}


//input
void editor_move_cursor(int key) {
	//check cursor is on actual line
	erow *row = (E.cy >= E.numrows) ? NULL: &E.row[E.cy];
	
	switch (key) {
		case ARROW_LEFT:
		if (E.cx!=0) {
			E.cx--;
		}
		else if (E.cy > 0 ) { //move cursor to last line
			E.cy--; //last row
			E.cx = E.row[E.cy].size; //last line last char
		}
		break;

		case ARROW_RIGHT:
		if (row && E.cx < row->size){ //check cursor is on the actual line can move right
			E.cx++;
		}
		else if (row && E.cx==row->size){//move cursor to next line start position
			E.cy++; //next line
			E.cx = 0; //next line start position
		}
		break;
		
		case ARROW_UP:
		if (E.cy!=0) {
			E.cy--;
		}
			break;
		case ARROW_DOWN:
		if (E.cy < E.numrows) {
			E.cy++;
		}
			break;
	}

	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int rowlen = row ? row->size : 0;
	if (E.cx > rowlen){
		E.cx = rowlen;
	}
}

void editor_process_keypress() {
	int c = editor_readkey();

	switch (c) {
		case '\r':
			break;
		case CTRL_KEY('q'): //mapping ctrl-q to exit
			write(STDOUT_FILENO,"\x1b[2J",4);
			write(STDOUT_FILENO,"\x1b[H",3);
			exit(0);
			break;

		case CTRL_KEY('s'):
			editor_save();
			break;

		case HOME_KEY:
			E.cx = 0;
			break;
		case END_KEY:
			if (E.cy < E.numrows) { //move cursor to end of the line
				E.cx = E.row[E.cy].size;
			}
			break;

		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:

			break;

		case PAGE_UP:
		case PAGE_DOWN:
		{
			if (c==PAGE_UP){ //move cursor to top of screen
				E.cy = E.rowoff; 
			} else if (c==PAGE_DOWN) { //move cursor to bottom of screen
				E.cy = E.rowoff+E.screen_rows-1; 
				if (E.cy > E.numrows) {
					E.cy = E.numrows;
				}
			}
			int times=E.screen_rows;
			while (times--) {
				editor_move_cursor(c==PAGE_UP? ARROW_UP:ARROW_DOWN);
			}
			break;
		}

		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editor_move_cursor(c);
			break;

		case CTRL_KEY('l'):
		case '\x1b': //[ character
		break;

		default:
			editor_insert_char(c);
			break;
	}
}

//init
void init_editor() {

	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.numrows = 0;
	E.row = NULL;
	E.rowoff = 0;
	E.coloff = 0;
	E.filename = NULL;
	E.statusmsg[0]='\0';
	E.statusmsg_time = 0;
	E.dirty = 0;

	if (get_window_size(&E.screen_rows,&E.screen_cols)==-1) {
		die("get_window_size");
	}

	E.screen_rows -=2; //leave space for status bar
}

int main(int argc,char *argv[]) {

	enable_rawmode();
	init_editor();
	if (argc>=2) {
		editor_open(argv[1]);
	}

	editor_set_status_message("help: ctrl-s = save | ctrl-q = quit");
	

	while (1) {
		editor_refresh_screen();
		editor_process_keypress();
	}

    
    return 0;
}
