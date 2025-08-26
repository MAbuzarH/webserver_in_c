#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_USERNAME_LEN 65
#define MAX_PASSWORD_LEN 65
#define HASH_LEN 65

struct FormData
{
    char username[65];
    char name[65];
    char password[65];
};

int main()
{
    char *body_data = "username=abuzar&password=1234&codei";
   printf("book it\n");
    return 0;
}