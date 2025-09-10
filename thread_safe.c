#include "thread_safe.h"
#include<stdio.h>


pthread_mutex_t session_mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;

Session* create_session_ts(const char *username){
    pthread_mutex_lock(&session_mutex);
    Session* session = create_session(username);
    pthread_mutex_unlock(&session_mutex);
    return session;
}

Session* get_session_ts(const char *session_id){
pthread_mutex_lock(&session_mutex);
Session* session = get_session(session_id);
pthread_mutex_unlock(&session_mutex);
return session;
}

void delete_session_ts(const char *session_id){
    pthread_mutex_lock(&session_mutex);
    delete_session(session_id);
    pthread_mutex_unlock(&session_mutex);
}

void cleanup_session_ts(){
pthread_mutex_lock(&session_mutex);
cleanup_sessions();
pthread_mutex_unlock(&session_mutex);

}