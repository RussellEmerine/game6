# This cmake file does not actually build anything. It is only for CLion to understand imports.

cmake_minimum_required(VERSION 3.26)
project(game6)

set(CMAKE_CXX_STANDARD 17)

include_directories(
        .
        ../nest-libs/macos/SDL2/include/SDL2
        ../nest-libs/macos/freetype/include
        ../nest-libs/macos/glm/include
        ../nest-libs/macos/harfbuzz/include
        ../nest-libs/macos/libogg/include
        ../nest-libs/macos/libopus/include
        ../nest-libs/macos/libopusenc/include
        ../nest-libs/macos/libpng/include
        ../nest-libs/macos/opusfile/include
        ../nest-libs/macos/zlib/include
)

add_executable(game6
        client.cpp
        ColorProgram.cpp
        ColorProgram.hpp
        ColorTextureProgram.cpp
        ColorTextureProgram.hpp
        Connection.cpp
        Connection.hpp
        data_path.cpp
        data_path.hpp
        DrawLines.cpp
        DrawLines.hpp
        Game.cpp
        Game.hpp
        GL.cpp
        GL.hpp
        gl_compile_program.cpp
        gl_compile_program.hpp
        gl_errors.hpp
        glcorearb.h
        hex_dump.cpp
        hex_dump.hpp
        LitColorTextureProgram.cpp
        LitColorTextureProgram.hpp
        Load.cpp
        Load.hpp
        load_opus.cpp
        load_opus.hpp
        load_save_png.cpp
        load_save_png.hpp
        load_wav.cpp
        load_wav.hpp
        Mesh.cpp
        Mesh.hpp
        Mode.cpp
        Mode.hpp
        PathFont-font.cpp
        PathFont.cpp
        PathFont.hpp
        PlayMode.cpp
        PlayMode.hpp
        read_write_chunk.hpp
        Scene.cpp
        Scene.hpp
        server.cpp
        show-meshes.cpp
        show-scene.cpp
        ShowMeshesMode.cpp
        ShowMeshesMode.hpp
        ShowMeshesProgram.cpp
        ShowMeshesProgram.hpp
        ShowSceneMode.cpp
        ShowSceneMode.hpp
        ShowSceneProgram.cpp
        ShowSceneProgram.hpp
        Sound.cpp
        Sound.hpp)
