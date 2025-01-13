// compile:  gcc -lcrypt  authentication.c authentication-main.c
// run: ./a.out

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include "authentication.h"

#define MAX_PSWD_SIZE 32

int main(void) {
   // register a psssword
   char *plainpswd = (char*) malloc(MAX_PSWD_SIZE);
   getpasswd(plainpswd, MAX_PSWD_SIZE);
   printf("your entered %s\n", plainpswd);  

   char *savedpswd = encode(plainpswd);
   printf("encoded  %s\n", savedpswd);

   free(plainpswd);

   // authentication
   char *loginpswd = (char*) malloc(MAX_PSWD_SIZE);
   getpasswd(loginpswd, MAX_PSWD_SIZE);
   printf("your entered %s\n", loginpswd);  

   free(loginpswd);
 
   if (authenticate(loginpswd, savedpswd)) {
       printf("logged in!\n");
   }
   else {
      printf("Authentication Failed!\n");
   }

   // free savedpswd as it is on heap
   free(savedpswd);
}

