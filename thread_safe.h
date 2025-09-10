#ifndef _THREAD_SAFE_H
#define _THREAD_SAFE_H

#include<pthread.h>
#include "session.h"

extern pthread_mutex_t session_mutex;
extern pthread_mutex_t file_mutex;
extern pthread_mutex_t db_mutex;


//thread-safe  funcation diclaration 
Session* create_session_ts(const char  *username);
Session* get_session_ts(const char *session_id);
void delete_session_ts(const char *session_id);
void cleanup_session_ts();

#endif