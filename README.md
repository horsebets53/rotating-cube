# Console 3D Cube Renderer

----

A simple yet impressive 3D rotating cube rendered in the Windows console using ASCII art. This project demonstrates basic 3D graphics concepts like perspective projection, lighting (ambient + diffuse), Z-buffering for occlusion, and face culling, all without external libraries. It runs smoothly at ~60 FPS and adapts to console resizing.

The cube features colored faces for better contrast, improved occlusion to prevent blending at edges/corners, and interactive controls for scale and rotation speed.

<img src="demo.gif" alt="Demo GIF" width="600" height="400">


# Compilation

```text
g++ -std=c++20 -O2 cube.cpp -o cube.exe
```

# Controls

    + : Increase cube scale (by 0.1).
    - : Decrease cube scale (minimum 0.1).
    [ : Decrease rotation speed (by 0.1, minimum 0.0 to stop).
    ] : Increase rotation speed (by 0.1).
    ESC : Exit the program.


# Requirements

OS: Windows (uses WinAPI for console manipulation).

Compiler: g++ with C++20 support (e.g., MinGW).

No external dependencies beyond standard C++20 and Windows headers.

