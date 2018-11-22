#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "babble_registration.h"

client_bundle_t *registration_table[MAX_CLIENT];
int nb_registered_clients;
pthread_mutex_t mutex_rw = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int readers =0;
int writer = 0;
int w_writer =0;


void reading (void){
    pthread_mutex_lock(&mutex_rw);
    while((w_writer >0) || (writer == 1)){
        pthread_cond_wait(&cond, &mutex_rw);
    }
    readers +=1;
    pthread_mutex_unlock(&mutex_rw);
}

void endreading(void){
    pthread_mutex_lock(&mutex_rw);
    readers -=1;
    if ((readers == 0) && (w_writer >0) ) {
        pthread_cond_broadcast(&cond);
    }
    pthread_mutex_unlock(&mutex_rw);
}

void writing(void){
    pthread_mutex_lock(&mutex_rw);
    w_writer +=1;
    while((readers >0) || (writer == 1)){
        pthread_cond_wait(&cond, &mutex_rw);
    }
    w_writer -=1;
    writer = 1;
    pthread_mutex_unlock(&mutex_rw);
}

void endwriting (void){
    pthread_mutex_lock(&mutex_rw);
    writer = 0;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex_rw);
}

void registration_init(void)
{
    nb_registered_clients=0;

    memset(registration_table, 0, MAX_CLIENT * sizeof(client_bundle_t*));
}

client_bundle_t* registration_lookup(unsigned long key)
{
    int i=0;
    client_bundle_t *c = NULL;
    reading();
    for(i=0; i< nb_registered_clients; i++){
        if(registration_table[i]->key == key){
            c = registration_table[i];
            break;
        }
    }
    endreading();
    return c;
}

int registration_insert(client_bundle_t* cl)
{   
    if(nb_registered_clients == MAX_CLIENT){
        fprintf(stderr, "ERROR: MAX NUMBER OF CLIENTS REACHED\n");
        return -1;
    }
    
    /* lookup to find if key already exists */
    writing(); 
    int i=0;
    
    for(i=0; i< nb_registered_clients; i++){
        if(registration_table[i]->key == cl->key){
            break;
        }
    }
    
    
    if(i != nb_registered_clients){
        fprintf(stderr, "Error -- id % ld already in use\n", cl->key);
        return -1;
    }

    /* insert cl */
    registration_table[nb_registered_clients]=cl;
    nb_registered_clients++;
    
    endwriting();
    return 0;
}


client_bundle_t* registration_remove(unsigned long key)
{
    writing();
    int i=0;
    
    for(i=0; i<nb_registered_clients; i++){
        if(registration_table[i]->key == key){
            break;
        }
    }

    if(i == nb_registered_clients){
        fprintf(stderr, "Error -- no client found\n");
        return NULL;
    }
    
    
    client_bundle_t* cl= registration_table[i];

    nb_registered_clients--;
    registration_table[i] = registration_table[nb_registered_clients];
    endwriting();
    return cl;
}
