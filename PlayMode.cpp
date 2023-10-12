#include "PlayMode.hpp"

#include "DrawLines.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "hex_dump.hpp"

#include "Scene.hpp"
#include "Mesh.hpp"
#include "data_path.hpp"
#include "Load.hpp"
#include "WalkMesh.hpp"
#include "LitColorTextureProgram.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

#include <random>
#include <array>

GLuint world_meshes_for_lit_color_texture_program = 0;
Load<MeshBuffer> world_meshes(LoadTagDefault, []() -> MeshBuffer const * {
    MeshBuffer const *ret = new MeshBuffer(data_path("world.pnct"));
    world_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
    return ret;
});

Scene::Drawable::Pipeline player_pipeline, sheep_pipeline;
Load<Scene> world_scene(LoadTagDefault, []() -> Scene const * {
    return new Scene(
            data_path("world.scene"),
            [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name) {
                Mesh const &mesh = world_meshes->lookup(mesh_name);
                
                Scene::Drawable::Pipeline pipeline = lit_color_texture_program_pipeline;
                pipeline.vao = world_meshes_for_lit_color_texture_program;
                pipeline.type = mesh.type;
                pipeline.start = mesh.start;
                pipeline.count = mesh.count;
                
                if (transform->name == "Player") {
                    player_pipeline = pipeline;
                } else if (transform->name == "Sheep") {
                    sheep_pipeline = pipeline;
                } else {
                    scene.drawables.emplace_back(transform);
                    scene.drawables.back().pipeline = pipeline;
                }
            });
});

PlayMode::PlayMode(Client &client_) : client(client_), scene(*world_scene) {
    // create a player transform:
    scene.transforms.emplace_back();
    player.transform = &scene.transforms.back();
    
    // create a player camera attached to a child of the player transform:
    scene.transforms.emplace_back();
    scene.cameras.emplace_back(&scene.transforms.back());
    player.camera = &scene.cameras.back();
    player.camera->fovy = glm::radians(60.0f);
    player.camera->near = 0.01f;
    player.camera->transform->parent = player.transform;
    
    // player's eyes are 1.8 units above the ground:
    player.camera->transform->position = glm::vec3(0.0f, 0.0f, 1.8f);
    
    // rotate camera facing direction (-z) to player facing direction (+y):
    player.camera->transform->rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    
    // start player walking at nearest walk point:
    player.at = game.walkmesh->nearest_walk_point(player.transform->position);
}

PlayMode::~PlayMode() = default;

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
    if (evt.type == SDL_KEYDOWN) {
        if (evt.key.repeat) {
            //ignore repeats
        } else if (evt.key.keysym.sym == SDLK_a) {
            controls.left.downs += 1;
            controls.left.pressed = true;
            return true;
        } else if (evt.key.keysym.sym == SDLK_d) {
            controls.right.downs += 1;
            controls.right.pressed = true;
            return true;
        } else if (evt.key.keysym.sym == SDLK_w) {
            controls.up.downs += 1;
            controls.up.pressed = true;
            return true;
        } else if (evt.key.keysym.sym == SDLK_s) {
            controls.down.downs += 1;
            controls.down.pressed = true;
            return true;
        } else if (evt.key.keysym.sym == SDLK_ESCAPE) {
            SDL_SetRelativeMouseMode(SDL_FALSE);
            return true;
        }
    } else if (evt.type == SDL_KEYUP) {
        if (evt.key.keysym.sym == SDLK_a) {
            controls.left.pressed = false;
            return true;
        } else if (evt.key.keysym.sym == SDLK_d) {
            controls.right.pressed = false;
            return true;
        } else if (evt.key.keysym.sym == SDLK_w) {
            controls.up.pressed = false;
            return true;
        } else if (evt.key.keysym.sym == SDLK_s) {
            controls.down.pressed = false;
            return true;
        }
    } else if (evt.type == SDL_MOUSEBUTTONDOWN) {
        if (SDL_GetRelativeMouseMode() == SDL_FALSE) {
            SDL_SetRelativeMouseMode(SDL_TRUE);
            return true;
        }
    } else if (evt.type == SDL_MOUSEMOTION) {
        if (SDL_GetRelativeMouseMode() == SDL_TRUE) {
            glm::vec2 motion = glm::vec2(
                    float(evt.motion.xrel) / float(window_size.y),
                    -float(evt.motion.yrel) / float(window_size.y)
            );
            
            // update yaw, handled on server side
            controls.mousex += motion.x;
            
            // and update pitch, handled on client side
            float pitch = glm::pitch(player.camera->transform->rotation);
            pitch += motion.y * player.camera->fovy;
            //camera looks down -z (basically at the player's feet) when pitch is at zero.
            pitch = std::min(pitch, 0.95f * 3.1415926f);
            pitch = std::max(pitch, 0.05f * 3.1415926f);
            player.camera->transform->rotation = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
            
            return true;
        }
    }
    
    return false;
}

void PlayMode::update(float elapsed) {
    //queue data for sending to server:
    controls.send_controls_message(&client.connection);
    
    //reset button press counters:
    controls.left.downs = 0;
    controls.right.downs = 0;
    controls.up.downs = 0;
    controls.down.downs = 0;
    controls.mousex = 0.0f;
    
    //send/receive data:
    client.poll([this](Connection *c, Connection::Event event) {
        if (event == Connection::OnOpen) {
            std::cout << "[" << c->socket << "] opened" << std::endl;
        } else if (event == Connection::OnClose) {
            std::cout << "[" << c->socket << "] closed (!)" << std::endl;
            throw std::runtime_error("Lost connection to server!");
        } else {
            assert(event == Connection::OnRecv);
            //std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n" << hex_dump(c->recv_buffer); std::cout.flush(); //DEBUG
            bool handled_message;
            try {
                do {
                    handled_message = false;
                    if (game.recv_state_message(c)) handled_message = true;
                } while (handled_message);
            } catch (std::exception const &e) {
                std::cerr << "[" << c->socket << "] malformed message from server: " << e.what() << std::endl;
                //quit the game:
                throw e;
            }
        }
    }, 0.0);
    
    // Set my own position
    // The server always sends with the current player first
    player.at = game.players.front().at;
    player.transform->position = game.walkmesh->to_world_point(player.at);
    player.transform->rotation = game.players.front().rotation;
    
    // Draw the other players
    // maybe it's a little slow to do this each frame - but whatever, computers are fast these days
    scene.drawables.remove_if([](Scene::Drawable &drawable) {
        return drawable.transform->name == "Player" || drawable.transform->name == "Sheep";
    });
    scene.transforms.remove_if([](Scene::Transform &transform) {
        return transform.name == "Player" || transform.name == "Sheep";
    });
    
    for (Player &other: game.players) {
        // only display players that aren't too close to myself, basic attempt to avoid ugliness of being inside other things
        if (glm::distance(game.walkmesh->to_world_point(player.at), game.walkmesh->to_world_point(other.at))
            > Game::PlayerRadius) {
            scene.transforms.emplace_back();
            Scene::Transform &transform = scene.transforms.back();
            transform.name = "Player";
            transform.position = game.walkmesh->to_world_point(other.at);
            transform.rotation = other.rotation;
            
            scene.drawables.emplace_back(&transform);
            Scene::Drawable &drawable = scene.drawables.back();
            drawable.pipeline = player_pipeline;
        }
    }
    
    for (Sheep &sheep: game.sheeps) {
        // only display sheep that aren't too close to myself, basic attempt to avoid ugliness of being inside other things
        if (glm::distance(game.walkmesh->to_world_point(player.at), game.walkmesh->to_world_point(sheep.at))
            > Game::PlayerRadius) {
            scene.transforms.emplace_back();
            Scene::Transform &transform = scene.transforms.back();
            transform.name = "Sheep";
            transform.position = game.walkmesh->to_world_point(sheep.at);
            transform.rotation = sheep.rotation;
            
            scene.drawables.emplace_back(&transform);
            Scene::Drawable &drawable = scene.drawables.back();
            drawable.pipeline = sheep_pipeline;
        }
    }
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
    // taken from game5 base code
    // update camera aspect ratio for drawable:
    player.camera->aspect = float(drawable_size.x) / float(drawable_size.y);
    
    //set up light type and position for lit_color_texture_program:
    // TODO: consider using the Light(s) in the scene to do this
    glUseProgram(lit_color_texture_program->program);
    glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
    glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f, -1.0f)));
    glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
    glUseProgram(0);
    
    glClearColor(0.1f, 0.3f, 0.9f, 1.0f);
    glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.
    
    scene.draw(*player.camera);
    
    /* In case you are wondering if your walkmesh is lining up with your scene, try:
    {
        glDisable(GL_DEPTH_TEST);
        DrawLines lines(player.camera->make_projection() * glm::mat4(player.camera->transform->make_world_to_local()));
        for (auto const &tri : walkmesh->triangles) {
            lines.draw(walkmesh->vertices[tri.x], walkmesh->vertices[tri.y], glm::u8vec4(0x88, 0x00, 0xff, 0xff));
            lines.draw(walkmesh->vertices[tri.y], walkmesh->vertices[tri.z], glm::u8vec4(0x88, 0x00, 0xff, 0xff));
            lines.draw(walkmesh->vertices[tri.z], walkmesh->vertices[tri.x], glm::u8vec4(0x88, 0x00, 0xff, 0xff));
        }
    }
    */
    
    { //use DrawLines to overlay some text:
        glDisable(GL_DEPTH_TEST);
        float aspect = float(drawable_size.x) / float(drawable_size.y);
        DrawLines lines(glm::mat4(
                1.0f / aspect, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
        ));
        
        constexpr float H = 0.09f;
        lines.draw_text("Mouse motion looks; WASD moves; escape ungrabs mouse",
                        glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
                        glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
                        glm::u8vec4(0x00, 0x00, 0x00, 0x00));
        float ofs = 2.0f / drawable_size.y;
        lines.draw_text("Mouse motion looks; WASD moves; escape ungrabs mouse",
                        glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + +0.1f * H + ofs, 0.0),
                        glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
                        glm::u8vec4(0xff, 0xff, 0xff, 0x00));
    }
    GL_ERRORS();
}
