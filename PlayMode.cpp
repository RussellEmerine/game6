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

// TODO: merge this with the world_scene in Game.hpp
Load<Scene> world_scene(LoadTagDefault, []() -> Scene const * {
    return new Scene(
            data_path("world.scene"),
            [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name) {
                Mesh const &mesh = world_meshes->lookup(mesh_name);
                
                scene.drawables.emplace_back(transform);
                Scene::Drawable &drawable = scene.drawables.back();
                
                drawable.pipeline = lit_color_texture_program_pipeline;
                
                drawable.pipeline.vao = world_meshes_for_lit_color_texture_program;
                drawable.pipeline.type = mesh.type;
                drawable.pipeline.start = mesh.start;
                drawable.pipeline.count = mesh.count;
                
            });
});

PlayMode::PlayMode(Client &client_) : client(client_), scene(*world_scene) {
    // create a player transform:
    // TODO: change to multiple players
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
        } else if (evt.key.keysym.sym == SDLK_SPACE) {
            controls.jump.downs += 1;
            controls.jump.pressed = true;
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
        } else if (evt.key.keysym.sym == SDLK_SPACE) {
            controls.jump.pressed = false;
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
    controls.jump.downs = 0;
    
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
    
    // TODO: loop through game players and filter on name
    player.at = game.players.back().at;
    player.transform->position = game.walkmesh->to_world_point(player.at);
    player.transform->rotation = game.players.back().rotation;
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
    
    glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
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
