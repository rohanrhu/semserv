/*
 * https://github.com/rohanrhu/semserv
 *
 * semserv is a high-performance semaphore service
 * useable with long string ipc keys stored in memory
 * 
 * Copyright (C) 2015 Oğuzhan Eroğlu <rohanrhu2@gmail.com>
 * 
 * Licensed under The MIT License (MIT)
 *
 */

#define PACKET_BUF_LEN 256
#define PACKET_SIGNATURE 2015
#define PACKET_SIGNATURE_LEN 2
#define PACKET_KEY_SIZE_LEN 4
#define PACKET_CMD_ACQUIRE 1
#define PACKET_CMD_RELEASE 2
#define PACKET_CMD_LEN 1

#define STREAMING_STATE_WAITING 1
#define STREAMING_STATE_DEFINING 2
#define STREAMING_STATE_KEY_STREAMING 3
#define STREAMING_STATE_CMD 4

#define SEM_STATE_LOCKED 1
#define SEM_STATE_AVAILABLE 2

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include "../lib/uthash.h"

typedef struct {
    char name[30];
    uint8_t state;
    int lock_num;
    UT_hash_handle hh;
} sems_hash_t;

typedef struct {
    int *sockets;
    int sockets_len;
    int sockets_i;
} clients_t;

typedef struct {
    int client_socket;
    sems_hash_t** semaphores;
    clients_t* clients;
} handle_connection_param_t;

typedef struct {
    int socket;
    sems_hash_t** semaphores;
    clients_t* clients;
} read_thread_param_t;

typedef struct {
    int socket;
    uint8_t state;
    char* key;
    clients_t* clients;
} write_thread_param_t;

typedef struct {
    int socket;
    char* key_buf;
    uint8_t cmd;
    uint32_t data_len;
    uint8_t state;
    sems_hash_t** semaphores;
    clients_t* clients;
} receive_packet_param_t;

void handle_connection(handle_connection_param_t* param);
void client_handler_f(handle_connection_param_t* param);
void check_recv_result(receive_packet_param_t* param, ssize_t result);
void receive_packet(receive_packet_param_t* param);
void response_thread_f(receive_packet_param_t* param);
void read_thread_f(read_thread_param_t* param);
void write_thread_f(write_thread_param_t* param);
uint8_t check_socket_op_result(ssize_t* result);
void exit_handler(int sig);

pthread_mutex_t mutex;

int main(int argc, char *argv[]) {
    sems_hash_t* _semaphores = NULL;
    sems_hash_t** semaphores = &_semaphores;

    signal(SIGINT, exit_handler);

    int port = 5001;

    int server_socket, client_socket, cli_addr_len;
    struct sockaddr_in serv_addr, cli_addr;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (server_socket < 0) {
        perror("Socket error");
        exit(1);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("Bind error");
        exit(1);
    }

    listen(server_socket, 5);
    cli_addr_len = sizeof(cli_addr);

    clients_t *clients;

    clients = malloc(sizeof(clients_t));
    clients->sockets_len = 0;
    clients->sockets_i = 0;

    printf("Server is listening from 0.0.0.0:%d\n", port);

    pthread_mutex_init(&mutex, NULL);

    pthread_t client_handler;

    for (;;) {
        client_socket = accept(server_socket, (struct sockaddr *) &cli_addr, &cli_addr_len);
        if (client_socket < 0) {
            perror("Accept error");
            exit(1);
        }

        int i;

        if (clients->sockets_len == 0) {
            clients->sockets = (int *) malloc((++clients->sockets_len)*(sizeof(int)));
            clients->sockets[clients->sockets_i++] = client_socket;
        } else {
            int* tmp = malloc((sizeof(int)*clients->sockets_len));
            memcpy(tmp, clients->sockets, (clients->sockets_len*sizeof(int)));
            free(clients->sockets);
            clients->sockets = malloc((sizeof(int)*clients->sockets_len)-sizeof(int));

            i = 0;

            for (; i < clients->sockets_len; i++) {
                clients->sockets[i] = tmp[i];
            }

            clients->sockets[i] = client_socket;

            clients->sockets_i = (clients->sockets_len++)-1;

            free(tmp);
        }

        printf("new socket: %d\n", client_socket);

        handle_connection_param_t* param;
        param = malloc(sizeof(handle_connection_param_t));
        param->client_socket = client_socket;
        param->semaphores = semaphores;
        param->clients = clients;
        
        pthread_create(
            &client_handler,
            NULL,
            (void *) &client_handler_f,
            (void *) param
        );
    }

    exit_handler(0);

    return 0;
}

void client_handler_f(handle_connection_param_t* param) {
    handle_connection(param);
    close(param->client_socket);
}

void read_thread_f(read_thread_param_t* param) {
    receive_packet_param_t* receive_packet_param;
    receive_packet_param = malloc(sizeof(receive_packet_param_t));

    receive_packet_param->socket = param->socket;
    receive_packet_param->state = STREAMING_STATE_WAITING;
    receive_packet_param->semaphores = param->semaphores;
    receive_packet_param->clients = param->clients;

    receive_packet(receive_packet_param);
}

void write_thread_f(write_thread_param_t* param) {
    int _i = 0;
    int _socket;

    for (; _i < param->clients->sockets_len; _i++) {
        _socket = param->clients->sockets[_i];
        printf("socket: %d\n", _socket);

        uint16_t _sign = PACKET_SIGNATURE;
        send(_socket, &_sign, PACKET_SIGNATURE_LEN, 0);
        send(_socket, &param->state, 1, 0);
        int _key_len = strlen(param->key);
        send(_socket, &_key_len, PACKET_KEY_SIZE_LEN, 0);
        send(_socket, param->key, _key_len, 0);
    }

    free(param);
}

void handle_connection(handle_connection_param_t* param) {
    pthread_t read_thread;

    read_thread_param_t* read_thread_param;
    read_thread_param = malloc(sizeof(read_thread_param_t));
    read_thread_param->socket = param->client_socket;
    read_thread_param->semaphores = param->semaphores;
    read_thread_param->clients = param->clients;

    pthread_create(
        &read_thread,
        NULL,
        (void *) &read_thread_f,
        (void *) read_thread_param
    );

    pthread_join(read_thread, NULL);
}

void check_recv_result(receive_packet_param_t* param, ssize_t result) {
    if (result == 0) {
        printf("client disconnected: %d\n", param->socket);
        int* tmp = malloc((sizeof(int)*param->clients->sockets_len));
        memcpy(tmp, param->clients->sockets, (param->clients->sockets_len*sizeof(int)));
        free(param->clients->sockets);
        param->clients->sockets = malloc((sizeof(int)*param->clients->sockets_len)-sizeof(int));

        int i = 0;
        int j = 0;

        for (; i < param->clients->sockets_len; i++) {
            if (tmp[i] != param->socket) {
                param->clients->sockets[j++] = tmp[i];
            }
        }

        param->clients->sockets_i = (--param->clients->sockets_len)-1;

        free(tmp);
        free(param);

        pthread_exit(NULL);
    }
}

void receive_packet(receive_packet_param_t* param) {
    ssize_t result;

    if (param->state == STREAMING_STATE_WAITING) {
        uint16_t packet_signature_buf;

        result = recv(param->socket, &packet_signature_buf, PACKET_SIGNATURE_LEN, MSG_WAITALL);
        check_recv_result(param, result);

        if (!check_socket_op_result(&result)) return;

        if (packet_signature_buf == (uint16_t)PACKET_SIGNATURE) {
            printf("Signature found.\n");
            param->state = STREAMING_STATE_DEFINING;
        } else {
            printf("Signature not found.\n");
        }
    } else if (param->state == STREAMING_STATE_DEFINING) {
        result = recv(param->socket, &param->data_len, PACKET_KEY_SIZE_LEN, MSG_WAITALL);
        check_recv_result(param, result);

        if (!check_socket_op_result(&result)) return;

        printf("Packet size: %d\n", param->data_len);
        param->state = STREAMING_STATE_KEY_STREAMING;
    } else if (param->state == STREAMING_STATE_KEY_STREAMING) {
        param->key_buf = malloc((param->data_len+1) * sizeof(char));
        *(param->key_buf+param->data_len) = '\0';

        result = recv(param->socket, param->key_buf, param->data_len, MSG_WAITALL);
        check_recv_result(param, result);

        param->state = STREAMING_STATE_CMD;

        if (!check_socket_op_result(&result)) return;

        printf("key: %s\n", param->key_buf);
    } else if (param->state == STREAMING_STATE_CMD) {
        result = recv(param->socket, &param->cmd, PACKET_CMD_LEN, MSG_WAITALL);
        check_recv_result(param, result);

        param->state = STREAMING_STATE_WAITING;
        if (!check_socket_op_result(&result)) return;

        pthread_t response_thread;

        pthread_create(
            &response_thread,
            NULL,
            (void *) &response_thread_f,
            (void *) param
        );
    }
    
    receive_packet(param);
}

void response_thread_f(receive_packet_param_t* param) {
    sems_hash_t *hash_item;

    pthread_mutex_lock(&mutex);

    HASH_FIND_STR(*param->semaphores, param->key_buf, hash_item);
    
    pthread_mutex_unlock(&mutex);

    if (!hash_item) {
        hash_item = (sems_hash_t *) malloc(sizeof(sems_hash_t));
        strcpy(hash_item->name, param->key_buf);
        hash_item->lock_num = 0;
        hash_item->state = SEM_STATE_AVAILABLE;

        pthread_mutex_lock(&mutex);

        HASH_ADD_STR(*param->semaphores, name, hash_item);

        pthread_mutex_unlock(&mutex);
    }

    if (param->cmd == PACKET_CMD_RELEASE) {
        printf("CMD(%s): release\n", param->key_buf);
        
        if (--hash_item->lock_num <= 0) {
            hash_item->state = SEM_STATE_AVAILABLE;
            hash_item->lock_num = 0;

            pthread_t write_thread;

            write_thread_param_t* write_thread_param;
            write_thread_param = malloc(sizeof(write_thread_param_t));
            write_thread_param->socket = param->socket;
            write_thread_param->clients = param->clients;
            write_thread_param->state = SEM_STATE_AVAILABLE;
            write_thread_param->key = malloc(sizeof(char) * (strlen(param->key_buf)+1));
            write_thread_param->key = strcpy(write_thread_param->key, param->key_buf);

            pthread_create(
                &write_thread,
                NULL,
                (void *) &write_thread_f,
                (void *) write_thread_param
            );
        }
    } else if (param->cmd == PACKET_CMD_ACQUIRE) {
        printf("CMD(%s): acquire\n", param->key_buf);

        hash_item->lock_num++;
    }

    printf("lock_num: %d\n", hash_item->lock_num);
}

uint8_t check_socket_op_result(ssize_t* result) {
    if (*result < 0) {
        perror("Socket error");
        return 0;
    }

    return 1;
}

void exit_handler(int sig) {
    if (sig == SIGINT) {
        printf("Keyboard interrupt, exiting..");
    }

    printf("Semserv stopped successfully.");

    exit(0);
}