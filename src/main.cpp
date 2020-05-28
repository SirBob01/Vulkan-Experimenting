#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include "renderer.cpp"

// Entry point
// Simulates Dynamo's update pipeline
int main(int argc, char **argv) {
    SDL_Init(SDL_INIT_EVERYTHING);

    // From Dynamo::Display module
    bool fullscreen = false;

    SDL_DisplayMode native_res;
    SDL_GetDesktopDisplayMode(0, &native_res);

    auto flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
    if(fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
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
        }
    }
    return 0;
}
