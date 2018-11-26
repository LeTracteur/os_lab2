#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>

#include "babble_server.h"
#include "babble_types.h"
#include "babble_utils.h"
#include "babble_communication.h"
#include "babble_server_answer.h"

sem_t mutex;
sem_t mutex_ans;
sem_t ansfull;
sem_t ansempty;
sem_t fullbuffer;
sem_t emptybuffer;

int in = 0;
int out = 0;
int in_ans = 0;
int out_ans = 0;

static void display_help(char *exec)
{
    printf("Usage: %s -p port_number\n", exec);
}


static int parse_command(char* str, command_t *cmd)
{
    char *name = NULL;
    
    /* start by cleaning the input */
    str_clean(str);
    
    /* get command id */
    cmd->cid=str_to_command(str, &cmd->answer_expected);
    
    switch(cmd->cid){
    case LOGIN:
        if(str_to_payload(str, cmd->msg, BABBLE_ID_SIZE)){
            name = get_name_from_key(cmd->key);
            fprintf(stderr,"Error from [%s]-- invalid LOGIN -> %s\n", name, str);
            free(name);
            return -1;
        }
        break;
    case PUBLISH:
        if(str_to_payload(str, cmd->msg, BABBLE_SIZE)){
            name = get_name_from_key(cmd->key);
            fprintf(stderr,"Warning from [%s]-- invalid PUBLISH -> %s\n", name, str);
            free(name);
            return -1;
        }
        break;
    case FOLLOW:
        if(str_to_payload(str, cmd->msg, BABBLE_ID_SIZE)){
            name = get_name_from_key(cmd->key);
            fprintf(stderr,"Warning from [%s]-- invalid FOLLOW -> %s\n", name, str);
            free(name);
            return -1;
        }
        break;
    case TIMELINE:
        cmd->msg[0]='\0';
        break;
    case FOLLOW_COUNT:
        cmd->msg[0]='\0';
        break;
    case RDV:
        cmd->msg[0]='\0';
        break;    
    default:
        name = get_name_from_key(cmd->key);
        fprintf(stderr,"Error from [%s]-- invalid client command -> %s\n", name, str);
        free(name);
        return -1;
    }

    return 0;
}


/* processes the command and eventually generates an answer */
static int process_command(command_t *cmd, answer_t **answer)
{
    int res=0;

    switch(cmd->cid){
    case LOGIN:
        res = run_login_command(cmd, answer);
        break;
    case PUBLISH:
        res = run_publish_command(cmd, answer);
        break;
    case FOLLOW:
        res = run_follow_command(cmd, answer);
        break;
    case TIMELINE:
        res = run_timeline_command(cmd, answer);
        break;
    case FOLLOW_COUNT:
        res = run_fcount_command(cmd, answer);
        break;
    case RDV:
        res = run_rdv_command(cmd, answer);
        break;
    default:
        fprintf(stderr,"Error -- Unknown command id\n");
        return -1;
    }

    if(res){
        fprintf(stderr,"Error -- Failed to run command ");
        display_command(cmd, stderr);
    }

    return res;
}

int main(int argc, char *argv[])
{
    int sockfd, newsockfd;
    int portno=BABBLE_PORT;
    
    int opt;
    int nb_args=1;
    
    //char* recv_buff=NULL;
    //int recv_size=0;
    
    //unsigned long client_key=0;
    //char client_name[BABBLE_ID_SIZE+1];

    //command_t *cmd;
    //answer_t *answer=NULL;

    int id_count = 0;
    pthread_t tids[MAX_CLIENT];
    //for the CS
    sem_init(&mutex, 0, 1);
    sem_init(&mutex_ans, 0, 1);
    sem_init(&fullbuffer, 0, 0);
    sem_init(&emptybuffer, 0, BABBLE_PRODCONS_SIZE);
    sem_init(&ansfull, 0, 0);
    sem_init(&ansempty, 0, BABBLE_ANSWER_THREADS);

    
    while ((opt = getopt (argc, argv, "+p:")) != -1){
        switch (opt){
        case 'p':
            portno = atoi(optarg);
            nb_args+=2;
            break;
        case 'h':
        case '?':
        default:
            display_help(argv[0]);
            return -1;
        }
    }
    
    if(nb_args != argc){
        display_help(argv[0]);
        return -1;
    }

    server_data_init();    

    if((sockfd = server_connection_init(portno)) == -1){
        return -1;
    }

    printf("Babble server bound to port %d\n", portno);
    /* main server loop */
    while(1){
        
        if((newsockfd= server_connection_accept(sockfd))==-1){
            return -1;
        }
        pthread_create(&tids[id_count], NULL, communication_thread, (void*)newsockfd);
        id_count +=1;
    }
    close(sockfd);
    return 0;
}


void* communication_thread(void * arg){
    int newsockfd = (int)arg;

    unsigned long client_key=0;
    char client_name[BABBLE_ID_SIZE+1];

    char* recv_buff=NULL;
    int recv_size=0;

    command_t *cmd;
    answer_t *answer=NULL;

    memset(client_name, 0, BABBLE_ID_SIZE+1);
    if((recv_size = network_recv(newsockfd, (void**)&recv_buff)) < 0){
        fprintf(stderr, "Error -- recv from client\n");
        close(newsockfd);
        return(0);
    }

    
    cmd = new_command(0);

    if(parse_command(recv_buff, cmd) == -1 || cmd->cid != LOGIN){
        fprintf(stderr, "Error -- in LOGIN message\n");
        close(newsockfd);
        free(cmd);
        return(0);
    }

    /* before processing the command, we should register the
        * socket associated with the new client; this is to be done only
        * for the LOGIN command */
    cmd->sock = newsockfd;

    if(process_command(cmd, &answer) == -1){
        fprintf(stderr, "Error -- in LOGIN\n");
        close(newsockfd);
        free(cmd);
        return(0);
    }
    
    /* notify client of registration */
    if(send_answer_to_client(answer) == -1){
        fprintf(stderr, "Error -- in LOGIN ack\n");
        close(newsockfd);
        free(cmd);
        free_answer(answer);
        return(0);
    }
    else{
        free_answer(answer);
    }
    
    /* let's store the key locally */
    client_key = cmd->key;
    
    strncpy(client_name, cmd->msg, BABBLE_ID_SIZE);
    free(recv_buff);
    free(cmd);

    /* looping on client commands */
    while((recv_size=network_recv(newsockfd, (void**) &recv_buff)) > 0){
        cmd = new_command(client_key);
    
        if(parse_command(recv_buff, cmd) == -1){
            fprintf(stderr, "Warning: unable to parse message from client %s\n", client_name);
            notify_parse_error(cmd, recv_buff, &answer);
            send_answer_to_client(answer);
            free_answer(answer);
            free(cmd);
        }
        else{
            /*On remplit le buffer */
            sem_wait(&emptybuffer);
            sem_wait(&mutex);
            cmd_buffer[in] = cmd;
            in = (in+1)% BABBLE_PRODCONS_SIZE;
            sem_post(&mutex);
            sem_post(&fullbuffer);
        }
        free(recv_buff);
    }

    if(client_name[0] != 0){
        cmd = new_command(client_key);
        cmd->cid= UNREGISTER;
        
        if(unregisted_client(cmd)){
            fprintf(stderr,"Warning -- failed to unregister client %s\n",client_name);
        }
        free(cmd);
    }
    return NULL;
}

void* executor(void* arg){
    while(1){
        sem_wait(&fullbuffer);
        sem_wait(&mutex);
        command_t* cmd = cmd_buffer[out];
        answer_t *answer=NULL;
        cmd_buffer[out] = NULL;
        out = (out+1)%BABBLE_PRODCONS_SIZE;
        sem_post(&mutex);
        sem_post(&emptybuffer);
        //creer un buffer un reponse
        
        
        if(process_command(cmd, &answer) == -1){
            fprintf(stderr, "Warning: unable to process command from client %lu\n", cmd->key);
        }
        free(cmd);

        sem_wait(&ansempty);
        sem_wait(&mutex_ans);
        ans_buffer[in_ans] = answer;
        in_ans = (in_ans+1)%BABBLE_ANSWER_THREADS;
        sem_post(&mutex_ans);
        sem_post(&ansfull);
    }
    return NULL;  
    

}
void* answering_fct(void* arg){
    while(1){
        sem_wait(&ansfull);
        sem_wait(&mutex_ans);
        answer_t* answer = ans_buffer[out_ans];
        out_ans = (out_ans+1)%BABBLE_ANSWER_THREADS;
        sem_post(&mutex_ans);
        sem_post(&ansempty);
        if(send_answer_to_client(answer) == -1){
            fprintf(stderr, "Warning: unable to answer command from client %lu\n", answer->key);
        }
        free_answer(answer);
    }
    return NULL;
}