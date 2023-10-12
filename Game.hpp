#pragma once

#include "WalkMesh.hpp"
#include "Load.hpp"

#include <glm/glm.hpp>

#include <string>
#include <list>
#include <random>

struct Connection;

// Game state, separate from rendering.

// Currently set up for a "client sends controls" / "server sends whole state" situation.

enum class Message : uint8_t {
    C2S_Controls = 1, // Greg!         jim i don't get it, what does greg mean
    S2C_State = 's',
};

// used to represent a control input:
struct Button {
    uint8_t downs = 0; // times the button has been pressed
    bool pressed = false; // is the button pressed now
};

// state of one player in the game:
struct Player {
    // player inputs (sent from client):
    struct Controls {
        Button left, right, up, down;
        float mousex = 0.0f;
        
        void send_controls_message(Connection *connection) const;
        
        // returns 'false' if no message or not a controls message,
        // returns 'true' if read a controls message,
        // throws on malformed controls message
        bool recv_controls_message(Connection *connection);
    } controls;
    
    // player state (sent from server):
    WalkPoint at;
    glm::quat rotation;
    std::string name;
};

// state of one sheep in the game
struct Sheep {
    // sheep state (sent from server):
    WalkPoint at;
    glm::quat rotation;
    
    /*
     * Note: the rotations for the players and the sheep work differently. For sheep, forwards is
     * on the mesh x-axis, while for players, forwards is on the mesh y-axis. I'm sure there's an
     * obvious reason for this in the code, but I just fixed it by rotating the model in blender.
     * If this game is to be expanded into something more complete, this should be investigated.
     */
    
    // the rest is unused by clients, only on server side
    
    // a random direction to go if there's nothing nearby
    // gets updated to a random value at random intervals, with average update time every 60 ticks
    glm::vec3 bias;
};

struct Game {
    std::list<Player> players; // (using list so they can have stable addresses)
    Player *spawn_player(); // add player the end of the players list (may also, e.g., play some spawn anim)
    void remove_player(Player *); // remove player from game (may also, e.g., play some despawn anim)
    
    std::list<Sheep> sheeps;
    
    std::mt19937 mt; // used for spawning players
    uint32_t next_player_number = 1; // used for naming players
    
    WalkMesh const *walkmesh;
    
    Game();
    
    // state update function:
    void update(float elapsed);
    
    // constants:
    // the update rate on the server:
    inline static constexpr float Tick = 1.0f / 30.0f;
    
    // player constants:
    
    inline static constexpr float PlayerSpeed = 2.0f;
    inline static constexpr float MouseSpeed = 1.2f;
    
    // used to detect when to hide players that are too close to yourself
    inline static constexpr float PlayerRadius = 1.06f;
    
    // sheep constants
    inline static constexpr size_t SheepCount = 15;
    inline static constexpr float SheepDetectPlayerRadius = 12.0f;
    inline static constexpr float SheepAvoidPlayerConstant = 60.0f;
    inline static constexpr float SheepDetectSheepRadius = 2.0f;
    inline static constexpr float SheepAvoidSheepConstant = 30.0f;
    inline static constexpr float SheepSpeed = 1.5f;
    
    // ---- communication helpers ----
    
    // used by client:
    // set game state from data in connection buffer
    // (return true if data was read)
    bool recv_state_message(Connection *connection);
    
    // used by server:
    // send game state.
    //  Will move "connection_player" to the front of the front of the sent list.
    void send_state_message(Connection *connection, Player *connection_player = nullptr) const;
    
    // used for spawning things randomly
    glm::vec3 min_bound = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 max_bound = glm::vec3(0.0f, 0.0f, 0.0f);
    
    glm::vec3 random_coordinates();
};
