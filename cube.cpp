// [RU] === cube.cpp — фронтальные грани, ambient+diffuse, корректная проекция по реальному шрифту ===
// [EN] === cube.cpp — front faces, ambient+diffuse, correct projection using actual console font metrics ===
#include <windows.h>                           // [RU] Доступ к геометрии консоли и прямому выводу в буфер
                                               // [EN] Access to console geometry and direct writes to the screen buffer
#include <vector>                              // [RU] Плоские буферы под символы и глубину
                                               // [EN] Flat buffers for characters and depth
#include <chrono>                              // [RU] Стендартный таймер для плавной анимации
                                               // [EN] Standard timer for smooth animation
#include <thread>                              // [RU] Сон между кадрами ~60 FPS
                                               // [EN] Sleep between frames ~60 FPS
#include <cmath>                               // [RU] Тригонометрия и корни
                                               // [EN] Trigonometry and square roots
#include <algorithm>                           // [RU] clamp/fill — аккуратная работа с массивами
                                               // [EN] clamp/fill — tidy array handling

// [RU] --- RAII: прячем курсор на время демо ---
// [EN] --- RAII: hide the cursor for the duration of the demo ---
struct ConsoleCursorGuard{                     // [RU] Гарант возвращения среды пользователю «как было»
                                               // [EN] Ensures the console returns to its original state
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);// [RU] Дескриптор StdOut — точка управления терминалом
                                               // [EN] StdOut handle — the control point for the terminal
    CONSOLE_CURSOR_INFO saved{};               // [RU] Снимок исходного состояния курсора
                                               // [EN] Snapshot of the original cursor state
    ConsoleCursorGuard(){ GetConsoleCursorInfo(h,&saved); auto cur=saved; cur.bVisible=FALSE; SetConsoleCursorInfo(h,&cur);} // [RU] Скрываем курсор
                                                                                                                             // [EN] Hide the cursor
    ~ConsoleCursorGuard(){ SetConsoleCursorInfo(h,&saved);} // [RU] Возвращаем курсор при выходе
                                                            // [EN] Restore cursor on exit
};

// [RU] --- Мини-алгебра 3D ---
// [EN] --- Tiny 3D algebra ---
struct Vec3{ float x,y,z; };                   // [RU] Компактный контейнер координат
                                               // [EN] Compact coordinate container
static inline Vec3 add(const Vec3&a,const Vec3&b){return {a.x+b.x,a.y+b.y,a.z+b.z};} // [RU] Сумма — для сдвига сцены
                                                                                     // [EN] Sum — used for translating the scene
static inline Vec3 mul(const Vec3&a,float s){return {a.x*s,a.y*s,a.z*s};}            // [RU] Масштаб — удобно для нормалей
                                                                                     // [EN] Scale — handy for normals
static inline float dot(const Vec3&a,const Vec3&b){return a.x*b.x+a.y*b.y+a.z*b.z;}  // [RU] Скалярное — основа освещения
                                                                                     // [EN] Dot product — basis for lighting
static inline Vec3 norm(const Vec3&a){float m=std::sqrt(dot(a,a)+1e-9f);return {a.x/m,a.y/m,a.z/m};} // [RU] Нормализация с защитой
                                                                                                     // [EN] Normalization with a safety guard

// [RU] --- Повороты вокруг осей + композиция ---
// [EN] --- Rotations around axes + composition ---
static inline Vec3 rotX(const Vec3&p,float s,float c){return {p.x, c*p.y - s*p.z, s*p.y + c*p.z};}   // [RU] Вращение X
                                                                                                     // [EN] Rotation around X
static inline Vec3 rotY(const Vec3&p,float s,float c){return { c*p.x + s*p.z, p.y, -s*p.x + c*p.z};} // [RU] Вращение Y
                                                                                                     // [EN] Rotation around Y
static inline Vec3 rotZ(const Vec3&p,float s,float c){return { c*p.x - s*p.y, s*p.x + c*p.y, p.z};}  // [RU] Вращение Z
                                                                                                     // [EN] Rotation around Z
static inline Vec3 rotateAll(const Vec3&p,float sx,float cx,float sy,float cy,float sz,float cz){    // [RU] Композиция Z→X→Y
                                                                                                     // [EN] Composition Z→X→Y
    return rotY(rotX(rotZ(p,sz,cz),sx,cx),sy,cy);                                                     // [RU] Порядок выбран ради живой динамики
                                                                                                      // [EN] Order chosen for lively motion
}

// [RU] --- Параметризация граней куба ---
// [EN] --- Cube face parameterization ---
struct Face{ Vec3 axis; float sign; };         // [RU] axis — какая координата фиксируется; sign — какая из двух сторон
                                               // [EN] axis — which coordinate is fixed; sign — which of the two sides
static const Face cubeFaces[6]={
    {{1,0,0},+1}, {{1,0,0},-1},                // [RU] X=±1
                                               // [EN] X=±1
    {{0,1,0},+1}, {{0,1,0},-1},                // [RU] Y=±1
                                               // [EN] Y=±1
    {{0,0,1},+1}, {{0,0,1},-1},                // [RU] Z=±1
                                               // [EN] Z=±1
};
static inline Vec3 pointOnFace(const Face&f,float u,float v){ // [RU] Генерация точки (u,v)∈[-1,1]² на выбранной грани
                                                              // [EN] Generate point (u,v)∈[-1,1]² on the chosen face
    if(f.axis.x) return { f.sign, u, v };       // [RU] На X-гранях фиксируем X
                                                // [EN] On X-faces we fix X
    if(f.axis.y) return { u, f.sign, v };       // [RU] На Y-гранях фиксируем Y
                                                // [EN] On Y-faces we fix Y
    return { u, v, f.sign };                    // [RU] На Z-гранях фиксируем Z
                                                // [EN] On Z-faces we fix Z
}

// [RU] --- Геометрия консоли и форма символа (пиксели) ---
// [EN] --- Console geometry and character shape (pixels) ---
struct ConsoleGeom{ SHORT winW,winH,bufW,winL,winT; float charAspect; }; // [RU] Полная картина видимой области
                                                                         // [EN] Full description of the visible area
static float queryCharAspect(HANDLE h){         // [RU] Добываем height/width глифа в пикселях — ключ к корректной проекции
                                                // [EN] Obtain glyph height/width in pixels — key to correct projection
    CONSOLE_FONT_INFO info{}; if(!GetCurrentConsoleFont(h,FALSE,&info)) return 2.0f; // [RU] Если API не дал — берём типичный дефолт
                                                                                     // [EN] If the API fails — use a typical default
    COORD px = GetConsoleFontSize(h, info.nFont); if(px.X<=0||px.Y<=0) return 2.0f;  // [RU] Защита от экзотики
                                                                                     // [EN] Guard against edge cases
    return (float)px.Y/(float)px.X;             // [RU] Отношение высоты к ширине (обычно >1)
                                                // [EN] Height-to-width ratio (usually >1)
}
static ConsoleGeom queryConsoleGeom(HANDLE h){  // [RU] Свежая метрика окна/буфера на каждый кадр
                                                // [EN] Fresh window/buffer metrics each frame
    CONSOLE_SCREEN_BUFFER_INFO bi{}; GetConsoleScreenBufferInfo(h,&bi); // [RU] Читаем актуальные цифры
                                                                        // [EN] Read current values
    ConsoleGeom g{}; g.winW=(SHORT)(bi.srWindow.Right-bi.srWindow.Left+1); g.winH=(SHORT)(bi.srWindow.Bottom-bi.srWindow.Top+1); // [RU] Видимая ширина/высота
                                                                                                                                 // [EN] Visible width/height
    g.bufW=bi.dwSize.X; g.winL=bi.srWindow.Left; g.winT=bi.srWindow.Top; g.charAspect=queryCharAspect(h); return g; // [RU] Остальное — как есть
                                                                                                                    // [EN] The rest — as is
}

// [RU] --- Проектор с учётом реального aspect символа ---
// [EN] --- Projector accounting for the real character aspect ratio ---
struct Projector{ int W,H; float fx,fy;        // [RU] W/H — символы; fx/fy — фокальные масштабы по осям
                                               // [EN] W/H — characters; fx/fy — focal scales along axes
    explicit Projector(const ConsoleGeom&g){ W=g.winW; H=g.winH; fx=W*0.60f; fy=fx/g.charAspect; } // [RU] Баланс ширины/высоты
                                                                                                   // [EN] Balance width/height
    bool toScreen(const Vec3&p,int&sx,int&sy)const{ if(p.z<=0.001f) return false; float invz=1.0f/p.z; // [RU] Стабильная перспектива
                                                                                                       // [EN] Stable perspective
        float x=p.x*invz, y=p.y*invz; sx=(int)(x*fx+W*0.5f); sy=(int)(-y*fy+H*0.5f);                 // [RU] Центрируем и масштабируем
                                                                                                     // [EN] Center and scale
        return (unsigned)sx<(unsigned)W && (unsigned)sy<(unsigned)H; }                                // [RU] Быстрая проверка границ
                                                                                                      // [EN] Fast bounds check
};

// [RU] --- Прямой blit в видимое окно с цветами ---
// [EN] --- Direct blit into the visible window with colors ---
static void blitFrame(HANDLE h,const ConsoleGeom&g,const std::vector<CHAR_INFO>&cbuf){ // [RU] Пишем построчно с символами и атрибутами
                                                                                       // [EN] Write line-by-line with characters and attributes
    COORD bufSize = {(SHORT)g.winW, (SHORT)g.winH};
    COORD bufCoord = {0, 0};
    SMALL_RECT writeRegion = {g.winL, g.winT, (SHORT)(g.winL + g.winW - 1), (SHORT)(g.winT + g.winH - 1)};
    WriteConsoleOutputA(h, cbuf.data(), bufSize, bufCoord, &writeRegion);
}

// [RU] --- Главная программа: фронт-face-culling + ambient + цвета + улучшенная окклюзия ---
// [EN] --- Main program: front-face culling + ambient + colors + improved occlusion ---
int main(){                                     // [RU] Начало пути — всё просто
                                                // [EN] Starting point — nice and simple
    ConsoleCursorGuard _cur;                    // [RU] Скрываем курсор на время демо
                                                // [EN] Hide cursor for the demo
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);   // [RU] Дескриптор консоли
                                                // [EN] Console handle

    const std::string ramp = " .,:;ox%#@";      // [RU] Мягкая палитра без «полосатых» символов
                                                // [EN] Soft palette without "stripy" characters
    const Vec3 lightDir = norm({-0.5f,1.0f,1.2f}); // [RU] Свет от верх-лево-вперёд — красивый рельеф
                                                   // [EN] Light from up-left-forward — pleasing relief
    const float ambient = 0.25f;                // [RU] Базовая подсветка чуть ярче для контраста
                                                // [EN] Base ambient slightly brighter for contrast
    const float camZ    = 3.2f;                 // [RU] Двигаем сцену вперёд — камера в (0,0,0), смотрит вдоль +Z
                                                // [EN] Push the scene forward — camera at (0,0,0), looking along +Z
    const float nearZ   = 0.25f;                // [RU] Ближняя плоскость — не даём проходить через камеру
                                                // [EN] Near plane — prevent passing through the camera
    const float step    = 0.032f;               // [RU] Шаг параметрической сетки по граням — плотность/скорость
                                                // [EN] Parametric grid step on faces — density/speed

    float cubeScale = 1.0f;                     // [RU] Масштаб куба (изменяется на + и -)
                                                // [EN] Cube scale (adjust with + and -)
    float rotSpeed = 1.0f;                      // [RU] Множитель скорости вращения (изменяется на [ и ])
                                                // [EN] Rotation speed multiplier (adjust with [ and ])

    // [RU] Цвета для каждой грани (атрибуты: текст + интенсивность)
    // [EN] Colors for each face (attributes: text + intensity)
    const WORD faceColors[6] = {
        FOREGROUND_RED | FOREGROUND_INTENSITY,     // [RU] X+ красный
                                                   // [EN] X+ red
        FOREGROUND_GREEN | FOREGROUND_INTENSITY,   // [RU] X- зелёный
                                                   // [EN] X- green
        FOREGROUND_BLUE | FOREGROUND_INTENSITY,    // [RU] Y+ синий
                                                   // [EN] Y+ blue
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY, // [RU] Y- жёлтый
                                                                  // [EN] Y- yellow
        FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY,  // [RU] Z+ magenta
                                                                  // [EN] Z+ magenta
        FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY // [RU] Z- cyan
                                                                  // [EN] Z- cyan
    };

    // [RU] Состояния клавиш для обнаружения "нажатия" (не удержания)
    // [EN] Key states to detect a "press" (not a hold)
    bool prevPlus = false, prevMinus = false, prevLB = false, prevRB = false, prevEsc = false;

    ConsoleGeom g = queryConsoleGeom(h);        // [RU] Снимаем геометрию и форму символа
                                                // [EN] Query geometry and character shape
    Projector proj(g);                          // [RU] Готовим проектор под текущий шрифт
                                                // [EN] Prepare projector for current font

    std::vector<float> zbuf((size_t)g.winW*(size_t)g.winH); // [RU] z-буфер (обратная глубина)
                                                            // [EN] z-buffer (inverse depth)
    std::vector<CHAR_INFO> cbuf((size_t)g.winW*(size_t)g.winH); // [RU] Буфер символов и цветов (фон чёрный по умолчанию)
                                                                // [EN] Character/color buffer (black background by default)

    auto t0=std::chrono::steady_clock::now();   // [RU] Нулевая отметка времени
                                                // [EN] Time zero

    for(;;){                                     // [RU] Основной цикл анимации
                                                 // [EN] Main animation loop
        // [RU] --- Обработка клавиш (только на нажатие, не на удержание) ---
        // [EN] --- Key handling (on press only, not on hold) ---
        bool currPlus = GetAsyncKeyState(VK_OEM_PLUS) & 0x8000;    // [RU] + (масштаб вверх)
                                                                   // [EN] + (scale up)
        bool currMinus = GetAsyncKeyState(VK_OEM_MINUS) & 0x8000;  // [RU] - (масштаб вниз)
                                                                   // [EN] - (scale down)
        bool currLB = GetAsyncKeyState(VK_OEM_4) & 0x8000;         // [RU] [ (скорость вниз)
                                                                   // [EN] [ (speed down)
        bool currRB = GetAsyncKeyState(VK_OEM_6) & 0x8000;         // [RU] ] (скорость вверх)
                                                                   // [EN] ] (speed up)
        bool currEsc = GetAsyncKeyState(VK_ESCAPE) & 0x8000;       // [RU] ESC (выход)
                                                                   // [EN] ESC (exit)

        if (currPlus && !prevPlus) cubeScale = std::max(0.1f, cubeScale + 0.1f);
        if (currMinus && !prevMinus) cubeScale = std::max(0.1f, cubeScale - 0.1f);
        if (currLB && !prevLB) rotSpeed = std::max(0.0f, rotSpeed - 0.1f);
        if (currRB && !prevRB) rotSpeed += 0.1f;
        if (currEsc && !prevEsc) break;  // [RU] Выход из цикла на ESC
                                         // [EN] Exit the loop on ESC

        prevPlus = currPlus; prevMinus = currMinus; prevLB = currLB; prevRB = currRB; prevEsc = currEsc;

        // [RU] --- Продолжение рендеринга ---
        // [EN] --- Rendering continues ---
        ConsoleGeom ng=queryConsoleGeom(h);      // [RU] Адаптация к динамическому ресайзу/смене шрифта
                                                 // [EN] Adapt to dynamic resize/font change
        if(ng.winW<40||ng.winH<20){ std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; } // [RU] Ждём адекватный размер
                                                                                                            // [EN] Wait for a reasonable size
        if(ng.winW!=g.winW||ng.winH!=g.winH||std::abs(ng.charAspect-g.charAspect)>1e-3f){ // [RU] Изменение метрики
                                                                                          // [EN] Metrics changed
            g=ng; proj=Projector(g); zbuf.assign((size_t)g.winW*(size_t)g.winH,-1e9f);
            cbuf.assign((size_t)g.winW*(size_t)g.winH, { ' ', 0 }); // [RU] Очистка с чёрным фоном
                                                                    // [EN] Clear with black background
        }else{ std::fill(zbuf.begin(),zbuf.end(),-1e9f);
            std::fill(cbuf.begin(),cbuf.end(), CHAR_INFO{ ' ', 0 }); } // [RU] Обычная очистка
                                                                       // [EN] Regular clear

        float t=std::chrono::duration<float>(std::chrono::steady_clock::now()-t0).count(); // [RU] Секунды с запуска
                                                                                           // [EN] Seconds since start
        float ax=t*0.9f*rotSpeed, ay=t*0.7f*rotSpeed+1.3f, az=t*1.1f*rotSpeed+0.7f; // [RU] Независимые фазы — приятная динамика
                                                                                    // [EN] Independent phases — pleasant dynamics
        float sx=std::sin(ax),cx=std::cos(ax), sy=std::sin(ay),cy=std::cos(ay), sz=std::sin(az),cz=std::cos(az); // [RU] Предрасчёт тригонометрии
                                                                                                                 // [EN] Precompute trigonometry

        for(int faceIndex=0; faceIndex<6; ++faceIndex){ // [RU] По всем шести граням с индексом
                                                        // [EN] Iterate over all six faces by index
            const Face& f = cubeFaces[faceIndex];
            Vec3 nCam = rotateAll(mul(norm(f.axis),f.sign), sx,cx,sy,cy,sz,cz); // [RU] Нормаль грани в координатах камеры
                                                                                // [EN] Face normal in camera coordinates
            if(nCam.z >= 0.0f) continue;         // [RU] BACK-FACE CULLING: грань от камеры — не рисуем её вовсе
                                                 // [EN] BACK-FACE CULLING: face turned away from the camera — skip drawing entirely
            float lambert = std::max(0.0f, dot(nCam, lightDir)); // [RU] Диффузная составляющая света
                                                                 // [EN] Diffuse light component
            float shadeF  = std::clamp(ambient + (1.0f-ambient)*lambert, 0.0f, 1.0f); // [RU] Ambient + diffuse
                                                                                      // [EN] Ambient + diffuse
            shadeF *= (0.8f + 0.4f * (faceIndex % 2)); // [RU] Per-face контраст: чередование яркости
                                                       // [EN] Per-face contrast: alternating brightness

            // [RU] Полуоткрытые интервалы для избежания дубликатов на ребрах
            // [EN] Half-open intervals to avoid duplicates on edges
            for(float u=-1.0f; u < 1.0f + step/2; u+=step){          
                for(float v=-1.0f; v < 1.0f + step/2; v+=step){      
                    Vec3 rawP = mul(pointOnFace(f,u,v), cubeScale); // [RU] Масштабируем точку грани
                                                                    // [EN] Scale the face point
                    Vec3 p = rotateAll(rawP, sx,cx,sy,cy,sz,cz);   // [RU] Поворачиваем
                                                                   // [EN] Rotate it
                    p = add(p,{0,0,camZ});                         // [RU] Отодвигаем сцену от камеры
                                                                   // [EN] Move the scene away from the camera
                    if(p.z<=nearZ) continue;                       // [RU] Отсекаем «слишком близко» — без артефактов у стекла
                                                                   // [EN] Clip "too close" — avoids artifacts at the near plane

                    int sxp,syp; if(!proj.toScreen(p,sxp,syp)) continue; // [RU] Проекция и отсечение по экрану
                                                                         // [EN] Projection and screen clipping

                    float invz = 1.0f/p.z + 1e-5f * (float)faceIndex; // [RU] Обратная глубина + bias для стабильности на ребрах
                                                                      // [EN] Inverse depth + bias for stability along edges
                    size_t idx=(size_t)syp*(size_t)g.winW+(size_t)sxp; // [RU] Индекс пикселя в нашем видимом окне
                                                                       // [EN] Pixel index within our visible window
                    if(invz>zbuf[idx]){                       // [RU] Z-тест — пишем только ближнее
                                                              // [EN] Z-test — write only the nearer
                        zbuf[idx]=invz;                       // [RU] Обновляем глубину
                                                              // [EN] Update depth
                        int shade=(int)std::round(shadeF*(int)(ramp.size()-1)); // [RU] Индекс символа по яркости
                                                                                // [EN] Character index by brightness
                        shade=std::clamp(shade,0,(int)ramp.size()-1); // [RU] Защита от округления
                                                                      // [EN] Guard against rounding
                        cbuf[idx].Char.AsciiChar = ramp[(size_t)shade]; // [RU] Символ
                                                                        // [EN] Character
                        cbuf[idx].Attributes = faceColors[faceIndex];   // [RU] Цвет грани
                                                                        // [EN] Face color
                    }
                }
            }
        }

        blitFrame(h,g,cbuf);                                  // [RU] Выводим кадр напрямую в консоль с цветами
                                                              // [EN] Output the frame directly to the console with colors
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // [RU] ~60 FPS
                                                                    // [EN] ~60 FPS
    }

    return 0;                                                 // [RU] Теперь достижимо на ESC
                                                              // [EN] Now reachable via ESC
}
