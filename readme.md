# Inventory Manager
An inventory manager with ui that is based on a local postgresql database. 

## Current Stage
Finetuning of rudimentary change tracking / change visualization.\
NEXT: Actually executing the changes in the DB.

## Dependencies

libpqxx (depends on libpq)\
imgui\
The imgui dx11 implementation is used, which requires at least:\
windows\
dx11
```c++
class App {
   private:
    ImGuiDX11Context imguiCtx;

```
The only interface to the dx11-backed is through these three functions:
```c++
while (running) {
    if (!imguiCtx.pollEvents()) { // POLL EVENTS
        appState = AppState::ENDING;
        break;
    }

    if (!imguiCtx.beginFrame()) { continue; } // BEGIN FRAME

    running = handleAppState();
    drawFpsOverlay();
    imguiCtx.endFrame(); // END FRAME
}
```
