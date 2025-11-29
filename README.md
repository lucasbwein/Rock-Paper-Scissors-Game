# Rock-Paper-Scissors Multiplayer Game Server

A TCP-based multiplayer rock-paper-scissors game demonstrating socket programming, I/O multiplexing with `select()`, and server-side game state management in C++.

## 🎯 Overview

This project implements a complete client-server architecture for a real-time multiplayer game. The server handles concurrent players without threading by using `select()` for I/O multiplexing, manages matchmaking queues, tracks game state across multiple sessions, and handles graceful disconnections.

## 🏗️ Technical Architecture

### Server Design
- **I/O Multiplexing**: Uses `select()` to monitor multiple sockets in a single thread, eliminating the need for thread-per-client architecture
- **Event-Driven**: Non-blocking select() loop responds to connection requests, player commands, and disconnections
- **State Management**: Implements finite state machines for both players and games
- **Memory Safety**: Proper cleanup of dynamically allocated Player and Game objects

### Key Concepts Demonstrated
- TCP socket programming (socket, bind, listen, accept)
- `select()` for efficient multiplexing of file descriptors
- Game state machines (PlayerState, GameState, Choice enums)
- Matchmaking queue implementation
- Broadcasting messages to multiple clients
- Graceful disconnect handling with opponent notification
- Dynamic memory management with proper cleanup

## 🚀 Features

- **Real-time Matchmaking**: Automatic pairing of players in queue
- **Best-of-3 Gameplay**: First player to 2 round wins takes the match
- **State Validation**: Context-aware error messages based on player state
- **Disconnect Handling**: Opponents are notified and awarded forfeit victory
- **Command System**: 
  - `join` - Enter matchmaking queue
  - `rock/paper/scissors` - Make game choice
  - `ready` - Continue to next round
  - `quit` - Exit gracefully

## 💻 Technical Stack

- **Language**: C++
- **Networking**: Berkeley sockets API (POSIX)
- **I/O Model**: `select()` multiplexing (server), threading (client)
- **Platform**: Linux/Unix

## 📦 How to Build & Run

### Compile
```bash
# Server (uses select() - no threading needed)
g++ game_server.cpp -o game_server

# Client (uses threads for send/receive)
g++ player.cpp -o player -pthread
```

### Run
```bash
# Terminal 1: Start server
./game_server

# Terminal 2-N: Connect clients
./player
```

## 🎮 Gameplay Flow

1. Connect and enter username
2. Type `join` to enter matchmaking queue
3. When matched, choose `rock`, `paper`, or `scissors`
4. View round results and overall score
5. Type `ready` to continue to next round
6. First to 2 round wins takes the match
7. Type `join` to play again or `quit` to exit

## 🧠 What I Learned

### Networking Fundamentals
- How TCP connections work at the socket level
- The difference between blocking and non-blocking I/O
- Managing client lifecycle (connect, active, disconnect)
- Why `select()` is more scalable than threading for many concurrent connections

### Architecture Decisions
- **Why select() over threading**: Initially considered thread-per-client but `select()` allows a single thread to handle hundreds of connections efficiently
- **State machines**: Using enums for PlayerState and GameState makes the code more maintainable and prevents invalid transitions
- **Shared Game objects**: Both players in a match point to the same Game object, simplifying synchronization

### Debugging Experience
- Discovered TCP message buffering can combine multiple sends into one receive
- Learned importance of using `.length()` instead of hardcoded message lengths
- Implemented hex dumps to debug null byte issues in network data

### Memory Management
- Proper use of `new`/`delete` for dynamic Player and Game objects
- Handling cleanup during unexpected disconnects
- Avoiding memory leaks by ensuring all allocated objects are freed

## 🔧 Technical Challenges Solved

**Challenge 1: Message Display Misalignment**
- **Problem**: One client would receive messages but not display them properly
- **Root Cause**: Hardcoded `send()` lengths sent extra garbage bytes, including null terminators
- **Solution**: Use `.length()` for all string sends to ensure exact byte counts

**Challenge 2: Username Output**
- **Problem**: Both the client and the server were handling inputing usernames causing a double output
- **Root Cause**: Would act two ways where after client side recieves username it is prompted again
- **Solution**: Client properly handles the username and server only stores it instead

**Challenge 3: Port Already in Use**
- **Problem**: Quick server restarts failed due to TIME_WAIT state
- **Solution**: Added `SO_REUSEADDR` socket option for immediate port reuse

## 📂 Project Structure
```
.
├── game_server.cpp    # Main server with game logic
├── player.cpp         # Client implementation
└── README.md          # This file
```

## 🎓 Skills Demonstrated

- C++ programming (STL containers, enums, structs)
- Systems programming (sockets, file descriptors)
- Concurrent programming (select() multiplexing, threading)
- Network protocol design (command parsing, state management)
- Memory management (dynamic allocation, proper cleanup)
- Debugging (network issues, race conditions, memory bugs)

## 🔮 Future Enhancements

- Add spectator mode
- Support custom game modes (best of 5, sudden death)
- Create lobby system for rematch with same opponent
- Add chat functionality between rounds
- Implement game statistics and leaderboard

## 📝 License

This project was created for learning purposes as part of my portfolio demonstrating socket programming and game server development skills.

---

**Built by Lucas Weinstein**  
