# canonical mode, cooked mode

keyboard input only sent to program when user press 'Enter'

example: type some text , you can use backspace to delete the text to fix, and then press 'Enter' to send it to the program

# raw mode
press any key, can process it immediately

# tcgetattr(int fd, struct termios *termios_p)
gets input from fd,and store it termios structure

# tcsetattr(int fd, int optional_actions,const struct termios *termios_p)
setting terminal structure with opt_actions 

## TCSAFLUSH option
tcsetattr()'s `optional_actions` 
waiting for all pending output to be write to the terminal，and discard hasn't been read input

# c_lflag
for local flag

## ECHO
Echo input characters

## ICANON
control canonical mode,
turn off,can be reading byte-by-byte instead of line-by-line

## ISIG
when press characters are `INTERRUPT`,`QUIT`,`SUSPEND` or `DSUSP`,
generate the corresponding signal

ex:ctrl+c is `SIGINT`, ctrl+z is `SIGTSTP`

# c_iflag
for input flag

## IXON
enable XON(ctrl+s) and XOFF(ctrl+q) flow control on output

## IEXTEN
enable implementation-defined input processing

some system, press ctrl+v, the terminal wait for you type another character and send that character

## ICRNL
input translate carriage(`\r`) to newline(`\n`)

turn off it,fix ctrl-m is character code 13, not 10,`Enter` key is also from 10 to 13

## BRKINT
If enable，break condition cause a SIGINT signal to be sent to the program,like ctrl+c

## INPCK
Enable input parity check

## ISTRIP
Strip of eighnth bit,meaning it will set it to 0

## CS8
not a flag，it's bitmask with multiple bits
setting the character size(CS) to 8 bits per byte，but most system support 

# c_oflag
for output flag

## OPOST
Enable implementation-defined  output  processing
POST stands for post processing of output
turn '\r\n' to '\n' when output

editor printf format must add '\r\n' showing new line

# int atexit(void (*function)(void));
from <stdlib.h>
register a function when program exit, it will call it


# iscntrl(int c)
check for character is control character
control character ASCII range code:0-31 and 127(backspace)

# ctrl+s and ctrl+q
stop and continue terminal output in raw mode

# VMIN
minimum number of characters for read

set 0 so read() will return as soon as input to be read

# VTIME
set maximum amount of time to wait before read() returns

# ctrl_key marco
ctrl-q:((K) & 0x1f) = 113&0x1f= 17

# escape sequence
for coloring text,moving cursor and clearing parts of screen
start with escape character \x1b(27 in decimal) and `[`

using `J` command to clear screen 

before `J` number `2` arugment means clear the entire screen

<esc>[1j would clear screen up from the cursor position

<esc>[0j would clear screen from cursor up to end of the screen


## <esc>[2J
clear screen

## <esc>[H
setting cursor position，take two argument row and column
default is 1,1 like <esc>[1;1H
row and column are 1-base not 0

## <esc>[999C<esc>[999B
move the cursor to the bottom-right corner

`C` for moves the cursor to the right
`B` for moves the cursor to the down
`999` ensure reaches the right and bottom edges for the screen

## <esc>[6n
query cursor position

## <esc>[?25l
hide cursor

## <esc>[?25h
show cursor

## <esc>[K
clear line

### <esc>[2K
clear whole line

### <esc>[1K
clear from left of the cursor side

### <esc>[0K
clear from right of the cursor to end of the line

# simple way to get window size
example: 
struct winsize ws;
ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws)

# hard way to get window size
strategy is to position the cursor at the bottom-right of screen ,and then use escape sequences query the cursor of the position


進度:Multiple lines