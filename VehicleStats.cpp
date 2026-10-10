// VehicleStats.cpp - GTA San Andreas 1.0 US (including "compact"/Hoodlum executables)
//
// Quick stats screen also available inside vehicles:
//  - Inside a vehicle: open with the key configured in the .ini (TAB by default) or LEFT D-pad
//    (reads the game's own CPad state, so it works with GInput). On foot: behaves like the original game.
//  - Inside a vehicle, the "Stamina" row displays the current vehicle's skill, using the same
//    bar/value format as the game's stats screen:
//        car/truck/quad -> driving skill          (stat 160, text STAT160)
//        motorcycle (subclass 9) -> motorcycle skill (stat 229, text STAT229)
//        bicycle (BMX, 10) -> cycling skill       (stat 230, text STAT230)
//        plane/helicopter -> flying skill         (stat 223, text STAT223)
//        boat/train/trailer -> remains "Stamina"
//  - Optional (ShowPercent = 1 in the .ini): displays the percentage at the RIGHT edge, inside each
//    bar: in the quick stats window, the pause menu stats screen, and the notification at the top
//    of the screen that appears when a skill is improved.
//
// Everything below has been verified in gta_sa.exe v1.0 US:
//   CHud::Draw (original code):
//     58FC24  call CPad::GetDisplayVitalStats
//     58FC29  test ax,ax / je 58FC4C        <- these 5 bytes are replaced with a JMP to Stub1
//     58FC32  call FindPlayerVehicle        <- redirected to FindVehHook
//   CHud::DrawVitalStats = 0x589650..0x58A158: 6 calls to CSprite2d::DrawBarChart (0x728640).
//     Stamina row: text at 0x589CD8 (CText::Get, key STAT022) and value at
//     0x589D30 (CStats::GetStatValue, id 0x16).
//   Pause menu stats screen: DrawBarChart at 0x574F54 (inside the routine that draws each
//     stat row that has a bar).
//   Top-of-screen notification when a stat/skill increases: DrawBarChart at 0x58BFB8
//     (the routine that builds the "STAT%d" text with a "+" sign; both execution paths go through
//     this single call).
//   DrawBarChart(x, y, width, height, progress 0..100, add, percentage, border, color, color2):
//     the game already has a percentage text argument ("percentage"), but places it at the end
//     of the filled portion; therefore, the plugin draws the text at the right edge using CFont,
//     saving and restoring the font state (0xC71A60..0xC71AA7) so other text is unaffected.
//   m_nVehicleSubClass = vehicle+0x594: 0 car, 1 monster truck, 2 quad, 3 helicopter, 4 plane,
//     5 boat, 6 train, 7 fake helicopter, 8 fake plane, 9 motorcycle, 10 bicycle (BMX), 11 trailer.
//
// Compile as Win32 (x86) using MSVC. The output file must be named VehicleStats.asi.
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

static const uintptr_t VERSION_ADDR   = 0x82457C;
static const uint32_t  VERSION_OK     = 0x94BF;

static const uintptr_t TEST_SITE      = 0x58FC29;  // test ax,ax / je 58FC4C
static const uintptr_t VEH_CALL_SITE  = 0x58FC32;  // call FindPlayerVehicle
static const uintptr_t FIND_VEHICLE   = 0x56E0D0;  // CVehicle* FindPlayerVehicle(int, bool)
static const uintptr_t GET_PAD        = 0x53FB70;  // CPad* CPad::GetPad(int)

static const uintptr_t VS_LO          = 0x589650;  // CHud::DrawVitalStats
static const uintptr_t VS_HI          = 0x58A158;
static const uintptr_t FN_BAR         = 0x728640;  // CSprite2d::DrawBarChart(...)
static const uintptr_t SITE_MENU_BAR  = 0x574F54;  // DrawBarChart on the pause menu stats screen
static const uintptr_t SITE_NOTIF_BAR = 0x58BFB8;  // DrawBarChart in the skill-up notification
static const uintptr_t FN_TEXTGET     = 0x6A0050;  // CText::Get(char*)
static const uintptr_t FN_STATVALUE   = 0x558E40;  // CStats::GetStatValue(ushort)
static const uintptr_t SITE_STAMINA_TXT  = 0x589CD8;
static const uintptr_t SITE_STAMINA_STAT = 0x589D30;
static const uintptr_t KEY_STAT022    = 0x866BE4;  // "STAT022" (Stamina)
static const int       OFF_SUBCLASS   = 0x594;

// CFont (all addresses verified in the executable)
static const uintptr_t FN_SETSCALE    = 0x719380;  // (float x, float y)
static const uintptr_t FN_SETCOLOR    = 0x719430;  // (CRGBA as a dword)
static const uintptr_t FN_SETSTYLE    = 0x719490;  // (byte)
static const uintptr_t FN_SETWRAPX    = 0x7194D0;  // (float)
static const uintptr_t FN_SETRJWRAP   = 0x7194F0;  // (float)
static const uintptr_t FN_SETDROPCOL  = 0x719510;  // (CRGBA as a dword)
static const uintptr_t FN_SETDROPPOS  = 0x719590;  // (byte)
static const uintptr_t FN_SETPROP     = 0x7195B0;  // SetProportional(byte)
static const uintptr_t FN_SETBACK     = 0x7195C0;  // SetBackground(byte, byte)
static const uintptr_t FN_SETJUSTIFY  = 0x719600;  // SetJustify(byte)
static const uintptr_t FN_SETORIENT   = 0x719610;  // (byte) 0 center, 1 left, 2 right
static const uintptr_t FN_PRINT       = 0x71A700;  // (float x, float y, char* text) [8-bit text in SA]
static const uintptr_t FN_STRWIDTH    = 0x71A0E0;  // float (char* text, bool, bool)
static const uintptr_t FONT_STATE     = 0xC71A60;  // CFont state block
static const size_t    FONT_STATE_LEN = 0x48;

static uintptr_t kCont = 0x58FC2E;   // continue: check whether the player is in a vehicle
static uintptr_t kSkip = 0x58FC4C;   // skip: draw the radar normally

// ---------------- Configuration ----------------
static bool  g_enabled  = true;
static bool  g_useKey   = true;
static bool  g_usePad   = true;
static int   g_vk       = VK_TAB;
static int   g_padIdx   = 10;      // index (in shorts) in CControllerState; 10 = D-pad left
static bool  g_skillRow = true;
static bool  g_percent  = false;
// Only affects the top notification (skill improved): the game draws a "+" at the end of the bar
// and measures the text smaller than it is actually rendered, so "100%" overlaps the border / "+".
static float g_noticeScale  = 1.45f;   // estimated width = NoticeWidthScale * measured width - NoticeWidthOffset * height
static float g_noticeOffset = 0.80f;
static bool  g_center       = false;  // [Percent] CenterText: center the text inside the bar
static float g_noticePad    = 1.20f;   // distance from the right edge (in multiples of the bar height)
static int   g_logCount     = 0;
static float g_textScale = 1.0f;

struct PadBtn { const char* name; int idx; };
static const PadBtn kPad[] = {
    {"L1",4},{"L2",5},{"R1",6},{"R2",7},{"UP",8},{"DOWN",9},{"LEFT",10},{"RIGHT",11},
    {"START",12},{"SELECT",13},{"SQUARE",14},{"TRIANGLE",15},{"CROSS",16},{"CIRCLE",17},
    {"L3",18},{"R3",19}
};

static char g_iniPath[MAX_PATH];

static int ParseKey(const char* s)
{
    if (!s[0]) return VK_TAB;
    if (!_stricmp(s, "TAB"))      return VK_TAB;
    if (!_stricmp(s, "CAPSLOCK")) return VK_CAPITAL;
    if (!_stricmp(s, "ENTER"))    return VK_RETURN;
    if (!_stricmp(s, "SPACE"))    return VK_SPACE;
    if (!_stricmp(s, "LSHIFT"))   return VK_LSHIFT;
    if (!_stricmp(s, "LCTRL"))    return VK_LCONTROL;
    if (!_stricmp(s, "LALT"))     return VK_LMENU;
    if ((s[0] == 'F' || s[0] == 'f') && s[1])
    {
        int n = atoi(s + 1);
        if (n >= 1 && n <= 12) return VK_F1 + n - 1;
    }
    if (strlen(s) == 1)
    {
        char c = s[0];
        if (c >= 'a' && c <= 'z') c -= 32;
        return (unsigned char)c;
    }
    return (int)strtol(s, nullptr, 0);
}

static float ReadFloat(const char* sec, const char* key, float def)
{
    char buf[32], d[32];
    sprintf(d, "%g", def);
    GetPrivateProfileStringA(sec, key, d, buf, sizeof(buf), g_iniPath);
    return (float)atof(buf);
}

static void LoadConfig()
{
    char buf[64];
    g_enabled = GetPrivateProfileIntA("VehicleStats", "Enabled", 1, g_iniPath) != 0;
    g_useKey  = GetPrivateProfileIntA("VehicleStats", "UseKeyboard", 1, g_iniPath) != 0;
    g_usePad  = GetPrivateProfileIntA("VehicleStats", "UsePad", 1, g_iniPath) != 0;

    GetPrivateProfileStringA("VehicleStats", "KeyboardKey", "TAB", buf, sizeof(buf), g_iniPath);
    g_vk = ParseKey(buf);

    GetPrivateProfileStringA("VehicleStats", "PadButton", "LEFT", buf, sizeof(buf), g_iniPath);
    g_padIdx = 10;
    for (const PadBtn& b : kPad)
        if (!_stricmp(buf, b.name)) { g_padIdx = b.idx; break; }

    g_skillRow  = GetPrivateProfileIntA("Layout", "SkillRow", 1, g_iniPath) != 0;
    g_percent   = GetPrivateProfileIntA("Percent", "ShowPercent", 0, g_iniPath) != 0;
    g_textScale = ReadFloat("Percent", "TextScale", 1.0f);
    g_center       = GetPrivateProfileIntA("Percent", "CenterText", 0, g_iniPath) != 0;
    g_noticeScale  = ReadFloat("Percent", "NoticeWidthScale", 1.45f);
    g_noticeOffset = ReadFloat("Percent", "NoticeWidthOffset", 0.80f);
    g_noticePad    = ReadFloat("Percent", "NoticeRightPad", 1.20f);
    if (g_noticeScale < 0.5f) g_noticeScale = 0.5f;   if (g_noticeScale > 3.0f) g_noticeScale = 3.0f;
    if (g_noticePad < 0.0f) g_noticePad = 0.0f;       if (g_noticePad > 4.0f) g_noticePad = 4.0f;
    if (g_textScale < 0.2f) g_textScale = 0.2f;
    if (g_textScale > 5.0f) g_textScale = 5.0f;
}

// ---------------- Logging ----------------
static void Log(const char* fmt, ...)
{
    FILE* f = fopen("VehicleStats.log", "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "\n");
    fclose(f);
}

// ---------------- Current drawing state ----------------
static bool        g_vehMode  = false;     // Drawing the stats screen INSIDE a vehicle
static unsigned    g_skillId  = 0;         // 0 = keep Stamina
static const char* g_skillKey = nullptr;

static const char kKey160[] = "STAT160";   // Driving skill
static const char kKey223[] = "STAT223";   // Flying skill
static const char kKey229[] = "STAT229";   // Motorcycle skill
static const char kKey230[] = "STAT230";   // Cycling skill

static void ChooseSkill(void* veh)
{
    g_skillId = 0;
    g_skillKey = nullptr;
    if (!g_skillRow || !veh) return;
    int sub = *(volatile int*)((uint8_t*)veh + OFF_SUBCLASS);
    switch (sub)
    {
        case 0: case 1: case 2:         g_skillId = 160; g_skillKey = kKey160; break; // car, truck, quad
        case 3: case 4: case 7: case 8: g_skillId = 223; g_skillKey = kKey223; break; // helicopter/plane
        case 9:                         g_skillId = 229; g_skillKey = kKey229; break; // motorcycle
        case 10:                        g_skillId = 230; g_skillKey = kKey230; break; // bicycle (BMX)
        default: break;                                                               // boat (5), train (6), trailer (11)
    }
}

// ---------------- Trigger ----------------
static bool GameFocused()
{
    HWND h = GetForegroundWindow();
    DWORD pid = 0;
    if (h) GetWindowThreadProcessId(h, &pid);
    return pid == GetCurrentProcessId();
}

static void* PlayerVehicle()
{
    return ((void* (__cdecl*)(int, bool))FIND_VEHICLE)(-1, false);
}

static bool Triggered(void* pad)
{
    if (g_useKey && GameFocused() && (GetAsyncKeyState(g_vk) & 0x8000)) return true;
    if (g_usePad && pad)
    {
        short v = ((volatile short*)pad)[g_padIdx];   // CPad::NewState is the first member
        if (v > 100) return true;
    }
    return false;
}

static int  g_logForce = 0, g_logVeh = 0;
static bool g_prevForce = false;

// Only applies INSIDE a vehicle: on foot, the screen opens only through the game's original button.
static bool __cdecl ForceNow()
{
    if (!g_enabled) return false;
    if (!PlayerVehicle()) return false;
    void* pad = ((void* (__cdecl*)(int))GET_PAD)(0);
    bool t = Triggered(pad);
    if (t != g_prevForce)
    {
        g_prevForce = t;
        if (g_logForce < 20)
        {
            ++g_logForce;
            Log("Vehicle trigger %s", t ? "PRESSED" : "released");
        }
    }
    return t;
}

// ---------------- CHud::Draw hooks ----------------
// Replaces "test ax,ax / je 58FC4C": if the game already returned "yes", continue normally;
// if it returned "no" but our trigger (inside a vehicle) is active, continue as if it returned "yes".
__declspec(naked) static void Stub1()
{
    __asm {
        test ax, ax
        jnz  cont
        call ForceNow
        test al, al
        jz   skip
    cont:
        jmp  dword ptr [kCont]
    skip:
        jmp  dword ptr [kSkip]
    }
}

// Called at 58FC32: tells the game there is NO vehicle while the trigger is active,
// and enables "inside vehicle" mode (skill row).
static void* __cdecl FindVehHook(int player, bool remote)
{
    void* v = ((void* (__cdecl*)(int, bool))FIND_VEHICLE)(player, remote);
    g_vehMode = false;
    g_skillId = 0;
    g_skillKey = nullptr;
    if (v && ForceNow())
    {
        g_vehMode = true;
        ChooseSkill(v);
        if (g_logVeh < 5)
        {
            ++g_logVeh;
            Log("Vehicle + trigger: displaying stats screen (subclass=%d, skill=%u).",
                *(volatile int*)((uint8_t*)v + OFF_SUBCLASS), g_skillId);
        }
        return nullptr;
    }
    return v;
}

// ---------------- Skill row (inside CHud::DrawVitalStats) ----------------
typedef const void* (__thiscall *TextGet_t)(void* self, const char* key);
typedef float (__cdecl *StatValue_t)(unsigned short id);

// Stamina row text -> vehicle skill name
static const void* __fastcall HookTextGet(void* self, void* /*edx*/, const char* key)
{
    if (g_vehMode && g_skillKey && key == (const char*)KEY_STAT022) key = g_skillKey;
    return ((TextGet_t)FN_TEXTGET)(self, key);
}

// Stamina row value -> vehicle skill value
static float __cdecl HookStat(unsigned short id)
{
    if (g_vehMode && g_skillId && id == 0x16) id = (unsigned short)g_skillId;
    return ((StatValue_t)FN_STATVALUE)(id);
}

// ---------------- Percentage at the right edge of bars ----------------
typedef void  (__cdecl *Bar_t)(float x, float y, int w, int h, float progress,
                               int add, int pct, int border, unsigned int fore, unsigned int back);
typedef void  (__cdecl *FSet2_t)(float, float);
typedef void  (__cdecl *FSet1f_t)(float);
typedef void  (__cdecl *FSetDw_t)(unsigned int);
typedef void  (__cdecl *FSetB_t)(int);
typedef void  (__cdecl *FSetB2_t)(int, int);
typedef void  (__cdecl *FPrint_t)(float, float, char*);
typedef float (__cdecl *FWidth_t)(char*, bool, bool);

static void DrawPercent(float x, float y, int w, int h, float progress, bool notice)
{
    if (w <= 0 || h <= 0) return;

    int pct = (int)progress;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    // SA text uses 8-bit characters (not 16-bit as in III/VC): digits and '%' are ASCII
    char text[16];
    sprintf(text, "%d%%", pct);

    // Save the font state (subsequent text depends on it)
    uint8_t saved[FONT_STATE_LEN];
    memcpy(saved, (const void*)FONT_STATE, FONT_STATE_LEN);

    float scaleX = (float)h * 0.03f * g_textScale;     // Same proportions as the game's own text
    float scaleY = (float)h * 0.04f * g_textScale;

    ((FSetDw_t)FN_SETCOLOR)(0xFFFFFFFFu);              // White
    ((FSetDw_t)FN_SETDROPCOL)(0xFF000000u);            // Black shadow
    ((FSetB_t) FN_SETDROPPOS)(1);
    ((FSetB_t) FN_SETSTYLE)(1);
    ((FSetB_t) FN_SETPROP)(1);                         // Proportional font (same as the game's window)
    ((FSetB_t) FN_SETJUSTIFY)(0);
    ((FSetB2_t)FN_SETBACK)(0, 0);                      // No text background
    ((FSetB_t) FN_SETORIENT)(1);                       // Left-aligned (position is calculated manually)
    ((FSet1f_t)FN_SETWRAPX)(10000.0f);
    ((FSet1f_t)FN_SETRJWRAP)(0.0f);
    ((FSet2_t) FN_SETSCALE)(scaleX, scaleY);

    float tw = ((FWidth_t)FN_STRWIDTH)(text, true, true);
    float px;
    if (!notice)
    {
        // Quick stats window and pause menu: preserve the original behavior
        // Right edge INSIDE the bar (with a small margin to avoid touching the border)
        float pad = (float)h * 0.3f;
        if (pad < 2.0f) pad = 2.0f;
        px = x + (float)w - tw - pad;
    }
    else
    {
        // Top notification: use the corrected width and leave room for the game's "+" before the edge
        float real = g_noticeScale * tw - g_noticeOffset * (float)h;
        if (real < tw) real = tw;
        float pad = (float)h * g_noticePad;
        if (pad < 2.0f) pad = 2.0f;
        px = x + (float)w - real - pad;
        if (g_logCount < 12)
        { ++g_logCount; Log("Notice '%s': x=%.0f w=%d h=%d measured=%.1f estimated=%.1f px=%.1f (right edge=%.1f)",
              text, x, w, h, tw, real, px, x + (float)w); }
    }
    if (g_center)
    {
        // Centered inside the bar (quick stats window, pause menu, and skill notification)
        float cw = tw;
        if (notice)
        {
            float real = g_noticeScale * tw - g_noticeOffset * (float)h;
            if (real > cw) cw = real;
        }
        px = x + ((float)w - cw) * 0.5f;
    }
    if (px < x) px = x;                                // If the text is too wide, keep it from extending past the left edge
    float py = y + 2.0f;
    ((FPrint_t)FN_PRINT)(px, py, text);

    memcpy((void*)FONT_STATE, saved, FONT_STATE_LEN);  // Restore the font state
}

static void __cdecl HookBar(float x, float y, int w, int h, float progress,
                            int add, int pct, int border, unsigned int fore, unsigned int back)
{
    ((Bar_t)FN_BAR)(x, y, w & 0xFFFF, h & 0xFF, progress, add, 0 /* disable native percentage text */, border, fore, back);
    if (g_percent) DrawPercent(x, y, w & 0xFFFF, h & 0xFF, progress, false);
}
static void __cdecl HookBarNotice(float x, float y, int w, int h, float progress,
                                  int add, int pct, int border, unsigned int fore, unsigned int back)
{
    ((Bar_t)FN_BAR)(x, y, w & 0xFFFF, h & 0xFF, progress, add, 0, border, fore, back);
    if (g_percent) DrawPercent(x, y, w & 0xFFFF, h & 0xFF, progress, true);
}

// ---------------- Initialization ----------------
static bool WriteJmp(uintptr_t at, void* target)
{
    DWORD old;
    if (!VirtualProtect((void*)at, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    *(uint8_t*)at = 0xE9;
    *(int32_t*)(at + 1) = (int32_t)((uintptr_t)target - (at + 5));
    VirtualProtect((void*)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, 5);
    return true;
}

static bool WriteCall(uintptr_t at, void* target)
{
    DWORD old;
    if (!VirtualProtect((void*)at, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    *(uint8_t*)at = 0xE8;
    *(int32_t*)(at + 1) = (int32_t)((uintptr_t)target - (at + 5));
    VirtualProtect((void*)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, 5);
    return true;
}

static uintptr_t CallTarget(uintptr_t site)
{
    uint8_t* c = (uint8_t*)site;
    if (c[0] != 0xE8) return 0;
    return site + 5 + (uintptr_t)(intptr_t)(*(int32_t*)(c + 1));
}

// Count "call <target>" instructions inside DrawVitalStats
static int CountCalls(uintptr_t target)
{
    int n = 0;
    for (uintptr_t a = VS_LO; a < VS_HI - 4; ++a)
        if (*(uint8_t*)a == 0xE8 && CallTarget(a) == target) ++n;
    return n;
}

static int PatchCalls(uintptr_t target, void* hook)
{
    int n = 0;
    for (uintptr_t a = VS_LO; a < VS_HI - 4; ++a)
        if (*(uint8_t*)a == 0xE8 && CallTarget(a) == target)
            if (WriteCall(a, hook)) ++n;
    return n;
}

BOOL APIENTRY DllMain(HMODULE hm, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(hm);

    FILE* f = fopen("VehicleStats.log", "w");
    if (f) fclose(f);

    GetModuleFileNameA(hm, g_iniPath, MAX_PATH);
    char* dot = strrchr(g_iniPath, '.');
    if (dot) strcpy(dot, ".ini");

    if (*(uint32_t*)VERSION_ADDR != VERSION_OK)
    {
        Log("Unrecognized executable version (expected 1.0 US). Mod disabled.");
        return TRUE;
    }

    LoadConfig();
    Log("Config: enabled=%d key=%d(vk=0x%02X) pad=%d(idx=%d) skillRow=%d percent=%d scale=%.2f",
        g_enabled, g_useKey, g_vk, g_usePad, g_padIdx, g_skillRow, g_percent, g_textScale);

    // Verify the original bytes before modifying anything
    static const uint8_t expectTest[5] = { 0x66, 0x85, 0xC0, 0x74, 0x1E };
    uint8_t* t = (uint8_t*)TEST_SITE;

    if (memcmp(t, expectTest, 5) != 0)
    {
        Log("Bytes at 0x%08X differ from expected (%02X %02X %02X %02X %02X). "
            "Another mod may have modified this area. Mod disabled.",
            (unsigned)TEST_SITE, t[0], t[1], t[2], t[3], t[4]);
        return TRUE;
    }
    if (CallTarget(VEH_CALL_SITE) != FIND_VEHICLE)
    {
        Log("Call at 0x%08X does not point to FindPlayerVehicle (target=0x%08X). Mod disabled.",
            (unsigned)VEH_CALL_SITE, (unsigned)CallTarget(VEH_CALL_SITE));
        return TRUE;
    }

    bool ok1 = WriteCall(VEH_CALL_SITE, (void*)&FindVehHook);
    bool ok2 = WriteJmp(TEST_SITE, (void*)&Stub1);
    Log("Patch: vehicle check=%s, button test=%s",
        ok1 ? "OK" : "FAILED", ok2 ? "OK" : "FAILED");

    // ---- Skill row (replaces Stamina) ----
    if (g_skillRow)
    {
        if (CallTarget(SITE_STAMINA_TXT) == FN_TEXTGET && CallTarget(SITE_STAMINA_STAT) == FN_STATVALUE)
        {
            bool a = WriteCall(SITE_STAMINA_TXT,  (void*)&HookTextGet);
            bool b = WriteCall(SITE_STAMINA_STAT, (void*)&HookStat);
            Log("Skill row: text=%s value=%s", a ? "OK" : "FAILED", b ? "OK" : "FAILED");
        }
        else
        {
            g_skillRow = false;
            Log("Skill row NOT applied: stamina calls differ from expected.");
        }
    }

    // ---- Percentages on bars (quick stats window + pause menu) ----
    if (g_percent)
    {
        int nBar = CountCalls(FN_BAR);
        if (nBar == 6)
        {
            int c = PatchCalls(FN_BAR, (void*)&HookBar);
            Log("Percentage (quick stats window): %d bars", c);
        }
        else
            Log("Percentage NOT applied to quick stats window: unexpected bar count (%d, expected 6).", nBar);

        if (CallTarget(SITE_MENU_BAR) == FN_BAR)
            Log("Percentage (pause menu): %s", WriteCall(SITE_MENU_BAR, (void*)&HookBar) ? "OK" : "FAILED");
        else
            Log("Percentage NOT applied to pause menu: call at 0x%08X differs from expected.", (unsigned)SITE_MENU_BAR);

        if (CallTarget(SITE_NOTIF_BAR) == FN_BAR)
            Log("Percentage (skill-up notification): %s", WriteCall(SITE_NOTIF_BAR, (void*)&HookBarNotice) ? "OK" : "FAILED");
        else
            Log("Percentage NOT applied to skill notification: call at 0x%08X differs from expected.", (unsigned)SITE_NOTIF_BAR);
    }
    return TRUE;
}
