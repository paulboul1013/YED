#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

//defines
#define CTRL_KEY(k) ((k) & 0x1f)

//data

struct termios orig_termios;

//terminal

void die(const char *s) {
	perror(s);
	exit(1);
}

void disable_rawmode() {
	if (tcsetattr(STDIN_FILENO,TCSAFLUSH,&orig_termios)==-1) {
		die("tcsetattr");
	}
}

void enable_rawmode() {
	//get terminal input
	if (tcgetattr(STDIN_FILENO,&orig_termios)==-1){
		die("tcgetattr");
	} 
	atexit(disable_rawmode); //when program exit, disable raw mode

	
	struct termios raw = orig_termios;

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

//output
void editor_refresh_screen() {
	write(STDOUT_FILENO,"\x1b[2J",4);
	write(STDOUT_FILENO,"\x1b[H",3);
}


//input
void editor_process_keypress() {
	char c = editor_readkey();

	switch (c) {
		case CTRL_KEY('q'): //mapping ctrl-q to exit
			exit(0);
			break;
	}
}

//init
int main() {

	enable_rawmode();

	while (1) {
		editor_refresh_screen();
		editor_process_keypress();
	}

    
    return 0;
}
