# nya_qt
Integration of QtQuick/QML UI with Nya engine

The purpose of this code is to provide a more-or-less complete demonstration of integration between QtQuick component of Qt library and a 3D game engine (in this case, Nya engine).

## Compilation:

A project for Microsoft Visual Studio 2015 is provided.

It is expected that nya-engine will be present in ext/ folder (as a sub-module - make sure you clone the repository recursively!)

It is also expected that Qt headers, libraries and QML modules of version >= 5.6 will be present in ext\qt5_msvc2015 folder.


## Some basic pinciples:

1. Main loop is handled by Qt. "Game" logic is processed in on_frame function, which is called from beforeRendering signal of QQuickView
2. Textures for UI are provided by Nya resource system
3. QML imports are NOT provided by Nya resource system, and should be available either from disk, or from additional Qt-style (rcc+qrc) resources
4. This example does not require MOC code generator, and demonstrates how to make QML and C++ parts work without access to it

