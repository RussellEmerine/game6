#include "Mode.hpp"

#include "Connection.hpp"
#include "Game.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>

struct PlayMode : Mode {
    explicit PlayMode(Client &client);
    
    ~PlayMode() override;
    
    //functions called by main loop:
    bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
    
    void update(float elapsed) override;
    
    void draw(glm::uvec2 const &drawable_size) override;
    
    //----- game state -----
    
    //input tracking for local player:
    Player::Controls controls;
    
    //latest game state (from server):
    Game game;
    
    //last message from server:
    std::string server_message;
    
    //connection to server:
    Client &client;
    
};
