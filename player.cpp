/*
Rock-Paper-Scissors Game Client

TCP client that connects to game server and handles bidirectional communication
using threads: main thread for user input, background thread for server messages.

*/

#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>

// Global: allows intertwine between threads
int sock_fd; // file descriptor used for connecting to server
bool running = true; // Intializes the client running for shutdown between threads

/*
recieveMessage(): reads the messages from server,
stops after server or client disconnects.

This thread is used for just reading
*/
void recieveMessage() {
    char buffer[1024];

    while(running) {
        // Clears the buffer, preventing contamination
        memset(buffer, 0, sizeof(buffer));

        // reads value sent from server
        int valread = read(sock_fd, buffer, 1024);

        // If the value is 0 = closed, and 0 > means error
        if (valread <= 0) {
            std::cout << "\nDisconnected from server" << std::endl;
            running = false; // calls global var, signaling all threads to exit
            break;
        }

        // Displays message and reprint for input prompt
        std::cout << "\n" << buffer;
        std::cout << "You: " << std::flush;
    }
}

int main() {
    // --------- Socket Setup ---------

    // Create socket
    // uses AF_INET/IPv4 and SOCK_STREAM/TCP (stream oriented connection) for reliability
    sock_fd = socket(AF_INET, SOCK_STREAM, 0); // changes global var for threads
    if (sock_fd == -1) {
        std::cerr << "Socket creation failed!" << std::endl;
        return 1;
    }
    
    // Configure server address to allow connection
    sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET; // IPv4
    serv_addr.sin_port = htons(8080); // converts the server port to byte order

    // Convert IPv4 address (IP) from text to binary
    // "127.0.0.1" is the local host (running clients on same machine as server)
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address!" << std::endl;
        return 1;
    }
    
    // Connects to server
    // uses connect() to link to the server
    if (connect(sock_fd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection failed!" << std::endl;
        return 1;
    }
    
    std::cout << "Connected to server!" << std::endl;

    // Setup for Username: prompts for username
    std::string username;
    std::cout << "Enter your username: ";
    std::getline(std::cin, username);

    // Sends the username to server
    send(sock_fd, username.c_str(), username.length(), 0);

    std::cout << "Start chatting (type 'quit' to exit):\n" << std::endl;

// --------- Threading Communication ---------

    // starts to recieve data/messages Thread
    std::thread(recieveMessage).detach();

    std::cout << "You: " << std::flush;
     
    // Main thread used to handle the input and sending to server
    std::string message;
    while(running) {
        
        std::getline(std::cin, message); // waits for user to input

        // Allows for clean exiting
        if (message == "quit") { 
            running = false; // changes global var to stop threads
            break;
        }

        // Send only non-empty messages to the server
        if(!message.empty()) {
            send(sock_fd, message.c_str(), message.length(), 0);
        }
    }
    
    // Closes connection for cleanup and prompts diconnected
    close(sock_fd);
    std::cout << "Disconnected." << std::endl;
    
    return 0;
}