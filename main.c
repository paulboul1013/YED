#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void disable_rawmode() {
	tcsetattr(STDIN_FILENO,TCSAFLUSH,&orig_termios);
}

void enable_rawmode() {
	tcgetattr(STDIN_FILENO,&orig_termios); //get terminal input
	atexit(disable_rawmode); //when program exit, disable raw mode

	
	struct termios raw = orig_termios;

	raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | ICRNL | IXON); //turn off ctrl-s and ctrl-q, and translate '\r' to '\n'
	raw.c_oflag &= ~(OPOST); //turn off output processing，'\r\n' to '\n'

	raw.c_cflag |= (CS8);
	//turn off echo characters,turn off canonical mode,turn off ctrl-c and ctrl-z signals
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN| ISIG); 

	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	tcsetattr(STDIN_FILENO, TCSAFLUSH,&raw);
}

int main() {

	enable_rawmode();

	while (1) {
		char c='\0';
		read(STDIN_FILENO,&c,1);
		
		if (iscntrl(c)) {
			printf("%d\r\n",c);
		} else {
			printf("%d ('%c')\r\n",c,c);
		}

		if (c=='q') {
			break;
		}
		
	}

    
    return 0;
}
