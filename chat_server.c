#include <stdio.h>
#include <stdlib.h>
#include "udp.h"
#include <pthread.h>
#include <ctype.h>
#include <time.h>

#define HISTORY_SIZE 15
#define INACTIVE_TIME 300
#define PING_TIMEOUT 10



typedef struct MutedClient {
    unsigned long id;
    struct MutedClient *next;
} MutedClient;

typedef struct Client {
    struct sockaddr_in address;
    char name[BUFFER_SIZE];

    unsigned long id;
    MutedClient *muted_clients;
    time_t last_active_time;
    time_t ping_sent_time;
    int waiting_ping;

    struct Client *activity_next;
    struct Client *next;
} Client;

typedef struct Request {
    struct sockaddr_in sender;
    char text[BUFFER_SIZE];
    int sd;
} Request;



Client *clients = NULL;
Client *active_clients = NULL;
Client *waiting_clients = NULL;

char history[HISTORY_SIZE][BUFFER_SIZE];
int history_next = 0;
int history_count = 0;

unsigned long next_client_id = 0;

pthread_rwlock_t clients_lock = PTHREAD_RWLOCK_INITIALIZER;
pthread_rwlock_t history_lock = PTHREAD_MUTEX_INITIALIZER;


void reply_client(Request *cr, char *message){
    udp_socket_write(cr->sd, &cr->sender, message, strlen(message) + 1);
}

time_t current_time(void){
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0){
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return ts.tv_sec;
}

time_t last_activity_time(Client *c){
    if(c->waiting_ping){
        return c->ping_sent_time;
    }
    return c->last_active_time;
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

int check_if_muted(MutedClient *m, unsigned long sender_id){
    while(m != NULL){
        if (m->id == sender_id){
            return 1;
        }
        m = m->next;
    }
    return 0;
}

void insert_activity(Client **head, Client *target){
    Client *c = *head;
    Client *previous = NULL;

    while(c != NULL && last_activity_time(c) <= last_activity_time(target)){
        previous = c;
        c = c->activity_next;
    }
    target->activity_next = c;

    if(previous == NULL){
        *head = target;
    }
    else{
        previous->activity_next = target;
    }
}

void remove_activity(Client **head, Client *target){
    Client *c = *head;
    Client *previous = NULL;

    while(c != NULL){
        if(c == target){
            if(previous == NULL){
                *head = c->activity_next;
            }
            else{
                previous->activity_next = c->activity_next;
            }

            c->activity_next = NULL;
            return;
        }
        previous = c;
        c = c->activity_next;
    }
}

void remove_client(Client *target){
    Client *c = clients;
    Client *previous = NULL;

    while(c != NULL){
        if(c == target){
            if(previous == NULL){
                clients = c->next;
            }
            else{
                previous->next = c->next;
            }

            if(target->waiting_ping == 1){
                remove_activity(&waiting_clients, c);
            }
            else{
                remove_activity(&active_clients, c);
            }

            MutedClient *m = c->muted_clients;
            while(m != NULL){
                MutedClient *next_m = m->next;
                free(m);
                m = next_m;
            }
            free(c);
            return;
        }
        previous = c;
        c = c->next;
    }
}

void add_history(char *message){
    pthread_rwlock_rdlock(&history_lock);
    snprintf(history[history_next], BUFFER_SIZE, "%s", message);
    history_next = (history_next + 1) % HISTORY_SIZE;
    if(history_count < HISTORY_SIZE){
        history_count++;
    }
    pthread_rwlock_unlock(&history_lock);
}

void record_activity(Request *cr){
    pthread_rwlock_wrlock(&clients_lock);
    Client *c = find_client(&cr->sender);

    if(c == NULL){
        pthread_rwlock_unlock(&clients_lock);
        return;
    }
    
    if(c->waiting_ping == 1){
        remove_activity(&waiting_clients, c);
        c->last_active_time = current_time();
        insert_activity(&waiting_clients, c);
    }
    else{
        remove_activity(&active_clients, c);
        c->last_active_time = current_time();
        insert_activity(&active_clients, c);
    }
    pthread_rwlock_unlock(&clients_lock);
}

void refresh_activity(Request *cr){
    pthread_rwlock_wrlock(&clients_lock);
    Client *c = find_client(&cr->sender);
    
    remove_activity(&waiting_clients, c);
    c->last_active_time = current_time();
    c->waiting_ping = 0;
    c->ping_sent_time = 0;
    insert_activity(&active_clients, c);
    pthread_rwlock_unlock(&clients_lock);
}



void connect_client(void *arg){
    char message[BUFFER_SIZE];
    Request *cr = (Request *) arg;

    pthread_rwlock_wrlock(&clients_lock);
    if(find_client(&cr->sender) != NULL){
        pthread_rwlock_unlock(&clients_lock);
        reply_client(cr, "You are already connected.");
        return;
    }
    if(find_client_by_name(cr->text) != NULL){
        pthread_rwlock_unlock(&clients_lock);
        reply_client(cr, "Username already exists.");
        return;
    }

    Client *c = calloc(1, sizeof(*c));
    if(c == NULL){
        pthread_rwlock_unlock(&clients_lock);
        perror("calloc");
        return;
    }
    c->id = next_client_id;
    next_client_id ++;
    c->muted_clients = NULL;
    c->address = cr->sender;
    strcpy(c->name, cr->text);

    c->last_active_time = current_time();
    c->ping_sent_time = 0;
    c->waiting_ping = 0;
    c->activity_next = NULL;
    insert_activity(&active_clients, c);

    c->next = clients;
    clients = c;
    pthread_rwlock_unlock(&clients_lock);

    snprintf(message, sizeof(message), "Hi %s, you have successfully connected to the chat\n", cr->text);
    reply_client(cr, message);
    reply_client(cr, "This is chat history.");

    pthread_rwlock_rdlock(&history_lock);
    if(history_count < HISTORY_SIZE){
        for(int i=0; i<history_count; i++){
            reply_client(cr,history[i]);
        }
    }
    else{
        int position = history_next;
        for(int i=0; i< HISTORY_SIZE; i++){
            reply_client(cr,history[position]);
            position = (position + 1) % HISTORY_SIZE;
        }
    }
    pthread_rwlock_unlock(&history_lock);
}

void say_client(void *arg){
    Request *cr = (Request *) arg;

    pthread_rwlock_rdlock(&clients_lock);
    Client *c = find_client(&cr->sender);

    if(c != NULL){
        Client *clients_list = clients;
        char message[BUFFER_SIZE];
        snprintf(message, sizeof(message), "%s: %s\n", c->name, cr->text);
        add_history(message);
        while(clients_list != NULL){
            if (check_if_muted(clients_list->muted_clients, c->id) != 1){
                udp_socket_write(cr->sd, &clients_list->address ,message, strlen(message) + 1);
            }
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
    remove_client(c);
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

    pthread_rwlock_wrlock(&clients_lock);
    Client *sender = find_client(&cr->sender);
    Client *target = find_client_by_name(cr->text);
    MutedClient *m = calloc(1, sizeof(*m));
    m->id = target->id;
    m->next = sender->muted_clients;
    sender->muted_clients = m;
    pthread_rwlock_unlock(&clients_lock);

}

void unmute_client(void *arg){
    Request *cr = (Request *) arg;

    pthread_rwlock_wrlock(&clients_lock);
    Client *sender = find_client(&cr->sender);
    Client *target = find_client_by_name(cr->text);
    MutedClient *m = sender->muted_clients;
    MutedClient *previous_m = NULL;

    while(m != NULL){
        if(m->id == target->id){
            if (previous_m == NULL){
                sender->muted_clients = m->next;
            }
            else{
                previous_m->next = m->next;
            }
        }
        previous_m = m;
        m = m->next;
    }
    pthread_rwlock_unlock(&clients_lock);
}

void kick_client(void *arg){
    Request *cr = (Request *) arg;
    if(ntohs(cr->sender.sin_port) != 6666){
        reply_client(cr, "No Permission.");
        return;
    }

    pthread_rwlock_wrlock(&clients_lock);
    Client *target = find_client_by_name(cr->text);
    char message_to_target[BUFFER_SIZE], broadcast_message[BUFFER_SIZE];
    snprintf(message_to_target, sizeof(message_to_target), "You have been removed from the chat.\n");
    snprintf(broadcast_message, sizeof(broadcast_message), "%s has been removed from the chat.\n", target->name);

    udp_socket_write(cr->sd, &target->address ,message_to_target, strlen(message_to_target) + 1);
    remove_client(target);
    Client *c_list = clients;
    
    while(c_list != NULL){
        udp_socket_write(cr->sd, &c_list->address ,broadcast_message, strlen(broadcast_message) + 1);
        c_list = c_list->next;
    }

    pthread_rwlock_unlock(&clients_lock);
}



void *ping_thread(void *arg){
    int sd = *(int *)arg;

    while(1){
        pthread_rwlock_wrlock(&clients_lock);

        time_t now = current_time();
        while(waiting_clients != NULL && now - waiting_clients->ping_sent_time >= PING_TIMEOUT){
            Client *c = waiting_clients;
            printf("%s has timed out.\n", c->name);
            remove_client(c);
        }

        while(active_clients != NULL && now - active_clients->last_active_time >= INACTIVE_TIME){
            Client *c = active_clients;

            remove_activity(&active_clients, c);
            c->waiting_ping = 1;
            c->ping_sent_time = current_time();
            insert_activity(&waiting_clients, c);

            char message[] = "ping$";
            udp_socket_write(sd, &c->address, message, sizeof(message));
        }

        pthread_rwlock_unlock(&clients_lock);
        sleep(1);
    }
    return NULL;
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
    else{
        reply_client(cr,"Unknown Command.");
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

        client_requests->text[strcspn(client_requests->text, "\r\n")] = '\0';
        if(strcmp(client_requests->text, "ret-ping$") == 0){
            refresh_activity(client_requests);
            free(client_requests);
            continue;
        }
        record_activity(client_requests);


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



int main(int argc, char *argv[]){
    int sd = udp_socket_open(SERVER_PORT);
    assert(sd > -1);

    pthread_t listener;
    pthread_t ping;

    pthread_create(&listener, NULL, listener_thread, &sd);
    pthread_create(&ping, NULL, ping_thread, &sd);
    pthread_join(listener, NULL);
    pthread_join(ping,NULL);

    close(sd);
    return 0;
}