// Compile command: gcc -o client my-client.c client-helper.c
// Run command: ./client server 36000

// General information:
// 1. The client is able to sign up and sign in.
// 2. The client is able to send messages to other users in different groups.
// 3. The client is able to join groups and when signed in, the membership is preserved.
// 4. Two threads are created for sending and receiving messages but cannot print the messages. 
// 5. The client cannot flawlessly send messages because of a potential race condition related 
//    to how the two threads are synchronized. 
// 6. The client cannot flawlessly receive messages from the server.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ctype.h> // needed for num_choice
#include <semaphore.h>
#include "client-helper.h"
#include <pthread.h>

#define SIGN_UP 10
#define SIGN_IN 20
#define S2C_OK 200
#define S2C_ERR 400
#define BUFFER_SIZE 256
#define MAX_PSWD_SIZE 32

typedef struct USER
{
    char *email;
    char *name;
    struct USER *next;
    int cmps340;
    int cmps352;
} User;

typedef struct THREADINFO
{
    int server_socket;
    User *user;
} ThreadArgs;

typedef struct MESSAGE
{
    char *message;
    User *sender;
    struct MESSAGE *next;
} Message;

int sign_up(int server_socket, char *string, size_t *size_ptr);
int perform_authentication(int server_socket, char *string, size_t *size_ptr, int choice, char *succ_msg, char *fail_msg, User *user);
void display_menu();
void handle_choice(int server_socket, int choice, char *string, size_t *size, User *user);
void send_msg(int server_socket, char *string, size_t *size, User *user, int group_id);
void send_string(int server_socket, const char *prompt, char *string, size_t *size_ptr);
void send_list_req(int server_socket, int groupID);
void send_exit(int server_socket);
int recv_ack(int server_socket);
void recv_message_list(int server_socket);
int num_choice(char *string);
int sign_in(int server_socket, char *string, size_t *size_ptr, User *user);
void display_group_menu();
void join_group_chat(int server_socket, char *string, size_t *size_ptr, User *user);
int getGroupChoice(char *string, size_t *size, User *user);
void authorize(int server_socket, char *string, size_t *size, int max_attempts, User *user);
void *receive_thread(void *args);
void *send_thread(void *args);
void verify_memberships(int server_socket, User *user);

pthread_mutex_t send_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t recv_mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t input_semaphore;
sem_t output_semaphore;


int main(int argc, char *argv[]) {
   
    pthread_mutex_init(&send_mutex, NULL);
    pthread_mutex_init(&recv_mutex, NULL);
    sem_init(&input_semaphore, 0, 1); // Initialize the input semaphore with an initial value of 1
    sem_init(&output_semaphore, 0, 0); // Initialize the output semaphore with an initial value of 0

    pthread_t sendThread; 
    pthread_t receiveThread;
    int server_socket = get_server_connection(argv[1], argv[2]);
    char *string = NULL;
    size_t size = 0;
    int choice;
    // Set CMPS340 and CMPS352 to 0
    User user;
    user.cmps340 = 0;
    user.cmps352 = 0;

    ThreadArgs thread_args = {server_socket, &user};

    printf("Welcome to cMessage!\n");

    authorize(server_socket, string, &size, 5, &user);

    // Create threads for sending and receiving messages
    pthread_create(&sendThread, NULL, send_thread, (void *)&thread_args);
    pthread_create(&receiveThread, NULL, receive_thread, (void *)&thread_args);

    // Wait for threads to finish before exiting
    pthread_join(sendThread, NULL);
    pthread_join(receiveThread, NULL);


    free(string);
    close(server_socket);

    pthread_mutex_destroy(&send_mutex);
    pthread_mutex_destroy(&recv_mutex);
    sem_destroy(&input_semaphore);
    sem_destroy(&output_semaphore);
    return 0;
}

void *send_thread(void *args) {
    ThreadArgs *thread_args = (ThreadArgs *)args;
    int server_socket = thread_args->server_socket;
    User *user = thread_args->user;
    char *string = NULL;
    size_t size = 0;
    int group_id;
    int choice = 0;

    while (1) {
        display_menu();
        getline(&string, &size, stdin);
        choice = num_choice(string);
        
        handle_choice(server_socket, choice, string, &size, user);
        if (choice == 3) {
            sem_post(&output_semaphore); // Signal the semaphore to exit
            break;
        }
    }
    
    free(string);
    pthread_exit(NULL);
}

void *receive_thread(void *args) {
    ThreadArgs *thread_args = (ThreadArgs *)args;
    int server_socket = thread_args->server_socket;

    while (1) {
        if (sem_trywait(&output_semaphore) == 0) {
            break; // Exit the loop if the semaphore is signaled
        }

        pthread_mutex_lock(&recv_mutex);

        int nameSize = 0;
        int msgSize = 0;
        recv(server_socket, &nameSize, sizeof(int), 0);
        char *name = (char *)malloc(nameSize * sizeof(char));
        recv(server_socket, name, nameSize, 0);
        recv(server_socket, &msgSize, sizeof(int), 0);
        char *msg = (char *)malloc(msgSize * sizeof(char));
        recv(server_socket, msg, msgSize, 0);

        int ack = recv_ack(server_socket);
        
        if (ack == S2C_OK)
            printf("\t%s<-%s \n", msg, name);
        
        free(name);
        free(msg);

        pthread_mutex_unlock(&recv_mutex);
    }

    pthread_exit(NULL);
}

void authorize(int server_socket, char *string, size_t *size_ptr, int max_attempts, User *user) {
    int attempts = 0;
    while (attempts < max_attempts) {
        attempts++;
        printf("1. Sign Up\n2. Sign In\n> ");
        getline(&string, size_ptr, stdin);
        int choice = num_choice(string);
        send(server_socket, &choice, sizeof(int), 0);

        if (choice == 1) {
            perform_authentication(server_socket, string, size_ptr, choice, "Sign up successful!", "Sign up failed, the email is already registered. Try again.", user);
            break;
        } else if (choice == 2) {
            perform_authentication(server_socket, string, size_ptr, choice, "Sign in successful!", "Sign in failed, invalid email or password. Try again.", user);
            break;
        } else {
            printf("Invalid choice. Please try again.\n");
        }
    }
}

int perform_authentication(int server_socket, char *string, size_t *size_ptr, int auth_type, char *succ_msg, char *fail_msg, User *user) {
    int server_response = 0;

    while (server_response != S2C_OK) {
        if (auth_type == 1)
            server_response = sign_up(server_socket, string, size_ptr);
        else
            server_response = sign_in(server_socket, string, size_ptr, user);

        if (server_response == S2C_ERR)
            printf("%s\n", fail_msg);
        else
            printf("%s\n", succ_msg);
    }
    return server_response;
}

int sign_up(int server_socket, char *string, size_t *size_ptr) {
    send_string(server_socket, "Enter your email: ", string, size_ptr);
    send_string(server_socket, "Enter your name: ", string, size_ptr);
    send_string(server_socket, "Enter your password: ", string, size_ptr);

    int status = recv_ack(server_socket);
    return status;
}

int sign_in(int server_socket, char *string, size_t *size_ptr, User *user) {
    send_string(server_socket, "Enter your email: ", string, size_ptr);
    send_string(server_socket, "Enter your password: ", string, size_ptr);

    int status = recv_ack(server_socket);

    // Receive the server's response
    if (status == S2C_OK) {
        verify_memberships(server_socket, user);
        printf("Memberships verified\n");
    }
    
    return status;
}

void display_menu() {
    printf("1. Join A group chat\n2. Send Message\n3. Log out\n> ");
}

void display_group_menu() {
    printf("1. CMPS Chat\n2. CMPS 340 Chat\n3. CMPS 352 Chat \n> ");
}

void join_group_chat(int server_socket, char *string, size_t *size_ptr, User *user) {
    // Get the group chat choice from the user
    printf("Enter the group chat you want to join\n");
    printf("1. Join CMPS340\n2. Join CMPS352\n> ");
    getline(&string, size_ptr, stdin);
    string[strlen(string) - 1] = '\0'; // Remove newline character
    
    // Set either CMPS340 or CMPS352 to 1 in the user struct
    int group_id;
    if (strcmp(string, "1") == 0) {
        if(user->cmps340 == 1) {
            printf("You are already a member of this group\n");
            return;
        }
        user->cmps340 = 1;
        group_id = 340;
    } else if (strcmp(string, "2") == 0) {
        if(user->cmps352 == 1) {
            printf("You are already a member of this group\n");
            return;
        }
        user->cmps352 = 1;
        group_id = 352;
    } else {
        printf("Invalid choice. Please try again.\n");
        return;
    }
    
    send(server_socket, &group_id, sizeof(int), 0);
}

int num_choice(char *string) {
    char *endptr;
    long val = strtol(string, &endptr, 10);

    // Check if the parsed value is 1, 2, 3, or 4 and if there are no extra characters
    if ((val == 1 || val == 2 || val == 3 || val == 4) && (*endptr == '\0' || *endptr == '\n')) {
        return (int)val;
    }

    return 0;
}

void handle_choice(int server_socket, int choice, char *string, size_t *size, User *user) {
    //sem_wait(&semaphore);
    send(server_socket, &choice, sizeof(int), 0);
    int group_id;
    switch (choice) {
    case 1:
        join_group_chat(server_socket, string, size, user);

        
        break;
    case 2:
       // Send message
        group_id = getGroupChoice(string, size, user);

        if (group_id != -1) {
            send_msg(server_socket, string, size, user, group_id);
            recv_ack(server_socket);
        }
        break;
    case 3:
        send_exit(server_socket);
        break;
    default:
        printf("Invalid choice. Please try again.\n");
        break;
    }
  
}

int getGroupChoice(char *string, size_t *size, User *user) {
    int group_choice;
    int group_id;
    printf("Select Group to Message\n");
    display_group_menu();
    getline(&string, size, stdin);
    group_choice = atoi(string);

    if (group_choice == 1) {
        group_id = 1;
    } else if (group_choice == 2) {
        if (user->cmps340 == 1) {
            group_id = 340;
        } else {
            printf("You are not a member of this group\n");
            group_id = -1;
        }
    } else if (group_choice == 3) {
        if (user->cmps352 == 1) {
            group_id = 352;
        } else {
            printf("You are not a member of this group\n");
            group_id = -1;
        }
    } else {
        printf("Invalid choice. Please try again.\n");
        group_id = -1;
    }

    return group_id;
}

void send_string(int server_socket, const char *prompt, char *string, size_t *size_ptr) {
    printf("%s", prompt);
    getline(&string, size_ptr, stdin);
    string[strlen(string) - 1] = '\0';
    send(server_socket, string, strlen(string) + 1, 0);
}

void send_msg(int server_socket, char *string, size_t *size, User *user, int group_id) {

    sem_wait(&input_semaphore); // Lock the input semaphore
    pthread_mutex_lock(&send_mutex); // Lock the send mutex
    // Get the message from the user
    printf("Enter your message: ");
    getline(&string, size, stdin);
    string[strlen(string) - 1] = '\0'; // Remove newline character

    // Send the message to the server
    int nBytes = strlen(string) + 1;
    send(server_socket, &nBytes, sizeof(int), 0);
    send(server_socket, string, nBytes, 0);
    send(server_socket, &group_id, sizeof(int), 0);
    printf("Message sent\n");

    recv_ack(server_socket);

    pthread_mutex_unlock(&send_mutex); // Unlock the send mutex
    sem_post(&input_semaphore); // Unlock the input semaphore
    sem_post(&output_semaphore); // Signal the output semaphore to print messages
}

void send_exit(int server_socket)
{
    int choice = 3;
    send(server_socket, &choice, sizeof(int), 0);
}

int recv_ack(int server_socket)
{
    int ack = 0;
    recv(server_socket, &ack, sizeof(int), 0);

    // for debugging
    if (ack == S2C_OK)
        printf("Received OK\n");
    else if (ack == S2C_ERR)
        printf("Received ERR\n");

    return ack;
}

void verify_memberships(int server_socket, User *user) {

    // Send a request to the server to verify memberships
    send(server_socket, "verify", strlen("verify") + 1, 0);
    // Receive the server's response
    char response[BUFFER_SIZE];
    recv(server_socket, response, BUFFER_SIZE, 0);
    // Display the membership status
    if (strstr(response, "CMPS340: YES") != NULL) {
        printf("CMPS340: YES\n");
        user->cmps340 = 1;
    } else {
        printf("CMPS340: NO\n");
    }

    if (strstr(response, "CMPS352: YES") != NULL) {
        printf("CMPS352: YES\n");
        user->cmps352 = 1;
    } else {
        printf("CMPS352: NO\n");
    }
}
