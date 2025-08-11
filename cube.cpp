// === cube.cpp — front faces, ambient+diffuse, correct projection using actual console font metrics ===
#include <windows.h>                           // Access to console geometry and direct writes to the screen buffer
#include <vector>                              // Flat buffers for characters and depth
#include <chrono>                              // Standard timer for smooth animation
#include <thread>                              // Sleep between frames ~60 FPS
#include <cmath>                               // Trigonometry and square roots
#include <algorithm>                           // clamp/fill — tidy array handling

// --- RAII: hide the cursor for the duration of the demo ---
struct ConsoleCursorGuard{                     // Ensures the console returns to its original state
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);// StdOut handle — the control point for the terminal
    CONSOLE_CURSOR_INFO saved{};               // Snapshot of the original cursor state
    ConsoleCursorGuard(){ GetConsoleCursorInfo(h,&saved); auto cur=saved; cur.bVisible=FALSE; SetConsoleCursorInfo(h,&cur);} // Hide the cursor
    ~ConsoleCursorGuard(){ SetConsoleCursorInfo(h,&saved);} // Restore cursor on exit
};

// --- Tiny 3D algebra ---
struct Vec3{ float x,y,z; };                   // Compact coordinate container
static inline Vec3 add(const Vec3&a,const Vec3&b){return {a.x+b.x,a.y+b.y,a.z+b.z};} // Sum — used for translating the scene
static inline Vec3 mul(const Vec3&a,float s){return {a.x*s,a.y*s,a.z*s};}            // Scale — handy for normals
static inline float dot(const Vec3&a,const Vec3&b){return a.x*b.x+a.y*b.y+a.z*b.z;}  // Dot product — basis for lighting
static inline Vec3 norm(const Vec3&a){float m=std::sqrt(dot(a,a)+1e-9f);return {a.x/m,a.y/m,a.z/m};} // Normalization with safety guard

// --- Rotations around axes + composition ---
static inline Vec3 rotX(const Vec3&p,float s,float c){return {p.x, c*p.y - s*p.z, s*p.y + c*p.z};}   // Rotation around X
static inline Vec3 rotY(const Vec3&p,float s,float c){return { c*p.x + s*p.z, p.y, -s*p.x + c*p.z};} // Rotation around Y
static inline Vec3 rotZ(const Vec3&p,float s,float c){return { c*p.x - s*p.y, s*p.x + c*p.y, p.z};}  // Rotation around Z
static inline Vec3 rotateAll(const Vec3&p,float sx,float cx,float sy,float cy,float sz,float cz){    // Composition Z→X→Y
    return rotY(rotX(rotZ(p,sz,cz),sx,cx),sy,cy);                                                     // Order chosen for lively motion
}

// --- Cube face parameterization ---
struct Face{ Vec3 axis; float sign; };         // axis — which coordinate is fixed; sign — which of the two sides
static const Face cubeFaces[6]={
    {{1,0,0},+1}, {{1,0,0},-1},                // X=±1
    {{0,1,0},+1}, {{0,1,0},-1},                // Y=±1
    {{0,0,1},+1}, {{0,0,1},-1},                // Z=±1
};
static inline Vec3 pointOnFace(const Face&f,float u,float v){ // Generate point (u,v)∈[-1,1]² on chosen face
    if(f.axis.x) return { f.sign, u, v };       // On X-faces we fix X
    if(f.axis.y) return { u, f.sign, v };       // On Y-faces we fix Y
    return { u, v, f.sign };                    // On Z-faces we fix Z
}

// --- Console geometry and character shape (pixels) ---
struct ConsoleGeom{ SHORT winW,winH,bufW,winL,winT; float charAspect; }; // Full description of the visible area
static float queryCharAspect(HANDLE h){         // Obtain glyph height/width in pixels — key to correct projection
    CONSOLE_FONT_INFO info{}; if(!GetCurrentConsoleFont(h,FALSE,&info)) return 2.0f; // If the API fails — use a typical default
    COORD px = GetConsoleFontSize(h, info.nFont); if(px.X<=0||px.Y<=0) return 2.0f;  // Guard against odd cases
    return (float)px.Y/(float)px.X;             // Height-to-width ratio (usually >1)
}
static ConsoleGeom queryConsoleGeom(HANDLE h){  // Fresh window/buffer metrics each frame
    CONSOLE_SCREEN_BUFFER_INFO bi{}; GetConsoleScreenBufferInfo(h,&bi); // Read current values
    ConsoleGeom g{}; g.winW=(SHORT)(bi.srWindow.Right-bi.srWindow.Left+1); g.winH=(SHORT)(bi.srWindow.Bottom-bi.srWindow.Top+1); // Visible width/height
    g.bufW=bi.dwSize.X; g.winL=bi.srWindow.Left; g.winT=bi.srWindow.Top; g.charAspect=queryCharAspect(h); return g; // The rest — as is
}

// --- Projector accounting for real character aspect ratio ---
struct Projector{ int W,H; float fx,fy;        // W/H — characters; fx/fy — focal scales along axes
    explicit Projector(const ConsoleGeom&g){ W=g.winW; H=g.winH; fx=W*0.60f; fy=fx/g.charAspect; } // Balance width/height
    bool toScreen(const Vec3&p,int&sx,int&sy)const{ if(p.z<=0.001f) return false; float invz=1.0f/p.z; // Stable perspective
        float x=p.x*invz, y=p.y*invz; sx=(int)(x*fx+W*0.5f); sy=(int)(-y*fy+H*0.5f);                 // Center and scale
        return (unsigned)sx<(unsigned)W && (unsigned)sy<(unsigned)H; }                                // Fast bounds check
};

// --- Direct blit into the visible window with colors ---
static void blitFrame(HANDLE h,const ConsoleGeom&g,const std::vector<CHAR_INFO>&cbuf){ // Write line-by-line with characters and attributes
    COORD bufSize = {(SHORT)g.winW, (SHORT)g.winH};
    COORD bufCoord = {0, 0};
    SMALL_RECT writeRegion = {g.winL, g.winT, (SHORT)(g.winL + g.winW - 1), (SHORT)(g.winT + g.winH - 1)};
    WriteConsoleOutputA(h, cbuf.data(), bufSize, bufCoord, &writeRegion);
}

// --- Main program: front-face culling + ambient + colors + improved occlusion ---
int main(){                                     // Starting point — nice and simple
    ConsoleCursorGuard _cur;                    // Hide cursor for the demo
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);   // Console handle

    const std::string ramp = " .,:;ox%#@";      // Soft palette without "stripy" characters
    const Vec3 lightDir = norm({-0.5f,1.0f,1.2f}); // Light from up-left-forward — pleasing relief
    const float ambient = 0.25f;                // Base ambient slightly brighter for contrast
    const float camZ    = 3.2f;                 // Push the scene forward — camera at (0,0,0), looking along +Z
    const float nearZ   = 0.25f;                // Near plane — prevent passing through the camera
    const float step    = 0.032f;               // Parametric grid step on faces — density/speed

    float cubeScale = 1.0f;                     // Cube scale (adjust with + and -)
    float rotSpeed = 1.0f;                      // Rotation speed multiplier (adjust with [ and ])

    // Colors per face (attributes: text + intensity)
    const WORD faceColors[6] = {
        FOREGROUND_RED | FOREGROUND_INTENSITY,     // X+ red
        FOREGROUND_GREEN | FOREGROUND_INTENSITY,   // X- green
        FOREGROUND_BLUE | FOREGROUND_INTENSITY,    // Y+ blue
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Y- yellow
        FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY,  // Z+ magenta
        FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY // Z- cyan
    };

    // Key states to detect "press" (not hold)
    bool prevPlus = false, prevMinus = false, prevLB = false, prevRB = false, prevEsc = false;

    ConsoleGeom g = queryConsoleGeom(h);        // Query geometry and character shape
    Projector proj(g);                          // Prepare projector for current font

    std::vector<float> zbuf((size_t)g.winW*(size_t)g.winH); // z-buffer (inverse depth)
    std::vector<CHAR_INFO> cbuf((size_t)g.winW*(size_t)g.winH); // Character/color buffer (black background by default)

    auto t0=std::chrono::steady_clock::now();   // Time zero

    for(;;){                                     // Main animation loop
        // --- Key handling (on press only, not on hold) ---
        bool currPlus = GetAsyncKeyState(VK_OEM_PLUS) & 0x8000;    // + (scale up)
        bool currMinus = GetAsyncKeyState(VK_OEM_MINUS) & 0x8000;  // - (scale down)
        bool currLB = GetAsyncKeyState(VK_OEM_4) & 0x8000;         // [ (speed down)
        bool currRB = GetAsyncKeyState(VK_OEM_6) & 0x8000;         // ] (speed up)
        bool currEsc = GetAsyncKeyState(VK_ESCAPE) & 0x8000;       // ESC (exit)

        if (currPlus && !prevPlus) cubeScale = std::max(0.1f, cubeScale + 0.1f);
        if (currMinus && !prevMinus) cubeScale = std::max(0.1f, cubeScale - 0.1f);
        if (currLB && !prevLB) rotSpeed = std::max(0.0f, rotSpeed - 0.1f);
        if (currRB && !prevRB) rotSpeed += 0.1f;
        if (currEsc && !prevEsc) break;  // Exit the loop on ESC

        prevPlus = currPlus; prevMinus = currMinus; prevLB = currLB; prevRB = currRB; prevEsc = currEsc;

        // --- Rendering continues ---
        ConsoleGeom ng=queryConsoleGeom(h);      // Adapt to dynamic resize/font change
        if(ng.winW<40||ng.winH<20){ std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; } // Wait for a reasonable size
        if(ng.winW!=g.winW||ng.winH!=g.winH||std::abs(ng.charAspect-g.charAspect)>1e-3f){ // Metrics changed
            g=ng; proj=Projector(g); zbuf.assign((size_t)g.winW*(size_t)g.winH,-1e9f);
            cbuf.assign((size_t)g.winW*(size_t)g.winH, { ' ', 0 }); // Clear with black background
        }else{ std::fill(zbuf.begin(),zbuf.end(),-1e9f);
            std::fill(cbuf.begin(),cbuf.end(), CHAR_INFO{ ' ', 0 }); } // Regular clear

        float t=std::chrono::duration<float>(std::chrono::steady_clock::now()-t0).count(); // Seconds since start
        float ax=t*0.9f*rotSpeed, ay=t*0.7f*rotSpeed+1.3f, az=t*1.1f*rotSpeed+0.7f; // Independent phases — pleasant dynamics
        float sx=std::sin(ax),cx=std::cos(ax), sy=std::sin(ay),cy=std::cos(ay), sz=std::sin(az),cz=std::cos(az); // Precompute trigonometry

        for(int faceIndex=0; faceIndex<6; ++faceIndex){ // Iterate over all six faces by index
            const Face& f = cubeFaces[faceIndex];
            Vec3 nCam = rotateAll(mul(norm(f.axis),f.sign), sx,cx,sy,cy,sz,cz); // Face normal in camera coordinates
            if(nCam.z >= 0.0f) continue;         // BACK-FACE CULLING: face turned away from the camera — skip drawing entirely
            float lambert = std::max(0.0f, dot(nCam, lightDir)); // Diffuse light component
            float shadeF  = std::clamp(ambient + (1.0f-ambient)*lambert, 0.0f, 1.0f); // Ambient + diffuse
            shadeF *= (0.8f + 0.4f * (faceIndex % 2)); // Per-face contrast: alternating brightness

            // Half-open intervals to avoid duplicates on edges
            for(float u=-1.0f; u < 1.0f + step/2; u+=step){          
                for(float v=-1.0f; v < 1.0f + step/2; v+=step){      
                    Vec3 rawP = mul(pointOnFace(f,u,v), cubeScale); // Scale the face point
                    Vec3 p = rotateAll(rawP, sx,cx,sy,cy,sz,cz);   // Rotate it
                    p = add(p,{0,0,camZ});                         // Move the scene away from the camera
                    if(p.z<=nearZ) continue;                       // Clip "too close" — avoids artifacts at the near plane

                    int sxp,syp; if(!proj.toScreen(p,sxp,syp)) continue; // Projection and screen clipping

                    float invz = 1.0f/p.z + 1e-5f * (float)faceIndex; // Inverse depth + bias for stability along edges
                    size_t idx=(size_t)syp*(size_t)g.winW+(size_t)sxp; // Pixel index within our visible window
                    if(invz>zbuf[idx]){                       // Z-test — write only the nearer
                        zbuf[idx]=invz;                       // Update depth
                        int shade=(int)std::round(shadeF*(int)(ramp.size()-1)); // Character index by brightness
                        shade=std::clamp(shade,0,(int)ramp.size()-1); // Guard against rounding
                        cbuf[idx].Char.AsciiChar = ramp[(size_t)shade]; // Character
                        cbuf[idx].Attributes = faceColors[faceIndex];   // Face color
                    }
                }
            }
        }

        blitFrame(h,g,cbuf);                                  // Output the frame directly to the console with colors
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }

    return 0;                                                 // Now reachable via ESC
}
