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

	raw.c_lflag &= ~(ECHO | ICANON); //close echo characters

	tcsetattr(STDIN_FILENO, TCSAFLUSH,&raw);
}

int main() {

	enable_rawmode();

    char c;
    while (read(STDIN_FILENO,&c,1)==1 && c != 'q') {
		if (iscntrl(c)) {
			printf("%d\n",c);
		} else {
			printf("%d ('%c')\n",c,c);
		}
	}
    
    return 0;
}
