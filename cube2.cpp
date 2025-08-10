// === cube.cpp — фронтальные грани, ambient+diffuse, корректная проекция по реальному шрифту ===
#include <windows.h>                           // Доступ к геометрии консоли и прямому выводу в буфер
#include <vector>                              // Плоские буферы под символы и глубину
#include <chrono>                              // Стендартный таймер для плавной анимации
#include <thread>                              // Сон между кадрами ~60 FPS
#include <cmath>                               // Тригонометрия и корни
#include <algorithm>                           // clamp/fill — аккуратная работа с массивами

// --- RAII: прячем курсор на время демо ---
struct ConsoleCursorGuard{                     // Гарант возвращения среды пользователю «как было»
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);// Дескриптор StdOut — точка управления терминалом
    CONSOLE_CURSOR_INFO saved{};               // Снимок исходного состояния курсора
    ConsoleCursorGuard(){ GetConsoleCursorInfo(h,&saved); auto cur=saved; cur.bVisible=FALSE; SetConsoleCursorInfo(h,&cur);} // Скрываем курсор
    ~ConsoleCursorGuard(){ SetConsoleCursorInfo(h,&saved);} // Возвращаем курсор при выходе
};

// --- Мини-алгебра 3D ---
struct Vec3{ float x,y,z; };                   // Компактный контейнер координат
static inline Vec3 add(const Vec3&a,const Vec3&b){return {a.x+b.x,a.y+b.y,a.z+b.z};} // Сумма — для сдвига сцены
static inline Vec3 mul(const Vec3&a,float s){return {a.x*s,a.y*s,a.z*s};}            // Масштаб — удобно для нормалей
static inline float dot(const Vec3&a,const Vec3&b){return a.x*b.x+a.y*b.y+a.z*b.z;}  // Скалярное — основа освещения
static inline Vec3 norm(const Vec3&a){float m=std::sqrt(dot(a,a)+1e-9f);return {a.x/m,a.y/m,a.z/m};} // Нормализация с защитой

// --- Повороты вокруг осей + композиция ---
static inline Vec3 rotX(const Vec3&p,float s,float c){return {p.x, c*p.y - s*p.z, s*p.y + c*p.z};}   // Вращение X
static inline Vec3 rotY(const Vec3&p,float s,float c){return { c*p.x + s*p.z, p.y, -s*p.x + c*p.z};} // Вращение Y
static inline Vec3 rotZ(const Vec3&p,float s,float c){return { c*p.x - s*p.y, s*p.x + c*p.y, p.z};}  // Вращение Z
static inline Vec3 rotateAll(const Vec3&p,float sx,float cx,float sy,float cy,float sz,float cz){    // Композиция Z→X→Y
    return rotY(rotX(rotZ(p,sz,cz),sx,cx),sy,cy);                                                     // Порядок выбран ради живой динамики
}

// --- Параметризация граней куба ---
struct Face{ Vec3 axis; float sign; };         // axis — какая координата фиксируется; sign — какая из двух сторон
static const Face cubeFaces[6]={
    {{1,0,0},+1}, {{1,0,0},-1},                // X=±1
    {{0,1,0},+1}, {{0,1,0},-1},                // Y=±1
    {{0,0,1},+1}, {{0,0,1},-1},                // Z=±1
};
static inline Vec3 pointOnFace(const Face&f,float u,float v){ // Генерация точки (u,v)∈[-1,1]² на выбранной грани
    if(f.axis.x) return { f.sign, u, v };       // На X-гранях фиксируем X
    if(f.axis.y) return { u, f.sign, v };       // На Y-гранях фиксируем Y
    return { u, v, f.sign };                    // На Z-гранях фиксируем Z
}

// --- Геометрия консоли и форма символа (пиксели) ---
struct ConsoleGeom{ SHORT winW,winH,bufW,winL,winT; float charAspect; }; // Полная картина видимой области
static float queryCharAspect(HANDLE h){         // Добываем height/width глифа в пикселях — ключ к корректной проекции
    CONSOLE_FONT_INFO info{}; if(!GetCurrentConsoleFont(h,FALSE,&info)) return 2.0f; // Если API не дал — берём типичный дефолт
    COORD px = GetConsoleFontSize(h, info.nFont); if(px.X<=0||px.Y<=0) return 2.0f;  // Защита от экзотики
    return (float)px.Y/(float)px.X;             // Отношение высоты к ширине (обычно >1)
}
static ConsoleGeom queryConsoleGeom(HANDLE h){  // Свежая метрика окна/буфера на каждый кадр
    CONSOLE_SCREEN_BUFFER_INFO bi{}; GetConsoleScreenBufferInfo(h,&bi); // Читаем актуальные цифры
    ConsoleGeom g{}; g.winW=(SHORT)(bi.srWindow.Right-bi.srWindow.Left+1); g.winH=(SHORT)(bi.srWindow.Bottom-bi.srWindow.Top+1); // Видимая ширина/высота
    g.bufW=bi.dwSize.X; g.winL=bi.srWindow.Left; g.winT=bi.srWindow.Top; g.charAspect=queryCharAspect(h); return g; // Остальное — как есть
}

// --- Проектор с учётом реального aspect символа ---
struct Projector{ int W,H; float fx,fy;        // W/H — символы; fx/fy — фокальные масштабы по осям
    explicit Projector(const ConsoleGeom&g){ W=g.winW; H=g.winH; fx=W*0.60f; fy=fx/g.charAspect; } // Баланс ширины/высоты
    bool toScreen(const Vec3&p,int&sx,int&sy)const{ if(p.z<=0.001f) return false; float invz=1.0f/p.z; // Стабильная перспектива
        float x=p.x*invz, y=p.y*invz; sx=(int)(x*fx+W*0.5f); sy=(int)(-y*fy+H*0.5f);                 // Центрируем и масштабируем
        return (unsigned)sx<(unsigned)W && (unsigned)sy<(unsigned)H; }                                // Быстрая проверка границ
};

// --- Прямой blit в видимое окно с цветами ---
static void blitFrame(HANDLE h,const ConsoleGeom&g,const std::vector<CHAR_INFO>&cbuf){ // Пишем построчно с символами и атрибутами
    COORD bufSize = {(SHORT)g.winW, (SHORT)g.winH};
    COORD bufCoord = {0, 0};
    SMALL_RECT writeRegion = {g.winL, g.winT, (SHORT)(g.winL + g.winW - 1), (SHORT)(g.winT + g.winH - 1)};
    WriteConsoleOutputA(h, cbuf.data(), bufSize, bufCoord, &writeRegion);
}

// --- Главная программа: фронт-face-culling + ambient + цвета + улучшенная окклюзия ---
int main(){                                     // Начало пути — всё просто
    ConsoleCursorGuard _cur;                    // Скрываем курсор на время демо
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);   // Дескриптор консоли

    const std::string ramp = " .,:;ox%#@";      // Мягкая палитра без «полосатых» символов
    const Vec3 lightDir = norm({-0.5f,1.0f,1.2f}); // Свет от верх-лево-вперёд — красивый рельеф
    const float ambient = 0.25f;                // Базовая подсветка чуть ярче для контраста
    const float camZ    = 3.2f;                 // Двигаем сцену вперёд — камера в (0,0,0), смотрит вдоль +Z
    const float nearZ   = 0.25f;                // Ближняя плоскость — не даём проходить через камеру
    const float step    = 0.032f;               // Шаг параметрической сетки по граням — плотность/скорость

    float cubeScale = 1.0f;                     // Масштаб куба (изменяется на + и -)
    float rotSpeed = 1.0f;                      // Множитель скорости вращения (изменяется на [ и ])

    // Цвета для каждой грани (атрибуты: текст + интенсивность)
    const WORD faceColors[6] = {
        FOREGROUND_RED | FOREGROUND_INTENSITY,     // X+ красный
        FOREGROUND_GREEN | FOREGROUND_INTENSITY,   // X- зелёный
        FOREGROUND_BLUE | FOREGROUND_INTENSITY,    // Y+ синий
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Y- жёлтый
        FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY,  // Z+ magenta
        FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY // Z- cyan
    };

    // Состояния клавиш для обнаружения "нажатия" (не удержания)
    bool prevPlus = false, prevMinus = false, prevLB = false, prevRB = false, prevEsc = false;

    ConsoleGeom g = queryConsoleGeom(h);        // Снимаем геометрию и форму символа
    Projector proj(g);                          // Готовим проектор под текущий шрифт

    std::vector<float> zbuf((size_t)g.winW*(size_t)g.winH); // z-буфер (обратная глубина)
    std::vector<CHAR_INFO> cbuf((size_t)g.winW*(size_t)g.winH); // Буфер символов и цветов (фон чёрный по умолчанию)

    auto t0=std::chrono::steady_clock::now();   // Нулевая отметка времени

    for(;;){                                     // Основной цикл анимации
        // --- Обработка клавиш (только на нажатие, не на удержание) ---
        bool currPlus = GetAsyncKeyState(VK_OEM_PLUS) & 0x8000;    // + (масштаб вверх)
        bool currMinus = GetAsyncKeyState(VK_OEM_MINUS) & 0x8000;  // - (масштаб вниз)
        bool currLB = GetAsyncKeyState(VK_OEM_4) & 0x8000;         // [ (скорость вниз)
        bool currRB = GetAsyncKeyState(VK_OEM_6) & 0x8000;         // ] (скорость вверх)
        bool currEsc = GetAsyncKeyState(VK_ESCAPE) & 0x8000;       // ESC (выход)

        if (currPlus && !prevPlus) cubeScale = std::max(0.1f, cubeScale + 0.1f);
        if (currMinus && !prevMinus) cubeScale = std::max(0.1f, cubeScale - 0.1f);
        if (currLB && !prevLB) rotSpeed = std::max(0.0f, rotSpeed - 0.1f);
        if (currRB && !prevRB) rotSpeed += 0.1f;
        if (currEsc && !prevEsc) break;  // Выход из цикла на ESC

        prevPlus = currPlus; prevMinus = currMinus; prevLB = currLB; prevRB = currRB; prevEsc = currEsc;

        // --- Продолжение рендеринга ---
        ConsoleGeom ng=queryConsoleGeom(h);      // Адаптация к динамическому ресайзу/смене шрифта
        if(ng.winW<40||ng.winH<20){ std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; } // Ждём адекватный размер
        if(ng.winW!=g.winW||ng.winH!=g.winH||std::abs(ng.charAspect-g.charAspect)>1e-3f){ // Изменение метрики
            g=ng; proj=Projector(g); zbuf.assign((size_t)g.winW*(size_t)g.winH,-1e9f);
            cbuf.assign((size_t)g.winW*(size_t)g.winH, { ' ', 0 }); // Очистка с чёрным фоном
        }else{ std::fill(zbuf.begin(),zbuf.end(),-1e9f);
            std::fill(cbuf.begin(),cbuf.end(), CHAR_INFO{ ' ', 0 }); } // Обычная очистка

        float t=std::chrono::duration<float>(std::chrono::steady_clock::now()-t0).count(); // Секунды с запуска
        float ax=t*0.9f*rotSpeed, ay=t*0.7f*rotSpeed+1.3f, az=t*1.1f*rotSpeed+0.7f; // Независимые фазы — приятная динамика
        float sx=std::sin(ax),cx=std::cos(ax), sy=std::sin(ay),cy=std::cos(ay), sz=std::sin(az),cz=std::cos(az); // Предрасчёт тригонометрии

        for(int faceIndex=0; faceIndex<6; ++faceIndex){ // По всем шести граням с индексом
            const Face& f = cubeFaces[faceIndex];
            Vec3 nCam = rotateAll(mul(norm(f.axis),f.sign), sx,cx,sy,cy,sz,cz); // Нормаль грани в координатах камеры
            if(nCam.z >= 0.0f) continue;         // BACK-FACE CULLING: грань от камеры — не рисуем её вовсе
            float lambert = std::max(0.0f, dot(nCam, lightDir)); // Диффузная составляющая света
            float shadeF  = std::clamp(ambient + (1.0f-ambient)*lambert, 0.0f, 1.0f); // Ambient + diffuse
            shadeF *= (0.8f + 0.4f * (faceIndex % 2)); // Per-face контраст: чередование яркости

            // Полуоткрытые интервалы для избежания дубликатов на ребрах
            for(float u=-1.0f; u < 1.0f + step/2; u+=step){          
                for(float v=-1.0f; v < 1.0f + step/2; v+=step){      
                    Vec3 rawP = mul(pointOnFace(f,u,v), cubeScale); // Масштабируем точку грани
                    Vec3 p = rotateAll(rawP, sx,cx,sy,cy,sz,cz);   // Поворачиваем
                    p = add(p,{0,0,camZ});                         // Отодвигаем сцену от камеры
                    if(p.z<=nearZ) continue;                       // Отсекаем «слишком близко» — без артефактов у стекла

                    int sxp,syp; if(!proj.toScreen(p,sxp,syp)) continue; // Проекция и отсечение по экрану

                    float invz = 1.0f/p.z + 1e-5f * (float)faceIndex; // Обратная глубина + bias для стабильности на ребрах
                    size_t idx=(size_t)syp*(size_t)g.winW+(size_t)sxp; // Индекс пикселя в нашем видимом окне
                    if(invz>zbuf[idx]){                       // Z-тест — пишем только ближнее
                        zbuf[idx]=invz;                       // Обновляем глубину
                        int shade=(int)std::round(shadeF*(int)(ramp.size()-1)); // Индекс символа по яркости
                        shade=std::clamp(shade,0,(int)ramp.size()-1); // Защита от округления
                        cbuf[idx].Char.AsciiChar = ramp[(size_t)shade]; // Символ
                        cbuf[idx].Attributes = faceColors[faceIndex];   // Цвет грани
                    }
                }
            }
        }

        blitFrame(h,g,cbuf);                                  // Выводим кадр напрямую в консоль с цветами
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }

    return 0;                                                 // Теперь достижимо на ESC
}