#include <stdio.h>
#include <stdlib.h>
#include "udp.h"
#include <pthread.h>
#include <ctype.h>



typedef struct Client {
    struct sockaddr_in address;
    char name[BUFFER_SIZE];
    struct Client *next;
} Client;

typedef struct Request {
    struct sockaddr_in sender;
    char text[BUFFER_SIZE];
    int sd;
} Request;

Client *clients = NULL;
pthread_rwlock_t clients_lock = PTHREAD_RWLOCK_INITIALIZER;

void reply_client(Request *cr, char *message){
    udp_socket_write(cr->sd, &cr->sender, message, strlen(message) + 1);
}

Client *find_client(struct sockaddr_in *address){
    Client *c = clients;

    while(c != NULL){
        if(c->address.sin_addr.s_addr == address->sin_addr.s_addr && c->address.sin_port == address->sin_port){
            return c;
        }
        c = c->next;
    }

    return NULL;
}

Client *find_client_by_name(char *name){
    Client *c = clients;
    while(c != NULL){
        if(strcmp(c->name, name) == 0){
            return c;
        }
        c = c->next;
    }
    return NULL;
}

void connect_client(void *arg){
    char message[BUFFER_SIZE];
    Request *cr = (Request *) arg;

    pthread_rwlock_wrlock(&clients_lock);
    Client *c = calloc(1, sizeof(*c));
    c->address = cr->sender;
    strcpy(c->name, cr->text);
    c->next = clients;
    clients = c;
    pthread_rwlock_unlock(&clients_lock);

    snprintf(message, sizeof(message), "Hi %s, you have successfully connected to the chat\n", cr->text);
    reply_client(cr, "you have successfully connected to the chat");
}

void say_client(void *arg){
    Request *cr = (Request *) arg;

    pthread_rwlock_rdlock(&clients_lock);
    Client *c = find_client(&cr->sender);

    if(c != NULL){
        Client *clients_list = clients;
        char message[BUFFER_SIZE];
        snprintf(message, sizeof(message), "%s: %s\n", c->name, cr->text);
        while(clients_list != NULL){
            udp_socket_write(cr->sd, &clients_list->address ,message, strlen(message) + 1);
            clients_list = clients_list->next;
        }
    }

    pthread_rwlock_unlock(&clients_lock);
}

void sayto_client(void *arg){
    Request *cr = (Request *) arg;

    pthread_rwlock_rdlock(&clients_lock);
    char target_client[BUFFER_SIZE], message[BUFFER_SIZE];
    char *p = strchr(cr->text, ' ');
    Client *c_sender = find_client(&cr->sender);

    if(p != NULL){
        size_t len_c = p - cr->text;
        char *start_point_r = p + 1;
        while(isspace((unsigned char)*start_point_r)){
            start_point_r++;
        }
        memcpy(target_client, cr->text, len_c);
        target_client[len_c] = '\0';

        Client *c_target = find_client_by_name(target_client);
        snprintf(message, sizeof(message), "%s: %s\n", c_sender->name, start_point_r);
        udp_socket_write(cr->sd, &c_target->address ,message, strlen(message) + 1);
    }
    pthread_rwlock_unlock(&clients_lock);
}

void disconn_client(void *arg){
    Request *cr = (Request *) arg;

    pthread_rwlock_wrlock(&clients_lock);
    Client *c = find_client(&cr->sender);
    if(c == NULL){
        pthread_rwlock_unlock(&clients_lock);
        return;
    }
    
    Client *c_list = clients;
    Client *previous = NULL;
    while(c_list != NULL){
        if(c_list == c){
            if(previous == NULL){
                clients = c_list->next;
            }
            else{
                previous->next = c_list->next;
            }

            free(c_list);
            break;
        }
        previous = c_list;
        c_list = c_list->next;
    }
    pthread_rwlock_unlock(&clients_lock);

    reply_client(cr, "Disconnected. Bye!");
}

void rename_client(void *arg){
    Request *cr = (Request *) arg;
    char message[BUFFER_SIZE];

    pthread_rwlock_wrlock(&clients_lock);
    Client *c = find_client(&cr->sender);
    strcpy(c->name,cr->text);
    pthread_rwlock_unlock(&clients_lock);

    snprintf(message, sizeof(message), "You are now known as %s.\n", cr->text);
    reply_client(cr,message);
}

void mute_client(void *arg){
    Request *cr = (Request *) arg;

}

void unmute_client(void *arg){
    Request *cr = (Request *) arg;
}

void kick_client(void *arg){
    Request *cr = (Request *) arg;
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

        memcpy(client_request, start_point_r, strlen(start_point_r) + 1);
    }
    else{
        free(cr);
        return NULL;
    }

    if (strcmp(client_command, "conn") == 0){
        strcpy(cr->text, client_request);
        connect_client(cr);
    }
    else if(strcmp(client_command, "say") == 0){
        strcpy(cr->text, client_request);
        say_client(cr);
    }
    else if(strcmp(client_command, "sayto") == 0){
        strcpy(cr->text, client_request);
        sayto_client(cr);
    }
    else if(strcmp(client_command, "disconn") == 0){
        strcpy(cr->text, client_request);
        disconn_client(cr);
    }
    else if(strcmp(client_command, "mute") == 0){
        strcpy(cr->text, client_request);
        mute_client(cr);
    }
    else if(strcmp(client_command, "unmute") == 0){
        strcpy(cr->text, client_request);
        unmute_client(cr);
    }
    else if(strcmp(client_command, "rename") == 0){
        strcpy(cr->text, client_request);
        rename_client(cr);
    }
    else if(strcmp(client_command, "kick") == 0){
        strcpy(cr->text, client_request);
        kick_client(cr);
    }


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