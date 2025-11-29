/*
Rock-Paper-Scissors Multiplayer Game Server

A TCP-based game server that handles multiple concurrent players using select()
for I/O multiplexing. Features matchmaking, game state management, and graceful
disconnect handling.

Key Concepts Demonstrated:
- Socket programming (TCP server implementation)
- select() for handling multiple clients without threading
- Game state machines (player states, game states)
- Memory management (dynamic allocation of Player/Game objects)
 */

#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include <map>

// ------------------- Enums -------------------

// Tracks the overall state of current game
enum class GameState {
    MATCHMAKING,
    ROUND_ACTIVE,
    ROUND_COMPLETE,
    GAME_OVER
};

// Tracks what each player is currently doing
enum class PlayerState {
    CONNECTED,          // Just connected, can join queue
    IN_QUEUE,           // Waiting for matchmaking
    IN_GAME_CHOOSING,   // Making rock/paper/scissors choice
    IN_GAME_WAITING,    // Waiting for opponent's choice
    VIEWING_RESULTS     // Viewing round results, can ready up
};

// Represents player choice for game
enum class Choice {
    NONE,
    ROCK,
    PAPER,
    SCISSORS
};

// ------------------- Structs -------------------

// Connected player
struct Player {
    int socket;           // Socket file descriptor for player
    std::string name;     // Player's username
    PlayerState state;    // Current state in the game flow

    Player(int sock, std::string n)
        : socket(sock), name(n), state(PlayerState::CONNECTED) {}
};

// Active game between two player
struct Game {
    int player1_socket;
    int player2_socket;
    std::string player1_name;
    std::string player2_name;

    Choice choice1;
    Choice choice2;
    
    // Works by best of 3
    int score1;
    int score2;

    GameState state;

    Game (Player* p1, Player* p2)
        : player1_socket(p1->socket),
        player2_socket(p2->socket),
        player1_name(p1->name),
        player2_name(p2->name),
        choice1(Choice::NONE),
        choice2(Choice::NONE),
        score1(0),
        score2(0),
        state(GameState::ROUND_ACTIVE) {}

    // Checks for both players making a choice
    bool bothChosen() {
        return choice1 != Choice::NONE && choice2 != Choice::NONE;
    }

    // Determine the winner of round
    // 0 = tie, 1 = player 1 wins, 2 = player 2 wins
    int getRoundWinner() {
        if (choice1 == choice2) return 0; // Tie

        if(choice1 == Choice::ROCK && choice2 == Choice::SCISSORS) return 1;
        if(choice1 == Choice::PAPER && choice2 == Choice::ROCK) return 1;
        if(choice1 == Choice::SCISSORS && choice2 == Choice::PAPER) return 1;

        return 2; // otherwise Player 2 wins
    }

    // Checks if the game is over (For best of 3)
    bool isGameOver() {
        return score1 >= 2 || score2 >= 2;
    }

    // Resets the vars and state for next round
    void resetRound() {
        choice1 = Choice::NONE;
        choice2 = Choice::NONE;
        state = GameState::ROUND_ACTIVE;
    }
};

// ------------------- Global State ------------------- 
std::vector<int> matchmaking_queue; // players waiting for a match
std::map<int, Game*> active_game;   // socket -> current game (both players point to same object)
std::map<int, Player*> players;     // socket -> player object


// ------------------- Helper Functions ------------------- 

// Sends message to both players
void broadcast(const std::string& message, int socket1, int socket2) {
    int sent1 = send(socket1, message.c_str(), message.length(), 0);
    int sent2 = send(socket2, message.c_str(), message.length(), 0);
}

// Convert string command to Choice enum
Choice stringToChoice(const std::string& str) {
    if(str == "rock") return Choice::ROCK;
    if(str == "scissors") return Choice::SCISSORS;
    if(str == "paper") return Choice::PAPER;
    return Choice::NONE;
}

// Convert Choice enum to string for display
std::string choiceToString(Choice c) {
    if(c == Choice::ROCK) return "rock";
    if(c == Choice::PAPER) return "paper";
    if(c == Choice::SCISSORS) return "scissors";
    return "none";
}

// Handles when player disconnects
void handleDisconnect(int socket) {
    // Gets player info before
    if (players.find(socket) == players.end()) {
        std::cout << "Warning: Tried to disconnect unknown socket " << socket << std::endl;
        close(socket);
        return;
    }
    Player* player = players[socket];
    std::string name = player->name.empty() ? "Unknown" : player->name;

    std::cout << name << " (socket " << socket << ") disconnected" << std::endl;

    // ---- CASE 1: Player in Queue ----
    // removes from matchmaking queue
    auto queue_position = std::find(matchmaking_queue.begin(), matchmaking_queue.end(), socket);
    if (queue_position != matchmaking_queue.end()) {
        matchmaking_queue.erase(queue_position);
        std::cout << name << " removed from matchmaking queue" << std::endl;
    }

    // ---- CASE 2: Player in Active Game ----
    
    //Checks if player was in a game
    if (active_game.find(socket) != active_game.end()) {
        Game* game = active_game[socket];

        // Finds the opponent socket
        int opponent_socket;
        std::string opponent_name;
        if (socket == game->player1_socket) {
            opponent_socket = game->player2_socket;
            opponent_name = game->player2_name;
        } else {
            opponent_socket = game->player1_socket;
            opponent_name = game->player1_name;
        }

        // Notify opponent of disconnect
        if (players.find(opponent_socket) != players.end()) {
            std::string msg = "\n--- OPPONENT DISCONNECTED ---\n";
            // wins by forfeit
            msg += "Your opponent, " + opponent_name + ", has left the game. You win by forfeit\n";
            msg += "Type 'join' to find a new match\n";
            
            send(opponent_socket, msg.c_str(), msg.length(), 0);

            // Reset opponent state to CONNECTED
            Player* opponent = players[opponent_socket];
            opponent->state = PlayerState::CONNECTED;
            active_game.erase(opponent_socket);
        }
        
        // Cleans up game object
        active_game.erase(socket);
        delete game;
        std::cout << "Game cleaned up due to disconnect" << std::endl;
    }

    // Ensures closing and erasing of player
    close(socket);
    delete player;
    players.erase(socket);
}

// Validates if player is in state, sends error if not
bool requireState(int socket, Player* player, PlayerState required_state) {
    if (player->state != required_state) {
        std::string msg;
        
        // Gives conextual messages to player
        switch(player->state) {
            case PlayerState::CONNECTED:
                msg = "You're not in a game! Type 'join' to play.\n";
                break;
            case PlayerState::IN_QUEUE:
                msg = "You're in queue. Please wait for a match.\n";
                break;
            case PlayerState::IN_GAME_CHOOSING:
                msg = "Invalid command! Type: rock, paper, or scissors\n";
                break;
            case PlayerState::IN_GAME_WAITING:
                msg = "Waiting for opponent to choose...\n";
                break;
            case PlayerState::VIEWING_RESULTS:
                msg = "Type 'ready' for next round!\n";
                break;
        }
        
        send(socket, msg.c_str(), msg.length(), 0);
        return false;  // State check failed
    }
    return true;  // State is correct
}

// Handles 'join' -> adds player to queue and match
void handleJoinCommand(int socket, Player* player) {
    player->state = PlayerState::IN_QUEUE;
    matchmaking_queue.push_back(socket);

    std::string msg = "Joined matchmaking queue. Waiting for opponent...\n";
    send(socket, msg.c_str(), msg.length(), 0);

    // Tries to match players if 2+ in queue, create a match
    if (matchmaking_queue.size() >= 2)
    {
        // Gets the first two players
        int p1_sock = matchmaking_queue[0];
        int p2_sock = matchmaking_queue[1];

        Player *p1 = players[p1_sock];
        Player *p2 = players[p2_sock];

        // Creates new game
        Game *game = new Game(p1, p2);
        active_game[p1_sock] = game;
        active_game[p2_sock] = game;

        // Updates states
        p1->state = PlayerState::IN_GAME_CHOOSING;
        p2->state = PlayerState::IN_GAME_CHOOSING;

        // Remove players from queue
        matchmaking_queue.erase(matchmaking_queue.begin(), matchmaking_queue.begin() + 2);

        // Notify both players
        std::string match_msg = "\n--- MATCH FOUND ---\n";
        match_msg += "Playing against: ";

        std::string p1_msg = match_msg + p2->name + "\n";
        p1_msg += "Choose: rock, paper, or scissors\n";
        send(p1_sock, p1_msg.c_str(), p1_msg.length(), 0);

        std::string p2_msg = match_msg + p1->name + "\n";
        p2_msg += "Choose: rock, paper, or scissors\n";
        send(p2_sock, p2_msg.c_str(), p2_msg.length(), 0);
    }
}

// Handles rock/paper/scissors choice -> stores choice and checks for both chosen
void handleChoiceCommand(int socket, Player* player, const std::string& command) {

    Game *game = active_game[socket];
    Choice choice = stringToChoice(command);

    // Stores choice based on player
    if (socket == game->player1_socket)
    {
        game->choice1 = choice;

        player->state = PlayerState::IN_GAME_WAITING;
        std::string msg_player = "Choice locked in! Waiting for opponent...\n";
        send(socket, msg_player.c_str(), msg_player.length(), 0);
    }
    else
    {
        game->choice2 = choice;

        player->state = PlayerState::IN_GAME_WAITING;
        std::string msg_player = "Choice locked in! Waiting for opponent...\n";
        send(socket, msg_player.c_str(), msg_player.length(), 0);
    }

    // if both players have chosen, resolves the current round
    if (game->bothChosen())
    {
        // Determines who won
        int winner = game->getRoundWinner();

        // Updates scores
        if (winner == 1)
            game->score1++;
        if (winner == 2)
            game->score2++;

        game->state = GameState::ROUND_COMPLETE;

        // Build result message
        std::string result = "\n--- ROUND RESULT ---\n";
        result += game->player1_name + " chose: " + choiceToString(game->choice1) + "\n";
        result += game->player2_name + " chose: " + choiceToString(game->choice2) + "\n";

        if (winner == 0)
        {
            result += "It's a TIE!\n";
        }
        else if (winner == 1)
        {
            result += game->player1_name + " WINS this round!\n";
        }
        else
        {
            result += game->player2_name + " WINS this round!\n";
        }

        result += "\nScore: " + game->player1_name + " " + std::to_string(game->score1);
        result += " - " + std::to_string(game->score2) + " " + game->player2_name + "\n";

        // Checks if the game is over
        if (game->isGameOver()) {
            game->state = GameState::GAME_OVER;
            result += "\n--- GAME OVER --- \n";

            if (game->score1 > game->score2) {
                result += game->player1_name + " WINS THE MATCH!\n";
            } else {
                result += game->player2_name + " WINS THE MATCH!\n";
            }

            result += "\nType 'join' to play again or 'quit' to leave\n";

            // Send to both
            broadcast(result, game->player1_socket, game->player2_socket);

            // Resets players to CONNECTED state
            Player *p1 = players[game->player1_socket];
            Player *p2 = players[game->player2_socket];
            p1->state = PlayerState::CONNECTED;
            p2->state = PlayerState::CONNECTED;

            // Cleans up the game
            active_game.erase(game->player1_socket);
            active_game.erase(game->player2_socket);
            delete game;
        } else {
            // Proceeds to Next Round
            result += "\nType 'ready' for next round!\n";
            broadcast(result, game->player1_socket, game->player2_socket);

            // Updates states to viewing results
            Player *p1 = players[game->player1_socket];
            Player *p2 = players[game->player2_socket];
            p1->state = PlayerState::VIEWING_RESULTS;
            p2->state = PlayerState::VIEWING_RESULTS;
        }
    }
}

// Handles 'ready' -> starts next round when both players ready
void handleReadyCommand(int socket, Player* player) {
    Game *game = active_game[socket];

    // Marks the player as ready
    player->state = PlayerState::IN_GAME_CHOOSING;

    Player *p1 = players[game->player1_socket];
    Player *p2 = players[game->player2_socket];

    // if both players are ready, starts new round
    if (p1->state == PlayerState::IN_GAME_CHOOSING &&
        p2->state == PlayerState::IN_GAME_CHOOSING) {
        game->resetRound();

        std::string msg = "\n--- NEW ROUND---\n";
        msg += "Type: rock, paper, or scissors\n";
        broadcast(msg, game->player1_socket, game->player2_socket);
    } else {
        // This player is ready, waiting on their opponent
        std::string msg_waiting = "Ready! Waiting for opponent...\n";
        send(socket, msg_waiting.c_str(), msg_waiting.length(), 0);
    }
}

// ------------------- Main -------------------

int main() {
// ----- Socket Setup -----
    // Create TCP socket
    // uses AF_INET/IPv4 and SOCK_STREAM/TCP (stream oriented connection) for reliability
    int server_fd = socket(AF_INET, SOCK_STREAM, 0); 
    if (server_fd == -1) { // if returned -1 the stops and fails creation
        std::cerr << "Socket creation failed!" << std::endl;
        return 1;
    }

    // Allows for resuse of address/port (saves time for quick restarts)
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt failed!" << std::endl;
        return 1;
    }
    
    // Configure server address
    sockaddr_in address;
    address.sin_family = AF_INET;         // IPv4
    address.sin_addr.s_addr = INADDR_ANY; // Accept connections on any local network
    address.sin_port = htons(8080);       // hosts on port 8080 using htons (Host To Network Short)
    
    // Bind socket to port 
    if (::bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed!" << std::endl;
        return 1;
    }
    
    // Listen for connections that are incoming
    // Second parameter catches if max queued connections
    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed!" << std::endl;
        return 1;
    }
    
    std::cout << "Server listening on port 8080..." << std::endl;
    

    // ----- SELECT() LOOP -----

    // select() requires fd_set to track which sockets to monitor
    fd_set read_fds; // set the file descriptors to monitor to read the activity

    // Main Server loop
    while (true) { // Accepts and handles clients through select
        
        // ---- Prepare FD_SET ----

        // Clear the set and rebuilds fd_set each iteration
        FD_ZERO(&read_fds);

        // Add server socket to set, for new connections
        FD_SET(server_fd, &read_fds);
        int max_fd = server_fd; // used in select, records highest FD number

        // Add all connected client sockets to detect messages sent
        for (auto& pair : players) {
            int socket = pair.first;
            FD_SET(socket, &read_fds); // sets each client
            // if the client is above the max, set new max
            if (socket > max_fd) max_fd = socket;
        }

        // ---- Wait for Activity ----

        // Block until activity on any socket
        // select() returns when: new connection, client msg, or client disconnect
        // Parameters:
        // max_fd, read set, write set, exception set, timeout
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        if (activity < 0) { // Calls error if nothing is selected
            std::cerr << "Select error" << std::endl;
            continue; // Attempts call again
        }

        // Checks if server socket has activity 
        // FD_ISSET is used to check for activity
        if (FD_ISSET(server_fd, &read_fds)) {
            int addrlen = sizeof(address);
            
            // accepts client through creating new socket for the connection
            int new_socket = accept(server_fd, (sockaddr*)&address, (socklen_t*)&addrlen);

            if (new_socket < 0) { // catches if not valid client
                std::cerr << "Accept failed!" << std::endl;
                continue;
            }

            // Creates new player
            Player* player = new Player(new_socket, "");
            players[new_socket] = player;

            std::cout << "New client connected (socket " << new_socket << ")" << std::endl;
            
        }

        // ---- Check all clients for activity ----
        // builds a list first for sockets to check to handle disconnect
        std::vector<int> sockets_to_check;
        for (auto& pair : players) {
            sockets_to_check.push_back(pair.first);
        }

        for (int socket : sockets_to_check) {
            // verify player still exists
            if (players.find(socket) == players.end()) {
                continue;
            }

            // Checks for if data is ready
            if (FD_ISSET(socket, &read_fds)) {
                Player* player = players[socket];

                // Reads data from given player
                const int BUFFER_SIZE = 1024;
                char buffer[BUFFER_SIZE];
                memset(buffer, 0, sizeof(buffer));
                int valread = read(socket, buffer, BUFFER_SIZE);

                // Checks for disconnection
                if (valread <= 0) { // if 0 = disconnection, 0 > means error
                    // Client disconnected
                    handleDisconnect(socket);
                } else {     
                    // Player sent message
                    std::string message(buffer);

                    // strips trailing newline/whitespace
                    message.erase(message.find_last_not_of(" \n\r\t") + 1); 

                    if (player->name.empty()) {
                        // This is the username
                        player->name = message;  
                        std::cout << message << " has connected!" << std::endl;

                        // Send game instructions
                        std::string menu = "\n--- Rock Paper Scissors ---\n";
                        menu += "Commands:\n";
                        menu += "join - Join matchmaking queue\n";
                        menu += "rock/paper/scissors - make your chioce\n";
                        menu += "quit - Exits the game\n";

                        send(socket, menu.c_str(), menu.length(), 0);
                    } else {
                        // ---- Command Parsing ----

                        // Parse the command (lowercase for easier use)
                        std::string command = message;
                        std::transform(command.begin(), command.end(), command.begin(), ::tolower);

                        std::cout << player->name << " sent: " << command << std::endl;

                        // ---- Handle Commands ----

                        if(command == "join") {
                            // Player is looking to join matchmaking
                            if (!requireState(socket, player, PlayerState::CONNECTED)) {
                                continue; // continues early
                            }
                            handleJoinCommand(socket, player);
                        }
                        else if (command == "rock" || command == "paper" || command == "scissors")
                        {
                            // player choosing
                            if (!requireState(socket, player, PlayerState::IN_GAME_CHOOSING)) {
                                continue; // continues early
                            }

                            handleChoiceCommand(socket, player, command);
                        }
                        else if (command == "ready")
                        {
                            // Player is ready for next round
                            if (!requireState(socket, player, PlayerState::VIEWING_RESULTS)) {
                                continue; // continues early
                            }
                            handleReadyCommand(socket, player);
                        }
                        else if (command == "quit")
                        {
                            // Handles quit
                            std::string msg = "Goodbye!\n";
                            send(socket, msg.c_str(), msg.length(), 0);

                            handleDisconnect(socket);
                        }
                        else
                        {
                            // Not valid command -> gives contextual help
                            std::string msg = "Unknown command. ";

                            if(player->state == PlayerState::CONNECTED) {
                                msg += "Type 'join' to play!\n";
                            } else if(player->state == PlayerState::IN_QUEUE) {
                                msg += "You're in queue. Please wait for a match.\n";
                            } else if(player->state == PlayerState::IN_GAME_CHOOSING) {
                                msg += "Invalid choice! Type: rock, paper, or scissors\n";
                            } else if(player->state == PlayerState::IN_GAME_WAITING) {
                                msg += "Waiting for opponent to choose...";
                            } else if(player->state == PlayerState::VIEWING_RESULTS) {
                                msg += "Type 'ready' for next round!\n";
                            } else {
                                msg += "Type 'join' to play!\n";
                            }

                            send(socket, msg.c_str(), msg.length(), 0);
                        }
                    }
                }
            }
        }
    }
    
    // Never used but allows for better closing of server
    close(server_fd); 
    return 0; 
}