#include <SDL2/SDL.h>
#include "renderer.h"

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
                    renderer.add_triangle();
                }
                else if(e.key.keysym.sym == SDLK_r) {
                    renderer.remove_triangle();
                }
            }
        }
    }
    return 0;
}
