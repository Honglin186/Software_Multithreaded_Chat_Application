#include <stdio.h>
#include <stdlib.h>
#include "udp.h"
#include <pthread.h>
#include <ctype.h>



typedef struct Client {
    struct sockaddr_in address;
    char name[32];
    struct Client *next;
} Client;

typedef struct Request {
    struct sockaddr_in sender;
    char text[BUFFER_SIZE];
    int sd;
} Request;

Client *clients = NULL;
pthread_rwlock_t clients_lock = PTHREAD_RWLOCK_INITIALIZER;

void connect_client(void *arg){
    Request *cr = (Request *) arg;
    pthread_rwlock_wrlock(&clients_lock);

    Client *c = calloc(1, sizeof(*c));
    c->address = cr->sender;
    strcpy(c->name, cr->text);
    c->next = clients;
    clients = c;

    pthread_rwlock_unlock(&clients_lock);
}








void *worker_thread(void *arg){
    Request *cr = (Request *) arg;
    char client_command[BUFFER_SIZE], client_request[BUFFER_SIZE];
    char *p = strchr(cr->text, '$');

    if(p != NULL){
        size_t len_c = p - cr->text;
        char *start_point_r = p + 1;
        while(isspace((unsigned char)*start_point_r)){
            start_point_r++;
        }
        memcpy(client_command, cr->text, len_c);
        client_command[len_c] = '\0';

        memcpy(client_request, p + 1, strlen(start_point_r) + 1);
    }
    else{
        free(cr);
        return NULL;
    }

    if (strcmp(client_command, "conn") == 0){
        strcpy(cr->text, client_request);
        connect_client(cr);
    }

    printf("Hi %s, you have successfully connected to the chat \n",clients->name);

    free(cr);
    return NULL;
}

void *listener_thread(void *arg){
    int sd = *(int *)arg;
    printf("Listener thread started on port %d\n", SERVER_PORT);
    while (1) {
        // Storage for request and response messages

        // Demo code (remove later)
        printf("Server is listening on port %d\n", SERVER_PORT);

        // Variable to store incoming client's IP address and port
    
        // This function reads incoming client request from
        // the socket at sd.
        // (See details of the function in udp.h)
        Request *client_requests = malloc(sizeof *client_requests);
        if (client_requests == NULL) {
            perror("malloc");
            return NULL;
        }
        client_requests->sd = sd;
        int rc = udp_socket_read(sd, &client_requests->sender, client_requests->text, sizeof client_requests->text - 1);
        if (rc <= 0){
            free(client_requests);
            continue;
        }
        client_requests->text[rc] = '\0';
        // Successfully received an incoming request

        pthread_t worker;
        rc = pthread_create(&worker, NULL, worker_thread, client_requests);
        if (rc != 0){
            fprintf(stderr, "pthread_create: %s\n", strerror(rc));
            free(client_requests);
            continue;
        }
        pthread_detach(worker);
    }
    return NULL;
}



int main(int argc, char *argv[])
{
    int sd = udp_socket_open(SERVER_PORT);
    assert(sd > -1);

    pthread_t listener;
    pthread_create(&listener, NULL, listener_thread, &sd);
    pthread_join(listener, NULL);
    close(sd);
    return 0;
}