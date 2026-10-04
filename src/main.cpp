// Simulador de jubilación — reads the INSS retirement simulation in assets/ and
// lets you re-run it against a different future.
//
// SDL3 + Dear ImGui (SDL_Renderer backend), C++17.
//
// Layout of the sources:
//   pensioner.h/.cpp    the person and their history, and the config.ini /
//                       .dat files they are read from and written back to
//   pension.h/.cpp      the arithmetic: regulatory base, percentage, early and
//                       late retirement, maximum-pension cap. No UI.
//   app.h               shared UI state and the scenario being edited
//   ui.cpp              style, fonts, window shell and the scenario panel
//   tabs.cpp            the six views
//   main.cpp            SDL init and the event/render loop
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdlib>
#include <string>
#include "app.h"
#include "pensioner.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_Window* win =
        SDL_CreateWindow("Simulador de jubilación", 1500, 940, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
    if (!win || !ren) {
        SDL_Log("window/renderer failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(ren, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // fixed layout, nothing to persist
    applyStyle();
    loadFonts();
    ImGui_ImplSDL3_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer3_Init(ren);

    App app;

    // config.ini says where the pensioner's data file lives. A missing data
    // file is not an error on a first run: seed it from the tables built into
    // the binary and write it out, so there is something to edit afterwards.
    const char* iniPath = SDL_getenv("PENSION_INI");
    char* basePath = SDL_GetBasePath() ? SDL_strdup(SDL_GetBasePath()) : nullptr;
    app.config = pensioner::findConfig(iniPath ? iniPath : "", basePath ? basePath : "");
    SDL_free(basePath);
    app.dataPath = app.config.path();
    SDL_Log("config.ini: %s", app.config.source.empty() ? "no encontrado (valores por defecto)"
                                                        : app.config.source.c_str());
    SDL_Log("fichero de datos: %s", app.dataPath.c_str());

    std::string error;
    if (pensioner::load(app.dataPath, app.person, error)) {
        app.status = "Datos cargados de " + app.dataPath;
    } else {
        // No data file yet: fall back to the worked example so the program is
        // usable out of the box, and say plainly that is what happened.
        bool loaded = false;
        for (const auto& candidate : pensioner::exampleSearchPaths()) {
            std::string ignored;
            if (pensioner::load(candidate, app.person, ignored)) {
                app.status = error + " — usando el ejemplo de " + candidate +
                             ". Copielo a esa ruta y editelo con sus datos.";
                loaded = true;
                break;
            }
        }
        if (!loaded) {
            SDL_Log("No se encontro %s ni el fichero de ejemplo.", app.dataPath.c_str());
            SDL_Log("Cree config.ini junto al ejecutable o en el directorio de trabajo,");
            SDL_Log("y genere el .dat con tools/informe_a_dat.py.");
            return 1;
        }
        // Deliberately no config.ini is written here. Dropping one into
        // whatever directory the program happened to start in is how you end up
        // with a stale config shadowing the real one.
    }
    pension::setActiveDataset(app.person.data);
    app.resetToReport();

    // debug hooks for headless testing / screenshots
    const char* shotPath = SDL_getenv("PENSION_SHOT");
    if (const char* t = SDL_getenv("PENSION_TAB")) app.forceTab = std::clamp(atoi(t), 0, 6);
    if (const char* y = SDL_getenv("PENSION_RETIRE_YEAR")) app.params.retirement.year = atoi(y);
    int frame = 0;

    bool running = true;
    Uint64 lastFrame = 0;
    while (running) {
        if (!shotPath) SDL_WaitEventTimeout(nullptr, 200);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) running = false;
        }
        Uint64 now = SDL_GetTicks();
        if (!shotPath && now - lastFrame < 16) SDL_Delay((Uint32)(16 - (now - lastFrame)));
        lastFrame = SDL_GetTicks();

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        drawUI(app);
        ImGui::Render();

        SDL_SetRenderDrawColor(ren, 22, 24, 32, 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);

        if (shotPath && ++frame == 5) {
            SDL_Surface* s = SDL_RenderReadPixels(ren, nullptr);
            if (s) {
                SDL_SaveBMP(s, shotPath);
                SDL_DestroySurface(s);
            }
            running = false;
        }
        SDL_RenderPresent(ren);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
