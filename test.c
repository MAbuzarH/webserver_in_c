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
int authantacate_user(const char *username,const char *password){
  FILE* fp = fopen("users.txt", "r");
    if (!fp) {
        return 0; // No user file, so no users can be authenticated.
    }
    
    char line[512];
    char hashed_password[HASH_LEN];
    // hash_password(password, hashed_password);
    while(fgets(line,sizeof(line),fp)){
     char stored_username[MAX_USERNAME_LEN];
     char stored_hash[HASH_LEN];
     char *colon = strchr(line,':');
     if(colon){
        size_t len = colon - line;
        if(len < MAX_USERNAME_LEN){
            strncpy(stored_username,line,len);
            stored_username[len] = '\0';

            char *newline = strchr(colon+1,'\n');
            if(newline) *newline = '\0';
            strncpy (stored_hash,colon + 1,HASH_LEN -1);
            if(strcmp(stored_username,username) == 0 && strcmp(stored_hash,password) == 0 ){
                fclose(fp);
                return 1;
            }
            
        }
     }
    }
    fclose(fp);
    return 0;
}


int main()
{
    char *body_data = "username=abuzar&password=1234&codei";
   if(authantacate_user("Abuzar","12345")){
    printf("founded\n");
   }
    return 0;
}