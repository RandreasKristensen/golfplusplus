vcr-golf Windows portable build

Run vcr-golf.exe from this folder.

Keep the assets folder and DLL files next to vcr-golf.exe. The game loads shaders,
holes, courses, clubs, and quests from the adjacent assets folder first.

Requirements:
- Windows 10 or newer
- OpenGL 3.3 capable GPU/driver

If the game does not start, update the graphics driver first. This build includes
SDL2 and MinGW runtime DLLs, so MSYS2 should not be required on the target machine.
