// Compile command locally: gcc -o server my-server.c server-helper.c -lpthread -lrt 
// Compile command on AWS: gcc -o server my-server.c server-helper.c authentication.c -lpthread -lrt -lcrypt
// Run command: ./server server 36000

// General information:
// 1. The server is able to authenticate users.
// 2. The server is able to register users.
// 3. The server is able to receive messages from users and append based on the group. -- needs work
// 4. The server is able to forward messages to online users based on the group 
//    and are being sent to the client to print them. 
// 5. The server is able to create threads for each user.
// 6. Potential race condition due to poor synchronization.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
#include "server-helper.h"
#include "authentication.h"

#define BACKLOG         10
#define BUFFER_SIZE     256
#define ACK_TYPE        200
#define ERR_TYPE        400
#define MAX_PSWD_SIZE   32

typedef struct USER {
    char *email;
    char *name;
    char *password;
    struct USER *next;
    struct SESSION_DATA *curSession;
} User;

typedef struct USER_LIST {
    User *first;
    User *last;
    int count;
} UserList;

typedef struct MESSAGE {
    char *message;
    User *sender;
    struct MESSAGE *next;
    int groupID;
} Message;

typedef struct MESSAGE_LIST {
    Message *first;
    Message *last;
    int count;
} MessageList;

typedef struct MESSAGE_QUEUE_NODE {
    Message *msg;
    struct MESSAGE_QUEUE_NODE *next;
} MessageQueueNode;

typedef struct MESSAGE_QUEUE {
    MessageQueueNode *first;
    MessageQueueNode *last;
} MessageQueue;

typedef struct CMPS340_MEMBERS {
    User *member;
    struct CMPS340_MEMBERS *next;
} Cmps340;

typedef struct CMPS352_MEMBERS {
    User *member;
    struct CMPS352_MEMBERS *next;
} Cmps352;

typedef struct GROUPS_MEMBERS {
    UserList *cmps;
    Cmps340 *cmps340;
    Cmps352 *cmps352;
} Groups;

typedef struct THREADINFO {
    MessageQueue *msgQueue;
    Groups *groups;
} DedicatedThreadData;

typedef struct SESSION_DATA {
    UserList *userList;
    MessageList *messageList;
    User *user;
    int socketFd;
    MessageQueue *messageQueue;
    Groups *groupList;
} Session;

void *subserver(void *sessionData);
void initNewUser(int client_socket, User *user, Session *session, UserList *userList, MessageList *msgList, MessageQueue *msgQue, Groups *groupList);
void user_reg(int client_socket, User *user, UserList *userList, Session *session);
void user_auth(int client_socket, UserList *userList, Session *session);
User* find_user(UserList *list, const char *email);
void *sendToGroup(void *dedicatedThreadData);
void handleGroupSelection(int client_socket, Groups *groupList, User *user);
void recv_msg(int client_socket, MessageList *msgList, User *user, MessageQueue *msgQueue);
void enqueue_message(MessageQueue *msgQueue, Message *msg);
void append_msg(MessageList *msgList, Message *msg);

void append_user(Groups *groupList, User *user, int groupID);
void send_msgList(int client_socket, MessageList *msgList, int groupID);
void sendMsgOut(Message *msg, Groups *groups);
void send_msg(int client_socket, Message *msg);
void send_ack(int client_socket);
void send_err(int client_socket);
void insert_newUser(UserList *list, User *user);
void print_session_details(Session *session);
int is_user_list_empty(UserList *list);
MessageQueueNode* dequeue_message(MessageQueue *msgQueue);
void handle_verify_memberships(int client_socket, User *user, Groups *groupList);
void init_session(Session *session, int client_socket, Groups *groupList);

pthread_mutex_t queueMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queueCond = PTHREAD_COND_INITIALIZER;



int main(int argc, char *argv[]) {
    int server_socket;
    int client_socket;
    
    pthread_mutex_init(&queueMutex, NULL);
    pthread_cond_init(&queueCond, NULL);

    server_socket = start_server(argv[1], argv[2], BACKLOG);
    MessageList messageList = {NULL, NULL, 0};
    MessageQueue messageQueue = {NULL, NULL};
    UserList userList = {NULL, NULL, 0};

    Cmps340 *cmps340 = NULL; 
    Cmps352 *cmps352 = NULL;

    Groups groupLists = {&userList, cmps340, cmps352};
    
    DedicatedThreadData *threadData = (DedicatedThreadData *)malloc(sizeof(DedicatedThreadData));
    
    threadData->msgQueue = &messageQueue;
    threadData->groups = &groupLists;

    pthread_t thread;
    pthread_create(&thread, NULL, sendToGroup, (void *)threadData);

    while (1) {
        client_socket = accept_client(server_socket);
        Session *newSession = (Session *)malloc(sizeof(Session));
        init_session(newSession, client_socket, &groupLists);
        User *incomingUser = (User *)malloc(sizeof(User));
        
        initNewUser(client_socket, incomingUser, newSession, &userList, &messageList, &messageQueue, &groupLists);

        pthread_create(&thread, NULL, subserver, (void *)newSession);
        pthread_detach(thread);
    }

    close(server_socket);
}

void *subserver(void *sessionData) {
    Session *session = (Session *)sessionData;
    MessageList *msgList = session->messageList;
    MessageQueue *msgQueue = session->messageQueue;
    User *user = session->user;
    UserList *userList = session->userList;
    Groups *groups = session->groupList;
    int client_socket = session->socketFd;
    int choice = 0;
    int groupId = 0;

    recv(client_socket, &choice, sizeof(int), 0);
    
    if (choice == 1) {
        user_reg(client_socket, user, userList, session);
    } else if (choice == 2) {
        user_auth(client_socket, userList, session);
    } else {
        send_err(client_socket);
        close(client_socket);
        free(session);
        free(user);
        pthread_exit(NULL);
    }

    while (choice != 3) {
        recv(client_socket, &choice, sizeof(int), 0);
        if (choice == 1) {
            handleGroupSelection(session->socketFd, session->groupList, session->user);
        } else if (choice == 2) {
            recv_msg(session->socketFd, session->messageList, session->user, session->messageQueue);
        }
    }

    session->user->curSession = NULL;
    send_ack(client_socket);
    close(client_socket);
    free(session);
    printf("Client closed the connection\n");
    pthread_exit(NULL);
}

void initNewUser(int client_socket, User *user, Session *session, UserList *userList, MessageList *msgList, MessageQueue *msgQue, Groups *groupList) {
    user->email = NULL;
    user->name = NULL;
    user->password = NULL;
    user->next = NULL;
    user->curSession = NULL;

    session->userList = userList;
    session->messageList = msgList;
    session->user = user;
    session->socketFd = client_socket;
    session->messageQueue = msgQue;
    session->groupList = groupList;
}

void user_reg(int client_socket, User *user, UserList *userList, Session *session) {
    char newEmail[BUFFER_SIZE];
    char newName[BUFFER_SIZE];
    char newPassword[BUFFER_SIZE];

    while (1) {
        recv(client_socket, newEmail, BUFFER_SIZE, 0);
        recv(client_socket, newName, BUFFER_SIZE, 0);
        recv(client_socket, newPassword, BUFFER_SIZE, 0);
        
        if (find_user(userList, newEmail) == NULL) {
            user->email = strdup(newEmail);
            user->name = strdup(newName);
            
            //For local testing only
            //user->password = strdup(newPassword);

            //Encode password and store it for AWS
            char *new_enc_pass = encode(newPassword);
            user->password = new_enc_pass;


            insert_newUser(userList, user);
            
        } else {
            send_err(client_socket);
            continue;
        }

        user->curSession = session;
        printf("User registered successfully\n");
        send_ack(client_socket);
        break;
        
    }
}

void user_auth(int client_socket, UserList *userList, Session *session) {
    char newEmail[BUFFER_SIZE];
    char newPassword[BUFFER_SIZE];

    while (1) {
        recv(client_socket, newEmail, BUFFER_SIZE, 0);
        recv(client_socket, newPassword, BUFFER_SIZE, 0);
        
        User *user = find_user(userList, newEmail);

        if (user == NULL) {
            send_err(client_socket);
            continue;
        }

        if (user->password == NULL) {
            printf("Error: user->password is NULL\n");
            send_err(client_socket);
            continue;
        }


        // Authentication on AWS
        if (authenticate(newPassword, user->password)) {
            printf("logged in!\n");
        } else {
            printf("Authentication Failed!\n");
            send_err(client_socket);
            continue;
        }

        // Authentication Locally - for testing purposes
        // if (strcmp(user->password, newPassword) != 0) {
        //     send_err(client_socket);
        //     continue;
        // }

        session->user = user;
        user->curSession = session;

        send_ack(client_socket);
        handle_verify_memberships(client_socket, user, session->groupList);
        printf("User authenticated successfully\n");
        break;
    }
}

User *find_user(UserList *list, const char *email) {

    if (email == NULL) {
        return NULL;
    }

    if (is_user_list_empty(list)) {
        return NULL;
    }

    User *iterator = list->first;
    
    while (iterator != NULL) {
        
        if (strcmp(iterator->email, email) == 0) {
            return iterator;
        }
        iterator = iterator->next;
    }

    return NULL; // User not found
}

void *sendToGroup(void *dedicatedThreadData) {
    DedicatedThreadData *threadData = (DedicatedThreadData *)dedicatedThreadData;
    MessageQueue *msgQueue = threadData->msgQueue;
    Groups *groups = threadData->groups;

    while (1) {
        MessageQueueNode *queue_node = dequeue_message(msgQueue);

        while (queue_node != NULL) {
            Message *msg = queue_node->msg;
            
            sendMsgOut(msg, groups);
            free(queue_node);
            queue_node = dequeue_message(msgQueue);
        }
    }

    pthread_exit(NULL);
}

void handleGroupSelection(int client_socket, Groups *groupList, User *user) {
    int group = 0;
    recv(client_socket, &group, sizeof(int), 0);
    
    if (group == 340) {
        append_user(groupList, user, group);
    } else if (group == 352) {
        append_user(groupList, user, group);
    } else {
        printf("Invalid group ID\n");
    }
}

void recv_msg(int client_socket, MessageList *msgList, User *user, MessageQueue *msgQueue) {
    Message *msg = (Message *)malloc(sizeof(Message));
    int nExpected;
    int groupID;
    char buffer[BUFFER_SIZE];
    recv(client_socket, &nExpected, sizeof(int), 0);
    recv(client_socket, buffer, nExpected, 0);
    recv(client_socket, &groupID, sizeof(int), 0);
    send_ack(client_socket);

    msg->message = strdup(buffer);
    msg->sender = user;
    msg->groupID = groupID;

    printf("Message id: %d\n", msg->groupID);
    printf("Message: %s\n", msg->message);

    append_msg(msgList, msg);
    enqueue_message(msgQueue, msg);
}

void enqueue_message(MessageQueue *msgQueue, Message *msg) {
    if (msgQueue == NULL || msg == NULL) {
        return; // Handle the case where msgQueue or msg is NULL
    }

    pthread_mutex_lock(&queueMutex);
    MessageQueueNode *newNode = (MessageQueueNode *)malloc(sizeof(MessageQueueNode));
    newNode->msg = msg;
    newNode->next = NULL;

    if (msgQueue->first == NULL) {
        msgQueue->first = newNode;
        msgQueue->last = newNode;
    } else {
        msgQueue->last->next = newNode;
        msgQueue->last = newNode;
    }

    pthread_cond_signal(&queueCond);
    pthread_mutex_unlock(&queueMutex);
}

void append_msg(MessageList *msgList, Message *msg) {
   
    if (msg->message == NULL) {
        
        return;
    }

    if (msgList->count == 0) {
        msgList->first = msg;
        msgList->last = msg;
    } else {
        msgList->last->next = msg;
        msgList->last = msg;
    }
    msgList->count++;
}

void append_user(Groups *groupList, User *user, int groupID) {
    if (groupID == 340) {
        if(groupList->cmps340 == NULL) {
            groupList->cmps340 = NULL;
        }

        Cmps340 *newMember = (Cmps340 *)malloc(sizeof(Cmps340));
        
        newMember->member = user;
        newMember->next = groupList->cmps340;
        groupList->cmps340 = newMember;
        
    } else if (groupID == 352) {
        // Initialize the list if it's NULL
        if (groupList->cmps352 == NULL) {
            groupList->cmps352 = NULL;  // Start with NULL
        }
        Cmps352 *newMember = (Cmps352 *)malloc(sizeof(Cmps352));
        
        newMember->member = user;
        newMember->next = groupList->cmps352;
        groupList->cmps352 = newMember;

    } else {
        printf("Invalid group ID\n");
    }
}

void sendMsgOut(Message *msg, Groups *groups) {
    

    int client_socket;

    if (msg->groupID == 1) { // Send to CMPS group
        User *iterator = groups->cmps->first;
        while (iterator != NULL) {
            if (iterator->curSession != NULL) {
                client_socket = iterator->curSession->socketFd;
                send_msg(client_socket, msg);
            }
            iterator = iterator->next;
        }
    } else if (msg->groupID == 340 || msg->groupID == 352) { // Send to CMPS 340 or CMPS 352
        if (msg->groupID == 340) {
            Cmps340 *iterator = groups->cmps340;
            while (iterator != NULL) {
                if (iterator->member->curSession != NULL) {
                    client_socket = iterator->member->curSession->socketFd;
                    send_msg(client_socket, msg);
                }
                iterator = iterator->next;
            }
        } else {
            Cmps352 *iterator = groups->cmps352;
            while (iterator != NULL) {
                if (iterator->member->curSession != NULL) {
                    client_socket = iterator->member->curSession->socketFd;
                    send_msg(client_socket, msg);
                }
                iterator = iterator->next;
            }
        }
    }

}

void send_msg(int client_socket, Message *msg) {
    int msgSize = strlen(msg->message) + 1;
    int nameSize = strlen(msg->sender->name) + 1;

    send(client_socket, &nameSize, sizeof(int), 0);
    send(client_socket, msg->sender->name, nameSize, 0);

    send(client_socket, &msgSize, sizeof(int), 0);
    send(client_socket, msg->message, msgSize, 0);

    send_ack(client_socket);        
}

void send_ack(int client_socket) {
    int type = ACK_TYPE;
    send(client_socket, &type, sizeof(int), 0);
}

void send_err(int client_socket) {
    int type = ERR_TYPE;
    send(client_socket, &type, sizeof(int), 0);
}

void insert_newUser(UserList *list, User *user) {
    user->next = NULL;

    if (list->count == 0) {
        list->first = user;
        list->last = user;
    } else {
        list->last->next = user;
        list->last = user;
    }

    list->count++;
}

void init_session(Session *session, int client_socket, Groups *groupList) {
    session->socketFd = client_socket;
    session->user = NULL;
    session->groupList = groupList;
}

void handle_verify_memberships(int client_socket, User *user, Groups *groupList) {
    
    char request[BUFFER_SIZE];
    recv (client_socket, request, BUFFER_SIZE, 0);
    
    char response[BUFFER_SIZE] = "";
    
    // Check membership in CMPS340
    Cmps340 *current340 = groupList->cmps340;
    while (current340 != NULL) {
        if (current340->member == user) {
            strcat(response, "CMPS340: YES\n");
            break;
        }
        current340 = current340->next;
    }
    if (current340 == NULL) {
        strcat(response, "CMPS340: NO\n");
    }

    // Check membership in CMPS352
    Cmps352 *current352 = groupList->cmps352;
    while (current352 != NULL) {
        if (current352->member == user) {
            strcat(response, "CMPS352: YES\n");
            break;
        }
        current352 = current352->next;
    }
    if (current352 == NULL) {
        strcat(response, "CMPS352: NO\n");
    }

    // Send the response back to the client
    send(client_socket, response, strlen(response), 0);
}

MessageQueueNode* dequeue_message(MessageQueue *msgQueue) {
    pthread_mutex_lock(&queueMutex);
    while (msgQueue->first == NULL) {
        pthread_cond_wait(&queueCond, &queueMutex);
    }
    MessageQueueNode *node = msgQueue->first;
    msgQueue->first = msgQueue->first->next;
    if (msgQueue->first == NULL) {
        msgQueue->last = NULL;
    }
    pthread_mutex_unlock(&queueMutex);
    return node;
}



int is_user_list_empty(UserList *list) {
    return (list->first == NULL);
}
