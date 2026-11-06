// Mouse sensitivity corrections for Battlefield Vietnam.
//
// Two independent features, both off by default:
//   1. correctedLookSensitivity   - the look sensitivity that feels doubled while
//                                   moving on foot.
//   2. stationaryMGInfSensitivity - use your Infantry sensitivity instead of
//                                   LandSea while on a stationary MG.
//
// WHY THIS IS DONE AT THE DIRECTINPUT LAYER
// -----------------------------------------
// bf42plus fixes the equivalent BF1942 issue inside Game::addPlayerInput, and its
// author is explicit about why it is NOT done in BFSoldier::handlePlayerInput: the
// server runs the same code, so changing the value there makes client and server
// disagree and you desync.
//
// bfvplus is already a dinput8.dll proxy, which puts us *upstream* of the whole
// input pipeline. If we scale the mouse delta here, the game genuinely receives a
// smaller mouse movement - local simulation and the packet sent to the server are
// built from the same number, so there is nothing to disagree about. That sidesteps
// the desync problem entirely and means we never have to find BFV's equivalent of
// addPlayerInput.
//
// Note this scales real mouse input, so it affects aim as well as view. That is the
// same net effect bf42plus has; it is not a view-only cosmetic change.
//
// THE LOOK ASYMMETRY BEING CORRECTED
// ----------------------------------
// In BFSoldier::handlePlayerInput (retail 0x0065F350) both look axes get the same
// base scaling - each is `* 30.0 * dt`:
//
//     yaw:   fld [LookX]  fmul [30.0]  fmul [dt]      (debug 0x6C2B0A)
//     pitch: fld [LookY]  fmul [30.0]  fmul [dt]      (debug 0x6C2C23)
//
// but they diverge afterwards. A two-iteration loop (debug 0x6C2BB0) calls 0x7D7940,
// which produces per-axis modifiers, and applies them:
//
//     forward *= [esp+0x24]        (conditional on a flag)
//     strafe  *= [esp+0x44]        (conditional on a flag)
//     xDelta  *= [esp+0x48]        (UNCONDITIONAL - it is the branch target)
//
// The yaw delta receives that modifier. The pitch delta computed right after the
// loop never does. We do not try to correct the engine maths (that is the part that
// would desync) - we scale the incoming mouse X so the felt result matches vertical.
//
// GAME ADDRESSES (retail BfVietnam.exe v1.21, image base 0x00400000)
// ------------------------------------------------------------------
// Derived from static analysis; the debug build (BfVietnam_DEBUG.exe) was used to
// name things, since it keeps assert text and profiler scope names.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "sensitivity.h"
#include "replay.h"
#include "replay_probe.h"
#include "replay_settings.h"

#include <cmath>
#include <cstdint>

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

bool  g_strafeFixEnabled = false;
float g_strafeFixScale   = 0.5f;
bool  g_strafeFixAnyMove = false;   // default: strafe only (matches the report)

bool  g_mgInfSensEnabled = false;

// ---------------------------------------------------------------------------
// Game structures
// ---------------------------------------------------------------------------

// Player manager global. buddycolors.cpp calls the same pointer g_playerList.
// Reached in the game as: mov ecx,[0xD7D01C]; mov eax,[ecx]; call [eax+0x24].
static const uintptr_t ADDR_playerMgr = 0x00D7D01C;

// Virtual call slots, verified against the assert site for
// "Localplayer should always have a vehicle" (debug 0x004F9B86):
//     playerMgr->vtable[+0x24]() -> localPlayer
//     player   ->vtable[+0x38]() -> vehicle
static const unsigned VSLOT_getLocalPlayer = 0x24 / 4;   // index 9
static const unsigned VSLOT_getVehicle     = 0x38 / 4;   // index 14

// BFSoldier primary vtable (RTTI .?AVBFSoldier@world@ref2@dice@@, complete object
// locator with offset==0 at 0x00C3EF08). Slot 39 (+0x9C) of this table is
// BFSoldier::handlePlayerInput at 0x0065F350, which is how it was confirmed.
// If the object the local player is driving has this vtable, they are on foot.
static const uintptr_t VT_BFSoldier = 0x00B9033C;

// BFSoldier caches the PlayerInput struct it was handed at this+0x480:
//     lea edi,[ebp+0x480]; mov ecx,0x3C; rep movsd     (retail 0x0065F412)
// 0x3C dwords = 60 floats. Index meanings match BF1942's PlayerInput.
static const size_t OFF_inputCache = 0x480;

enum ControlIndex {
    CTL_STRAFE  = 0,
    CTL_FORWARD = 3,
    CTL_LOOKX   = 4,
    CTL_LOOKY   = 5,
};

// Vehicle type query. Worked example at retail 0x004F6C52:
//     mov ecx,[obj+0x4C]        ; -> the object's template
//     mov eax,[0x00B81014]      ; interface id (value 0xC4C4)
//     call [edx+0x0C]           ; queryInterface(iid)
//     call [edx+0x9C]           ; getVehicleType()
//     cmp eax,0x15              ; 21 valid values, 0x15 = out of range
static const uintptr_t ADDR_pcoTemplateIID = 0x00B81014;
static const size_t    OFF_objTemplate     = 0x4C;
static const unsigned  VSLOT_queryInterface = 0x0C / 4;   // index 3
static const unsigned  VSLOT_getVehicleType = 0x9C / 4;   // index 39

// Ordinals read off the enumToString jump table at 0x0061818C (21 entries).
// The category table at 0x00617EA8 gives VCLand = 0, but a stationary MG is
// inherently a land vehicle, so checking the type alone is sufficient and saves
// having to locate getVehicleCategory's slot.
static const int VT_STATIONARY_MG = 9;

// Control-settings objects. Both are constructed exactly once, in the settings-root
// constructor, and stored on it:
//     0x004E0939  call InfantryControls::ctor (0x005A40E0) -> parent+0x10
//     0x004E0956  call LandSeaControls::ctor  (0x005A3160) -> parent+0x14
// Rather than chase the settings root down from a global, we patch those two call
// sites and record `this` as each object is built. Field layout confirmed twice -
// from the property registration (lea edx,[esi+0x10] / [esi+0x14]) and from the
// settings writer at 0x005A4BE8 / 0x005A3C82:
//     +0x10  float  mouse sensitivity
//     +0x14  byte   mouse invert
static const uintptr_t ADDR_call_infCtor     = 0x004E0939;
static const uintptr_t ADDR_call_landSeaCtor = 0x004E0956;
static const uintptr_t ADDR_infCtor          = 0x005A40E0;
static const uintptr_t ADDR_landSeaCtor      = 0x005A3160;
static const uintptr_t VT_InfantryControls   = 0x00B748C0;
static const uintptr_t VT_LandSeaControls    = 0x00B745C8;
static const size_t    OFF_sensitivity       = 0x10;
static const size_t    OFF_invert            = 0x14;

static void* s_infControls     = nullptr;
static void* s_landSeaControls = nullptr;

static bool ptrSane(const void* p)
{
    uintptr_t v = (uintptr_t)p;
    return v >= 0x00010000 && v < 0x7FF00000;
}

// Read one virtual function pointer out of an object's vtable.
static void* vslot(const void* obj, unsigned index)
{
    if (!ptrSane(obj)) return nullptr;
    void** vt = *(void***)obj;
    if (!ptrSane(vt)) return nullptr;
    void* fn = vt[index];
    return ptrSane(fn) ? fn : nullptr;
}

// The object the local player currently controls, or null if we are not in a live
// round (menus, loading, spectating with no vehicle).
static void* getLocalVehicle()
{
    void* mgr = *(void**)ADDR_playerMgr;
    if (!ptrSane(mgr)) return nullptr;

    typedef void* (__fastcall* getLocalPlayer_t)(void*, void*);
    getLocalPlayer_t getLocalPlayer = (getLocalPlayer_t)vslot(mgr, VSLOT_getLocalPlayer);
    if (!getLocalPlayer) return nullptr;

    void* player = getLocalPlayer(mgr, nullptr);
    if (!ptrSane(player)) return nullptr;

    typedef void* (__fastcall* getVehicle_t)(void*, void*);
    getVehicle_t getVehicle = (getVehicle_t)vslot(player, VSLOT_getVehicle);
    if (!getVehicle) return nullptr;

    void* vehicle = getVehicle(player, nullptr);
    return ptrSane(vehicle) ? vehicle : nullptr;
}

// Is the local player on foot, and if so are they moving in a way that triggers the
// look correction? Reads the input the soldier was handed last frame - one frame of
// latency, which at any playable framerate is not perceptible.
static bool localSoldierTriggers(void* vehicle)
{
    if (!vehicle) return false;
    if (*(void**)vehicle != (void*)VT_BFSoldier) return false;   // not on foot

    const float* ctl = (const float*)((const char*)vehicle + OFF_inputCache);
    if (!ptrSane(ctl)) return false;

    const float strafe = ctl[CTL_STRAFE];
    if (g_strafeFixAnyMove) {
        // bf42plus behaviour: any movement, forward or strafe.
        return strafe != 0.0f || ctl[CTL_FORWARD] != 0.0f;
    }
    return strafe != 0.0f;
}

// Is the local player manning a stationary MG?
static bool onStationaryMG(void* vehicle)
{
    if (!vehicle) return false;
    if (*(void**)vehicle == (void*)VT_BFSoldier) return false;   // on foot, not a PCO

    void* tmpl = *(void**)((char*)vehicle + OFF_objTemplate);
    if (!ptrSane(tmpl)) return false;

    typedef void* (__fastcall* queryInterface_t)(void*, void*, unsigned);
    queryInterface_t queryInterface = (queryInterface_t)vslot(tmpl, VSLOT_queryInterface);
    if (!queryInterface) return false;

    const unsigned iid = *(const unsigned*)ADDR_pcoTemplateIID;
    void* iface = queryInterface(tmpl, nullptr, iid);
    if (!ptrSane(iface)) return false;   // not a player-control-object template

    typedef int (__fastcall* getVehicleType_t)(void*, void*);
    getVehicleType_t getVehicleType = (getVehicleType_t)vslot(iface, VSLOT_getVehicleType);
    if (!getVehicleType) return false;

    return getVehicleType(iface, nullptr) == VT_STATIONARY_MG;
}

// Read a sensitivity/invert pair, validating the object still looks right.
static bool readControls(void* obj, uintptr_t expectVt, float& sens, bool& invert)
{
    if (!ptrSane(obj)) return false;
    if (*(void**)obj != (void*)expectVt) return false;
    sens   = *(const float*)((const char*)obj + OFF_sensitivity);
    invert = *(const unsigned char*)((const char*)obj + OFF_invert) != 0;
    return sens > 0.0f && sens < 100.0f;
}

// ---------------------------------------------------------------------------
// Scaling
// ---------------------------------------------------------------------------
//
// Mouse deltas are integers, so naively multiplying by 0.5 throws away every delta
// of 1 and makes slow movement stutter. Carry the fractional remainder across polls
// so the total distance moved is preserved.

static float s_carryX = 0.0f;
static float s_carryY = 0.0f;

static LONG scaleAxis(LONG raw, float k, float& carry)
{
    if (raw == 0) return 0;

    float v = (float)raw * k + carry;
    // Truncate toward zero, keep the remainder for next time.
    LONG out = (LONG)((v >= 0.0f) ? floorf(v) : ceilf(v));
    carry = v - (float)out;

    // Never let a real movement round away to nothing, or the mouse feels dead at
    // low speed. Push it to the smallest movement in the same direction.
    if (out == 0) {
        out = (raw > 0) ? 1 : -1;
        carry = 0.0f;
    }
    return out;
}

// Per-poll multipliers. kx/ky default to 1.0; flipY negates the Y axis.
static void currentScales(float& kx, float& ky, bool& flipY)
{
    kx = ky = 1.0f;
    flipY = false;

    if (!g_strafeFixEnabled && !g_mgInfSensEnabled) return;

    void* vehicle = getLocalVehicle();
    if (!vehicle) return;

    if (g_strafeFixEnabled && localSoldierTriggers(vehicle)) {
        float k = g_strafeFixScale;
        if (!(k > 0.0f) || k > 4.0f) k = 0.5f;   // guard against a silly ini value
        kx = k;
    }

    // Stationary MG: rescale both axes by infSens/landSens so mounting an MG does
    // not change how the mouse feels, and correct for an invert mismatch. This is
    // the same maths bf42plus uses; like theirs it is slightly approximate, because
    // the engine adds a small constant to the sensitivity elsewhere.
    if (g_mgInfSensEnabled && onStationaryMG(vehicle)) {
        float infSens = 0.0f, landSens = 0.0f;
        bool  infInv = false, landInv = false;
        if (readControls(s_infControls, VT_InfantryControls, infSens, infInv) &&
            readControls(s_landSeaControls, VT_LandSeaControls, landSens, landInv)) {
            float m = infSens / landSens;
            if (m > 0.0f && m < 10.0f) { kx *= m; ky *= m; }
            if (infInv != landInv) flipY = true;
        }
    }
}

// ---------------------------------------------------------------------------
// Capturing the control-settings objects
// ---------------------------------------------------------------------------
//
// Each constructor has exactly one call site, so we patch the 5-byte `call rel32`
// the same way buddycolors.cpp patches the AddBuddy call. Our stub records `this`
// and forwards unchanged. Signature is thiscall(this, parent) returning this.

typedef void* (__fastcall* ctrlCtor_t)(void* self, void* edx, void* parent);
static const ctrlCtor_t realInfCtor     = (ctrlCtor_t)ADDR_infCtor;
static const ctrlCtor_t realLandSeaCtor = (ctrlCtor_t)ADDR_landSeaCtor;

static void* __fastcall hook_infCtor(void* self, void* edx, void* parent)
{
    s_infControls = self;
    return realInfCtor(self, edx, parent);
}

static void* __fastcall hook_landSeaCtor(void* self, void* edx, void* parent)
{
    s_landSeaControls = self;
    return realLandSeaCtor(self, edx, parent);
}

// Repoint a 5-byte `call rel32` whose current target is `expect`.
static bool repointCall(uintptr_t site, uintptr_t expect, void* replacement)
{
    uint8_t* p = (uint8_t*)site;
    if (p[0] != 0xE8) return false;
    int32_t rel = *(int32_t*)(p + 1);
    if ((uintptr_t)(p + 5) + (uintptr_t)rel != expect) return false;

    DWORD old;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    *(int32_t*)(p + 1) = (int32_t)((uintptr_t)replacement - (uintptr_t)(p + 5));
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

void installControlSettingsHooks()
{
    if ((uintptr_t)GetModuleHandleA(nullptr) != 0x00400000) return;
    repointCall(ADDR_call_infCtor,     ADDR_infCtor,     (void*)&hook_infCtor);
    repointCall(ADDR_call_landSeaCtor, ADDR_landSeaCtor, (void*)&hook_landSeaCtor);
}

// ---------------------------------------------------------------------------
// DirectInput interception
// ---------------------------------------------------------------------------
//
// We do not implement wrapper COM objects - we patch the two vtable entries we care
// about. The vtable is shared by every device dinput8 hands out, so each hook checks
// the `this` pointer against the mouse device we saw created.

// {6F1D2B60-D5A0-11CF-BFC7-444553540000}
static const GUID kGuidSysMouse =
    { 0x6F1D2B60, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

static const unsigned VSLOT_CreateDevice   = 3;    // IDirectInput8
static const unsigned VSLOT_GetDeviceState = 9;    // IDirectInputDevice8
static const unsigned VSLOT_GetDeviceData  = 10;

typedef HRESULT(WINAPI* CreateDevice_t)(void* self, const GUID* rguid, void** dev, void* outer);
typedef HRESULT(WINAPI* GetDeviceState_t)(void* self, DWORD cbData, void* lpvData);
typedef HRESULT(WINAPI* GetDeviceData_t)(void* self, DWORD cbOde, void* rgdod,
                                         DWORD* pdwInOut, DWORD dwFlags);

static CreateDevice_t   s_realCreateDevice   = nullptr;
static GetDeviceState_t s_realGetDeviceState = nullptr;
static GetDeviceData_t  s_realGetDeviceData  = nullptr;

static void* s_mouseDevice  = nullptr;
static bool  s_deviceHooked = false;

// DIDEVICEOBJECTDATA, redeclared so we do not need <dinput.h>.
struct ObjData {
    DWORD dwOfs;
    DWORD dwData;
    DWORD dwTimeStamp;
    DWORD dwSequence;
};

static const DWORD DIMOFS_X = 0;   // offsetof(DIMOUSESTATE, lX)
static const DWORD DIMOFS_Y = 4;   // offsetof(DIMOUSESTATE, lY)

// Overwrite one vtable slot, returning the previous value.
static void* patchSlot(void* obj, unsigned index, void* replacement)
{
    void** vt = *(void***)obj;
    DWORD old;
    if (!VirtualProtect(&vt[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
        return nullptr;
    void* prev = vt[index];
    vt[index] = replacement;
    VirtualProtect(&vt[index], sizeof(void*), old, &old);
    return prev;
}

// The game polls the mouse once or more per frame, so these hooks double as the replay
// recorder's frame tick (and the probe counts which thread makes the calls). Called for the
// mouse device whether or not the poll succeeded: a frame happened either way.
static void pollCallbacks(int kind)
{
    if (g_replay.probe) probe_onInputPoll(kind);
    replay_onInputPoll();
}

static HRESULT WINAPI hook_GetDeviceState(void* self, DWORD cbData, void* lpvData)
{
    HRESULT hr = s_realGetDeviceState(self, cbData, lpvData);
    if (self == s_mouseDevice) pollCallbacks(0);
    if (FAILED(hr) || self != s_mouseDevice || !lpvData) return hr;

    // Immediate mode: the data format is DIMOUSESTATE or DIMOUSESTATE2, both of
    // which start with LONG lX, lY.
    if (cbData >= sizeof(LONG) * 3) {
        float kx, ky; bool flipY;
        currentScales(kx, ky, flipY);
        if (kx != 1.0f || ky != 1.0f || flipY) {
            LONG* axes = (LONG*)lpvData;
            if (kx != 1.0f) axes[0] = scaleAxis(axes[0], kx, s_carryX);
            if (ky != 1.0f) axes[1] = scaleAxis(axes[1], ky, s_carryY);
            if (flipY)      axes[1] = -axes[1];
        }
    }
    return hr;
}

static HRESULT WINAPI hook_GetDeviceData(void* self, DWORD cbOde, void* rgdod,
                                         DWORD* pdwInOut, DWORD dwFlags)
{
    HRESULT hr = s_realGetDeviceData(self, cbOde, rgdod, pdwInOut, dwFlags);
    if (self == s_mouseDevice) pollCallbacks(1);
    if (FAILED(hr) || self != s_mouseDevice || !rgdod || !pdwInOut) return hr;

    float kx, ky; bool flipY;
    currentScales(kx, ky, flipY);
    if ((kx == 1.0f && ky == 1.0f && !flipY) || cbOde < sizeof(ObjData)) return hr;

    // Buffered mode: scale every axis event in the returned batch.
    DWORD count = *pdwInOut;
    for (DWORD i = 0; i < count; ++i) {
        ObjData* e = (ObjData*)((char*)rgdod + (size_t)i * cbOde);
        if (e->dwOfs == DIMOFS_X && kx != 1.0f) {
            e->dwData = (DWORD)scaleAxis((LONG)e->dwData, kx, s_carryX);
        } else if (e->dwOfs == DIMOFS_Y) {
            LONG v = (LONG)e->dwData;
            if (ky != 1.0f) v = scaleAxis(v, ky, s_carryY);
            if (flipY)      v = -v;
            e->dwData = (DWORD)v;
        }
    }
    return hr;
}

static HRESULT WINAPI hook_CreateDevice(void* self, const GUID* rguid,
                                        void** dev, void* outer)
{
    HRESULT hr = s_realCreateDevice(self, rguid, dev, outer);
    if (FAILED(hr) || !dev || !*dev || !rguid) return hr;

    if (memcmp(rguid, &kGuidSysMouse, sizeof(GUID)) != 0) return hr;

    s_mouseDevice = *dev;

    // The device vtable is shared, so only hook it once. Both hooks filter on
    // `self == s_mouseDevice` anyway, so a keyboard device sharing the table is
    // passed through untouched.
    if (!s_deviceHooked) {
        s_realGetDeviceState = (GetDeviceState_t)
            patchSlot(*dev, VSLOT_GetDeviceState, (void*)&hook_GetDeviceState);
        s_realGetDeviceData = (GetDeviceData_t)
            patchSlot(*dev, VSLOT_GetDeviceData, (void*)&hook_GetDeviceData);
        s_deviceHooked = (s_realGetDeviceState != nullptr);
    }
    return hr;
}

void installSensitivityHooks(void* dinput8)
{
    if (!ptrSane(dinput8)) return;
    if (s_realCreateDevice) return;               // already installed

    // Only meaningful inside BfVietnam.exe at its expected base.
    if ((uintptr_t)GetModuleHandleA(nullptr) != 0x00400000) return;

    s_realCreateDevice = (CreateDevice_t)
        patchSlot(dinput8, VSLOT_CreateDevice, (void*)&hook_CreateDevice);
}
