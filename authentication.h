// compile:  gcc -lcrypt  authentication.c authentication-main.c
// run: ./a.out

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <termios.h>

// Encode plainpswd using algorithm SHA-256
// return the encoded password on heap.
// plainpswd -- a null-terminated string
// returned -- a null-terminated stirng
char *encode(char *plainpswd);

// compare if the login password matches the saved password
// loginpswd -- a null-terminated string
// savedpswd -- a null-terminated stirng
// return 1 if true, 0 otherwrise.
int authenticate(char *loginpawd, char *savedpswd);

// get password from keyboard with no echo
// password: buffer for the password
// len: size of the buffer - max size of password
int getpasswd(char *password, int len);
