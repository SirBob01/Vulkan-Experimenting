#include <SDL2/SDL.h>
#include "renderer/renderer.h"

// Entry point
// Simulates Dynamo's update pipeline
int main(int argc, char **argv) {
    SDL_Init(SDL_INIT_EVERYTHING);

    // From Dynamo::Display module
    bool fullscreen = false;

    SDL_DisplayMode native_res;
    SDL_GetDesktopDisplayMode(0, &native_res);

    auto flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
    int width = fullscreen ? native_res.w : 640;
    int height = fullscreen ? native_res.h : 480;

    SDL_Window *window = SDL_CreateWindow(
        "Experimental Renderer",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        flags
    );

    // Instantiate Renderer class
    Renderer renderer(window);

    // Load textures
    Texture t1 = renderer.load_texture("../assets/texture.jpg");
    Texture t2 = renderer.load_texture("../assets/hazard.png");
    Texture viking_room_texture = renderer.load_texture("../assets/viking_room.png");

    // Load models
    Model squares;
    squares.vertices = {
        {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},

        {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}}
    };
    squares.indices = {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4
    };
    Model viking_room("../assets/viking_room.obj");

    SDL_Event e;
    bool running = true;
    while(running) {
        renderer.refresh();
        while(SDL_PollEvent(&e) != 0) {
            if(e.type == SDL_QUIT) {
                running = false;
            }
            if(e.type == SDL_MOUSEBUTTONDOWN) {
                if(e.button.button == SDL_BUTTON_LEFT) {
                    renderer.set_fill(255, 255, 255, 255);
                }
                else {
                    renderer.set_fill(0, 0, 0, 0);
                }
            }
            if(e.type == SDL_KEYDOWN) {
                if(e.key.keysym.sym == SDLK_F9) {
                    if(!fullscreen)  {
                        SDL_SetWindowFullscreen(
                            window, SDL_WINDOW_FULLSCREEN
                        );
                        fullscreen = true;                        
                    }
                    else {
                        SDL_SetWindowFullscreen(
                            window, 0
                        );
                        fullscreen = false;
                    }
                }
                else if(e.key.keysym.sym == SDLK_t) {
                    renderer.add_mesh(squares, 0);
                }
                else if(e.key.keysym.sym == SDLK_y) {
                    renderer.add_mesh(squares, t1);
                }
                else if(e.key.keysym.sym == SDLK_u) {
                    renderer.add_mesh(squares, t2);
                }
                else if(e.key.keysym.sym == SDLK_i) {
                    renderer.add_mesh(viking_room, viking_room_texture);
                }
                else if(e.key.keysym.sym == SDLK_r) {
                    renderer.remove_mesh();
                }
            }
        }
    }
    SDL_Quit();
    return 0;
}
