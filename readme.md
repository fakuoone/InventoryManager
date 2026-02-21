# Inventory Manager
An inventory manager with UI that is based on a local postgresql database. 

## Current Stage
CSV-files can now be used to generate changes which then can be executed and sent to the local database.
Next step is to clean up the display of changes in the UI, general usability improvementes and rigorous testing.

## Dependencies
- libcurl
- libpqxx
- imgui
- C++23
The imgui dx11 implementation is used, which requires at least:\
- windows
- dx11

```c++
class App {
   private:
    ImGuiDX11Context imguiCtx;

```
The only interface to the dx11-backed is through these three functions:
```c++
while (running) {
    if (!imguiCtx.pollEvents()) { // POLL EVENTS
        /*
        ...
        */
    }

    if (!imguiCtx.beginFrame()) { continue; } // BEGIN FRAME
    /*
    ...
    */
    imguiCtx.endFrame(); // END FRAME
}
```

## Visual Examples
Mapping view: 
![Mapping](doc/mapping.png)
Database view:
![Database](doc/database.png)

