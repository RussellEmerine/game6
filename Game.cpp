#include "Game.hpp"

#include "Connection.hpp"
#include "data_path.hpp"
#include "Load.hpp"
#include "WalkMesh.hpp"

#include <stdexcept>
#include <iostream>
#include <cstring>
#include <ctime>

#include <glm/gtx/norm.hpp>

// TODO: adjust the bounds to the walkmesh
// this really shouldn't be done like this but i'd have to do work to actually make right
glm::vec3 random_coordinates(std::mt19937 &mt) {
    return {
            float(mt()) / float(std::mt19937::max()) * 30.0f - 5.0f,
            float(mt()) / float(std::mt19937::max()) * 10.0f - 5.0f,
            float(mt()) / float(std::mt19937::max()) * 20.0f - 10.0f
    };
}

void Player::Controls::send_controls_message(Connection *connection_) const {
    assert(connection_);
    auto &connection = *connection_;
    
    uint32_t size = 4 + sizeof(float);
    connection.send(Message::C2S_Controls);
    connection.send(uint8_t(size));
    connection.send(uint8_t(size >> 8));
    connection.send(uint8_t(size >> 16));
    
    auto send_button = [&](Button const &b) {
        if (b.downs & 0x80) {
            std::cerr << "Wow, you are really good at pressing buttons!" << std::endl;
        }
        connection.send(uint8_t((b.pressed ? 0x80 : 0x00) | (b.downs & 0x7f)));
    };
    
    send_button(left);
    send_button(right);
    send_button(up);
    send_button(down);
    connection.send(mousex);
}

bool Player::Controls::recv_controls_message(Connection *connection_) {
    assert(connection_);
    auto &connection = *connection_;
    
    auto &recv_buffer = connection.recv_buffer;
    
    //expecting [type, size_low0, size_mid8, size_high8]:
    if (recv_buffer.size() < 4) return false;
    if (recv_buffer[0] != uint8_t(Message::C2S_Controls)) return false;
    uint32_t size = (uint32_t(recv_buffer[3]) << 16)
                    | (uint32_t(recv_buffer[2]) << 8)
                    | uint32_t(recv_buffer[1]);
    if (size != 4 + sizeof(float))
        throw std::runtime_error(
                "Controls message with size " + std::to_string(size) + " != 4 + sizeof(float) + sizeof(float)!");
    
    //expecting complete message:
    if (recv_buffer.size() < 4 + size) return false;
    
    auto recv_button = [](uint8_t byte, Button *button) {
        button->pressed = (byte & 0x80);
        uint32_t d = uint32_t(button->downs) + uint32_t(byte & 0x7f);
        if (d > 255) {
            std::cerr << "got a whole lot of downs" << std::endl;
            d = 255;
        }
        button->downs = uint8_t(d);
    };
    
    recv_button(recv_buffer[4 + 0], &left);
    recv_button(recv_buffer[4 + 1], &right);
    recv_button(recv_buffer[4 + 2], &up);
    recv_button(recv_buffer[4 + 3], &down);
    {
        float delta;
        connection.recv(4 + 4, delta);
        mousex += delta;
    }
    
    //delete message from buffer:
    recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);
    
    return true;
}


//-----------------------------------------

// moved this out of a load that would have to be in a header to avoid linker errors
WalkMeshes const *world_walkmeshes = nullptr;

Game::Game() : mt(std::time(nullptr)) {
    // clang-tidy doesn't like how i initialize the rng, interesting
    if (world_walkmeshes == nullptr) {
        world_walkmeshes = new WalkMeshes(data_path("world.w"));
    }
    
    walkmesh = &world_walkmeshes->lookup("WalkMesh");
    assert(walkmesh && "walkmesh not initialized");
    
    for (size_t i = 0; i < SheepCount; i++) {
        sheeps.emplace_back();
        Sheep &sheep = sheeps.back();
        
        sheep.at = walkmesh->nearest_walk_point(random_coordinates(mt));
        // TODO: randomize the angle
        sheep.rotation = glm::rotation(
                glm::vec3(0.0f, 0.0f, 1.0f),
                walkmesh->to_world_smooth_normal(sheep.at)
        );
    }
}

Player *Game::spawn_player() {
    players.emplace_back();
    Player &player = players.back();
    
    player.at = walkmesh->nearest_walk_point(random_coordinates(mt));
    // TODO: randomize the angle
    player.rotation = glm::rotation(
            glm::vec3(0.0f, 0.0f, 1.0f),
            walkmesh->to_world_smooth_normal(player.at)
    );
    
    player.name = "ClientPlayer " + std::to_string(next_player_number++);
    
    return &player;
}

void Game::remove_player(Player *player) {
    bool found = false;
    for (auto pi = players.begin(); pi != players.end(); ++pi) {
        if (&*pi == player) {
            players.erase(pi);
            found = true;
            break;
        }
    }
    assert(found);
}

void Game::update(float elapsed) {
    //position/velocity update:
    for (auto &player: players) {
        // part paraphrased, part copied, from game5 base code:
        glm::vec3 move = glm::vec3(0.0f, 0.0f, 0.0f);
        if (player.controls.left.pressed) move.x -= 1.0f;
        if (player.controls.right.pressed) move.x += 1.0f;
        if (player.controls.down.pressed) move.y -= 1.0f;
        if (player.controls.up.pressed) move.y += 1.0f;
        
        if (glm::length(move) > 0) {
            move = glm::normalize(move) * PlayerSpeed * elapsed;
        }
        
        glm::vec3 remain = player.rotation * move;
        
        for (size_t i = 0; i < 10; i++) {
            if (remain == glm::vec3(0.0f, 0.0f, 0.0f)) {
                break;
            }
            WalkPoint end;
            float time;
            
            walkmesh->walk_in_triangle(player.at, remain, &end, &time);
            player.at = end;
            if (time == 1.0f) {
                remain = glm::vec3(0.0f, 0.0f, 0.0f);
                break;
            }
            remain *= (1.0f - time);
            glm::quat rotation;
            if (walkmesh->cross_edge(player.at, &end, &rotation)) {
                player.at = end;
                remain = rotation * remain;
            } else {
                glm::vec3 const &a = walkmesh->vertices[player.at.indices.x];
                glm::vec3 const &b = walkmesh->vertices[player.at.indices.y];
                glm::vec3 const &c = walkmesh->vertices[player.at.indices.z];
                glm::vec3 along = glm::normalize(b - a);
                glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
                glm::vec3 in = glm::cross(normal, along);
                
                // check how much 'remain' is pointing out of the triangle:
                float d = glm::dot(remain, in);
                if (d < 0.0f) {
                    // bounce off of the wall:
                    remain += (-1.25f * d) * in;
                } else {
                    // if it's just pointing along the edge, bend slightly away from wall:
                    remain += 0.01f * d * in;
                }
            }
        }
        
        if (remain != glm::vec3(0.0f)) {
            std::cout << "NOTE: code used full iteration budget for walking." << std::endl;
        }
        
        // game5 code updates transform position here, we don't to that
        // since the client will update position based on sent walkpoints
        
        // update the rotation due to moving across triangles, this has to sent
        {
            glm::quat adjust = glm::rotation(
                    player.rotation * glm::vec3(0.0f, 0.0f, 1.0f), //current up vector
                    walkmesh->to_world_smooth_normal(player.at) //smoothed up vector at walk location
            );
            player.rotation = glm::normalize(adjust * player.rotation);
        }
        
        // but also update the rotation according to the input (this is only the yaw, since pitch is handled by the client)
        {
            // TODO: make mouse speed a constant
            // TODO: the game5 base code uses player.camera->fovy, maybe want to use that instead somehow
            player.rotation = glm::angleAxis(
                    -1.2f * player.controls.mousex,
                    walkmesh->to_world_smooth_normal(player.at)
            ) * player.rotation;
        }
        
        //reset 'downs' since controls have been handled:
        player.controls.left.downs = 0;
        player.controls.right.downs = 0;
        player.controls.up.downs = 0;
        player.controls.down.downs = 0;
        player.controls.mousex = 0;
    }
    
    // TODO: sheep movement
}


void Game::send_state_message(Connection *connection_, Player *connection_player) const {
    assert(connection_);
    auto &connection = *connection_;
    
    connection.send(Message::S2C_State);
    //will patch message size in later, for now placeholder bytes:
    connection.send(uint8_t(0));
    connection.send(uint8_t(0));
    connection.send(uint8_t(0));
    size_t mark = connection.send_buffer.size(); //keep track of this position in the buffer
    
    
    //send player info helper:
    auto send_player = [&](Player const &player) {
        connection.send(player.at);
        connection.send(player.rotation);
        
        //NOTE: can't just 'send(name)' because player.name is not plain-old-data type.
        //effectively: truncates player name to 255 chars
        uint8_t len = uint8_t(std::min<size_t>(255, player.name.size()));
        connection.send(len);
        connection.send_buffer.insert(connection.send_buffer.end(), player.name.begin(), player.name.begin() + len);
    };
    
    //player count:
    connection.send(uint8_t(players.size()));
    if (connection_player) send_player(*connection_player);
    for (auto const &player: players) {
        if (&player == connection_player) continue;
        send_player(player);
    }
    
    connection.send(uint8_t(sheeps.size()));
    for (auto const &sheep: sheeps) {
        connection.send(sheep.at);
        connection.send(sheep.rotation);
    }
    
    //compute the message size and patch into the message header:
    auto size = uint32_t(connection.send_buffer.size() - mark);
    connection.send_buffer[mark - 3] = uint8_t(size);
    connection.send_buffer[mark - 2] = uint8_t(size >> 8);
    connection.send_buffer[mark - 1] = uint8_t(size >> 16);
}

bool Game::recv_state_message(Connection *connection_) {
    assert(connection_);
    auto &connection = *connection_;
    auto &recv_buffer = connection.recv_buffer;
    
    if (recv_buffer.size() < 4) return false;
    if (recv_buffer[0] != uint8_t(Message::S2C_State)) return false;
    uint32_t size = (uint32_t(recv_buffer[3]) << 16)
                    | (uint32_t(recv_buffer[2]) << 8)
                    | uint32_t(recv_buffer[1]);
    uint32_t at = 0;
    //expecting complete message:
    if (recv_buffer.size() < 4 + size) return false;
    
    //copy bytes from buffer and advance position:
    auto read = [&](auto *val) {
        if (at + sizeof(*val) > size) {
            throw std::runtime_error("Ran out of bytes reading state message.");
        }
        std::memcpy(val, &recv_buffer[4 + at], sizeof(*val));
        at += sizeof(*val);
    };
    
    players.clear();
    uint8_t player_count;
    read(&player_count);
    for (uint8_t i = 0; i < player_count; ++i) {
        players.emplace_back();
        Player &player = players.back();
        read(&player.at);
        read(&player.rotation);
        uint8_t name_len;
        read(&name_len);
        //n.b. would probably be more efficient to directly copy from recv_buffer, but I think this is clearer:
        player.name = "";
        for (uint8_t n = 0; n < name_len; ++n) {
            char c;
            read(&c);
            player.name += c;
        }
    }
    
    sheeps.clear();
    uint8_t sheep_count;
    read(&sheep_count);
    for (uint8_t i = 0; i < sheep_count; ++i) {
        sheeps.emplace_back();
        Sheep &sheep = sheeps.back();
        read(&sheep.at);
        read(&sheep.rotation);
    }
    
    if (at != size) throw std::runtime_error("Trailing data in state message.");
    
    //delete message from buffer:
    recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);
    
    return true;
}
