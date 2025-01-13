
This is not complete. The sending thread works on the client side as intended but the receiving thread is not. Potential infinite loop due to race condition between the two threads. All other implementations work.

Race conditions identified:
- MessageQueue being accessed by multiple threads. Protected using mutexes
- Although with flaws, the send and the receive threads are synchronized using two semaphores for input/output and two mutexes for threads

Server:   gcc -o server my-server.c server-helper.c authentication.c -lpthread -lrt -lcrypt
Client:   gcc -o client my-client.c client-helper.c

# My-Client.c

The purpose of this program is to communicate with the server data such as User: Email, Name, and messages. When the user enters '1' on the client side they are prompted to send a message. When the user types '2' then the server will return all the save data from the message list and the user list to the client. The client will then print each message sent along with the user who sent the message. 

## Main()

Establishes the initial connection to the server using `get_server_connection` from `client-helper.c`, where the server’s hostname and port are specified as command-line arguments. After connecting, `main` starts the signup process by calling "sign_up" to collect and send the user’s email and name to the server, setting up a user profile. It then enters a loop to display a menu and take user input repeatedly until the user selects “Exit.” Within this loop, the user’s choice is checked by "num_choice", and "handle_choice" is called to handle the appropriate client actions. Once the user chooses to exit, the client frees the string buffer and closes the server connection.

## int num_choice()

Validates the user's menu selection by converting the input to an integer and ensuring it corresponds to one of the available options which are: 1, 2, or 3. It strips away leading and trailing whitespace, checks that the input is not empty, and uses strtol to convert the input string to a long integer. If the parsed value doesn’t match a valid menu choice or includes any extra characters, it returns 0 to indicate an invalid choice.

## void  sign_up()

A helper function that prompts the user for their email and name and then sends this information to the server, allowing the server to identify this client and associate them with a user profile. It calls send_string twice, once to capture and send the email and once for the name. This setup process happens only once at the start of the client session.

## void display_menu()

Displays the main client menu with three options, prompting the user to select an action. This function is called at the beginning of each loop iteration in main to ensure the menu is visible after each action and to guide the user through their choices.

## void  handle_choice()
Directs the client’s response based on the user’s menu choice:
-   If the user selects 1 (Send Message), the function prompts for a message, which is then sent to the server using send_msg. After sending, it calls recv_ack to confirm that the server received the message.
-   If the user selects 2, which is Request All Messages, it sends a request for the message list with "send_list_req" and then calls "recv_message_list" to receive and display all stored messages from the server.
-   If the user selects 3, which is Exit, it notifies the server of the intent to exit using "send_exit", closing the client session.
-   For invalid inputs, it prints an error message and prompts the user to try again.

## void  send_msg()

Handles string input from the user and sends it to the server. After displaying the prompt , it uses "getline" to read user input and remove the newline character before sending the string over the socket. This function is primarily used for gathering and sending user information during signup.

## void  send_string()

Sends a message to the server in response to the user’s “Send Message” choice. The function sends the following:

-   "choice" value set to 1 (indicating a message action),
-   the message length "nBytes" as an integer,
-   and the message content itself.

This struct ensures the server can interpret the message correctly.

## void  send_list_req()

Sends a request for the full message list by setting "choice" to 2. This choice signals the server to retrieve and send all stored messages, which the client will receive and display. This function is crucial for allowing the client to view previous messages in the group chat conversation.

## void  send_exit()

Notifies the server of the client’s intent to disconnect by sending "choice == 3", indicating an exit command. This allows the server to close the connection for this client seamlessly, freeing up any resources associated with the session.

## void  recv_ack()

Waits to receive an acknowledgment, "S2C_ACK` set to 200" from the server to confirm that the client’s previous message has been received. If the acknowledgment is not received, the client notifies the user that the acknowledgment failed, which could indicate a communication issue. This function helps verify the server’s successful receipt of messages.

## void  recv_message_list()

Receives and displays the complete list of messages from the server. 

-   First, it receives the number of messages "count".
-   It then iterates over each message, receiving the size of the sender’s name and message and dynamically allocating memory for each.
-   The client displays each message in the format “-> [sender name]: [message content]” for readability.
-   Finally, the client frees all dynamically allocated memory to prevent memory leaks. This function enables the client to view a history of all messages exchanged in the session.

## display_group_menu

Displays the list of groups 

## getGroupChoice()

Validates the user's group selection and ensures they are part of the chosen group before proceeding

## void *send_thread(void *args)

Handles user input and send messages to the server

## void *receive_thread(void *args)

Listens to the server and prints out messages for a relevant group




# My-Server.c
My server is the server side of our environment. The server is used to receive and return acknowledgements from clients. It is also used to store user data in lists such as the users names and messages. When these lists are needed by the client, the server will send the list to the client as to allow the client to print the messages along with the names of each user who sent each message.
## `main` Function
### Description:
The entry point of the server program. Initializes necessary resources (e.g., mutexes, condition variables), starts the server, and handles incoming client connections. It creates a new session for each client and delegates further handling to a dedicated subserver thread.

---

## `subserver` Function
### Description:
Handles client requests after connection. Authenticates or registers users, manages group selection, and processes message reception. The loop continues until the client disconnects, at which point the session is closed.

---

## `initNewUser` Function
### Description:
Initializes a new user by setting their properties to default values and linking them to the current session.

---

## `user_reg` Function
### Description:
Handles user registration. Receives user data (email, name, password), checks if the user exists, and registers them by storing the user data and sending an acknowledgment.

---

## `user_auth` Function
### Description:
Handles user authentication. Receives user login credentials, verifies them against stored data, and either logs the user in or sends an error response.

---

## `find_user` Function
### Description:
Searches for a user in the user list by their email and returns the corresponding user if found.

---

## `sendToGroup` Function
### Description:
A dedicated thread function that processes messages from the message queue and sends them to the appropriate group based on the group ID.

---

## `handleGroupSelection` Function
### Description:
Handles user requests for group selection (CMPS340 or CMPS352) and adds the user to the appropriate group.

---

## `recv_msg` Function
### Description:
Receives a message from a client, processes it (storing it in the message list and queue), and sends an acknowledgment to the client.

---

## `enqueue_message` Function
### Description:
Adds a message to the message queue and signals the condition variable for message processing.

---

## `append_msg` Function
### Description:
Appends a message to the message list.

---

## `append_user` Function
### Description:
Adds a user to a specific group (CMPS340 or CMPS352) based on the provided group ID.

---

## `sendMsgOut` Function
### Description:
Sends a message to all users in the specified group by iterating over the group members and sending the message through their active sessions.

---

## `send_msg` Function
### Description:
Sends a message to a client by sending the sender's name, message content, and an acknowledgment.

---

## `send_ack` Function
### Description:
Sends an acknowledgment message to the client.

---

## `send_err` Function
### Description:
Sends an error message to the client.

---

## `insert_newUser` Function
### Description:
Inserts a new user into the user list.

---

## `init_session` Function
### Description:
Initializes a session with the given client socket and associated group list.

---

## `handle_verify_memberships` Function
### Description:
Verifies a user's membership in specific groups (CMPS340, CMPS352) and sends the result back to the client.

---

## `dequeue_message` Function
### Description:
Removes and returns the first message from the message queue while ensuring thread safety through mutex locking and condition variables.

---

## `is_user_list_empty` Function
### Description:
Checks if the user list is empty.
