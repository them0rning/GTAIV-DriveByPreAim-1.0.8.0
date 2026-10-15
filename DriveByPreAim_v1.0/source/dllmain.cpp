// DriveByPreAim - aim from vehicles without shooting, for GTA IV 1.0.8.0
// Copyright (C) 2026  [your name]
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY;
// see the GNU General Public License (LICENSE.txt) for details.
//
// Port of the idea behind [original mod name] by [original author] (Complete Edition only).
// No code from that mod is used: its compiled .asi was analysed to learn the approach, and the
// equivalent game code was located in 1.0.8.0 and patched here. Written with the help of
// Claude (Anthropic). Built on zolika1351's iv-sdk (GPL-3.0).
//
//  Controller: hold LB/L1 in a vehicle  -> aim (drive-by pose) without shooting
//              press  RB/R1 while aiming -> shoot (handbrake is blocked only while aiming)
//  Mouse:      hold right mouse button   -> aim without shooting, left click shoots as normal
//  Optional:   people/drivers you point the gun at flee (TargetsFlee=1)
//
//  How it works (1.0.8.0 addresses, image base 0x400000):
//   - 0xAA5CF0 "does the player want to drive-by?" normally answers yes only while the attack
//     button is held. It is detoured to also answer yes while pre-aiming.
//   - While pre-aiming with a controller, the attack input is hidden from the game unless RB is
//     pressed, so the drive-by task (which reads attack separately to fire) does not shoot.
//   - Three "is attack held?" checks that drive the aim direction (0x98E55C) and the crosshair
//     (0x4A86D4, 0x4A87EA) are patched to also accept pre-aiming.
//   Every patch site is byte-checked first; if anything differs it is skipped and logged.

#include "../iv-sdk/include/IVSDK.cpp"  // https://github.com/zolika1351/iv-sdk
#include <map>
#include <cmath>
#include <cstdio>
#include <cstdarg>

using namespace Scripting;

static const uint32_t kWantsDriveByRVA = 0x6A5CF0;              // 1.0.8.0 only
static const uint8_t  kExpectedBytes[5] = { 0x55, 0x8B, 0x6C, 0x24, 0x08 };

static bool gEnabled = true, gPadScheme = true, gMouseRMB = true, gFlee = true;
static wchar_t gIni[MAX_PATH];
static FILE* gLog = nullptr;

static volatile bool gPreAim = false;     // set in pad hook, read by the game hook
static volatile uint8_t gPreAimByte = 0;  // same, for the aim-direction patch
static volatile bool gInVehicle = false;  // set by script tick
static bool gHooked = false;

typedef bool (__cdecl* WantsDriveByFn)(CPed*);
static WantsDriveByFn gOrigWantsDriveBy = nullptr;

static void Log(const char* fmt, ...)
{
	if (!gLog) return;
	SYSTEMTIME t; GetLocalTime(&t);
	fprintf(gLog, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
	va_list a; va_start(a, fmt); vfprintf(gLog, fmt, a); va_end(a);
	fputc('\n', gLog); fflush(gLog);
}

static CPed* PlayerPedPtr()
{
	CPlayerInfo* info = CPlayerInfo::GetPlayerInfo(CWorld::PlayerInFocus);
	return info ? info->m_pPlayerPed : nullptr;
}

// ---------- the game hook ----------
static bool __cdecl HookWantsDriveBy(CPed* ped)
{
	bool r = gOrigWantsDriveBy(ped);
	if (!r && gEnabled && gPreAim && ped && ped == PlayerPedPtr()) r = true;
	return r;
}

static bool InstallHook()
{
	if (plugin::gameVer != plugin::VERSION_1080) { Log("not GTA IV 1.0.8.0 - hook not installed"); return false; }
	uint8_t* target = (uint8_t*)(AddressSetter::gBaseAddress + kWantsDriveByRVA);
	if (target[0] == 0xE9)
	{
		// another mod already detoured this function: chain onto it
		uint8_t* prev = target + 5 + *(int32_t*)(target + 1);
		gOrigWantsDriveBy = (WantsDriveByFn)prev;
		Log("drive-by check already hooked by another mod (-> %p); chaining onto it", prev);
	}
	else if (memcmp(target, kExpectedBytes, 5) == 0)
	{
		// trampoline: original 5 bytes + jmp back
		uint8_t* tramp = (uint8_t*)VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (!tramp) return false;
		memcpy(tramp, target, 5);
		tramp[5] = 0xE9;
		*(int32_t*)(tramp + 6) = (int32_t)((target + 5) - (tramp + 10));
		gOrigWantsDriveBy = (WantsDriveByFn)tramp;
	}
	else
	{
		Log("unexpected bytes at drive-by check (%02X %02X %02X %02X %02X) - not installed",
			target[0], target[1], target[2], target[3], target[4]);
		return false;
	}

	DWORD old;
	VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &old);
	target[0] = 0xE9;
	*(int32_t*)(target + 1) = (int32_t)((uint8_t*)&HookWantsDriveBy - (target + 5));
	VirtualProtect(target, 5, old, &old);
	FlushInstructionCache(GetCurrentProcess(), target, 5);
	Log("drive-by check hooked at %p", target);
	return true;
}


// The vehicle's drive-by aim-direction function (0x98E3C6) only produces an aim direction
// while the attack button is held. Patch its
// "cmp dl,7F / jbe fail" so pre-aiming also counts.
static const uint32_t kAimCheckRVA = 0x58E55C;  // 80 FA 7F 0F 86 9E 01 00 00
static const uint8_t  kAimCheckBytes[9] = { 0x80, 0xFA, 0x7F, 0x0F, 0x86, 0x9E, 0x01, 0x00, 0x00 };
static bool InstallAimPatch()
{
	uint8_t* site = (uint8_t*)(AddressSetter::gBaseAddress + kAimCheckRVA);
	if (memcmp(site, kAimCheckBytes, 9) != 0)
	{
		Log("aim-direction check has unexpected bytes - patch skipped");
		return false;
	}
	uint8_t* pressedPath = site + 9;                  // continues computing the aim direction
	uint8_t* failPath    = site + 9 + 0x19E;          // "return false"
	uint8_t* stub = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!stub) return false;
	uint8_t* s = stub;
	*s++ = 0x80; *s++ = 0xFA; *s++ = 0x7F;                         // cmp dl, 7Fh
	*s++ = 0x0F; *s++ = 0x87; uint8_t* jaPressed = s; s += 4;      // ja pressed
	*s++ = 0x80; *s++ = 0x3D; *(uint32_t*)s = (uint32_t)&gPreAimByte; s += 4; *s++ = 0x00; // cmp byte [gPreAimByte],0
	*s++ = 0x0F; *s++ = 0x85; uint8_t* jnePressed = s; s += 4;     // jne pressed
	*s++ = 0xE9; *(int32_t*)s = (int32_t)(failPath - (s + 4)); s += 4;     // jmp fail
	uint8_t* pressed = s;
	*s++ = 0xE9; *(int32_t*)s = (int32_t)(pressedPath - (s + 4)); s += 4;  // jmp pressed path
	*(int32_t*)jaPressed  = (int32_t)(pressed - (jaPressed + 4));
	*(int32_t*)jnePressed = (int32_t)(pressed - (jnePressed + 4));

	DWORD old;
	VirtualProtect(site, 9, PAGE_EXECUTE_READWRITE, &old);
	site[0] = 0xE9; *(int32_t*)(site + 1) = (int32_t)(stub - (site + 5));
	site[5] = 0x90; site[6] = 0x90; site[7] = 0x90; site[8] = 0x90;
	VirtualProtect(site, 9, old, &old);
	FlushInstructionCache(GetCurrentProcess(), site, 9);
	Log("aim-direction patch installed at %p", site);
	return true;
}


// Generic "treat pre-aim as attack held" patch for a  [load/xor/... ; cmp r8,7F ; ja OK]  sequence.
// keep:   the original bytes to replay (loads + xor + any side effects), without the cmp/ja
// cmpReg: 0xF8+reg for "cmp r8,7F" (0x3C form used when reg==al)
static bool PatchAttackCheck(const char* name, uint32_t rva, const uint8_t* expect, int len,
                             int keepLen, bool regIsAl, uint8_t cmpModrm, uint32_t okRva, uint32_t failRva)
{
	uint8_t* site = (uint8_t*)(AddressSetter::gBaseAddress + rva);
	if (memcmp(site, expect, len) != 0) { Log("%s: unexpected bytes - skipped", name); return false; }
	uint8_t* ok   = (uint8_t*)(AddressSetter::gBaseAddress + okRva);
	uint8_t* fail = (uint8_t*)(AddressSetter::gBaseAddress + failRva);
	uint8_t* stub = (uint8_t*)VirtualAlloc(nullptr, 96, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!stub) return false;
	uint8_t* s = stub;
	memcpy(s, site, keepLen); s += keepLen;                               // original loads/side effects
	if (regIsAl) { *s++ = 0x3C; *s++ = 0x7F; }                            // cmp al,7F
	else { *s++ = 0x80; *s++ = cmpModrm; *s++ = 0x7F; }                  // cmp r8,7F
	*s++ = 0x0F; *s++ = 0x87; uint8_t* j1 = s; s += 4;                    // ja OK
	*s++ = 0x80; *s++ = 0x3D; *(uint32_t*)s = (uint32_t)&gPreAimByte; s += 4; *s++ = 0x00; // cmp byte [flag],0
	*s++ = 0x0F; *s++ = 0x85; uint8_t* j2 = s; s += 4;                    // jne OK
	*s++ = 0xE9; *(int32_t*)s = (int32_t)(fail - (s + 4)); s += 4;        // jmp fail
	uint8_t* okStub = s;
	*s++ = 0xE9; *(int32_t*)s = (int32_t)(ok - (s + 4)); s += 4;          // jmp OK
	*(int32_t*)j1 = (int32_t)(okStub - (j1 + 4));
	*(int32_t*)j2 = (int32_t)(okStub - (j2 + 4));
	DWORD old;
	VirtualProtect(site, len, PAGE_EXECUTE_READWRITE, &old);
	site[0] = 0xE9; *(int32_t*)(site + 1) = (int32_t)(stub - (site + 5));
	for (int i = 5; i < len; i++) site[i] = 0x90;
	VirtualProtect(site, len, old, &old);
	FlushInstructionCache(GetCurrentProcess(), site, len);
	Log("%s patched at %p", name, site);
	return true;
}

static void InstallReticlePatches()
{
	// 0x4A86D4: mov al,[ebx+28FE]; xor al,[ebx+28FC]; mov byte[esp+1A],1; cmp al,7F; ja 4A86EE   (else -> 4A86E9)
	static const uint8_t a[21] = { 0x8A,0x83,0xFE,0x28,0x00,0x00, 0x32,0x83,0xFC,0x28,0x00,0x00, 0xC6,0x44,0x24,0x1A,0x01, 0x3C,0x7F, 0x77,0x05 };
	PatchAttackCheck("reticle check A", 0x0A86D4, a, 21, 17, true, 0, 0x0A86EE, 0x0A86E9);
	// 0x4A87EA: mov dl,[esi+28FE]; xor dl,[esi+28FC]; cmp dl,7F; ja 4A875C   (else -> 4A87FF)
	static const uint8_t b[21] = { 0x8A,0x96,0xFE,0x28,0x00,0x00, 0x32,0x96,0xFC,0x28,0x00,0x00, 0x80,0xFA,0x7F, 0x0F,0x87,0x5D,0xFF,0xFF,0xFF };
	PatchAttackCheck("reticle check B", 0x0A87EA, b, 21, 12, false, 0xFA, 0x0A875C, 0x0A87FF);
}

// ---------- pad hook: runs right after the game reads the controller ----------
static inline bool Pressed(tPadValues& v) { return (uint8_t)(v.m_nCurrentValue ^ v.m_nUnknown4) > 0x7F; }
static inline void SetPressed(tPadValues& v, bool on) { v.m_nCurrentValue = on ? (uint8_t)(v.m_nUnknown4 ^ 0xFF) : v.m_nUnknown4; }

static bool GameHasFocus()
{
	DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
	return pid == GetCurrentProcessId();
}

static void PadHook(CPad* pad)
{
	if (!gHooked || !gEnabled || pad != CPad::GetPad()) return;
	if (!gInVehicle) { gPreAim = false; gPreAimByte = 0; return; }

	tPadValues& attack = pad->m_aValues[INPUT_VEH_ATTACK];
	tPadValues& hb     = pad->m_aValues[INPUT_VEH_HANDBRAKE];
	tPadValues& hb2    = pad->m_aValues[INPUT_VEH_HANDBRAKE_ALT];
	bool focus = GameHasFocus();
	bool lmbDown = focus && (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
	bool rmbDown = focus && (GetAsyncKeyState(VK_RBUTTON) & 0x8000);

	if (gMouseRMB && rmbDown)
	{
		// mouse scheme: RMB aims, LMB (the normal attack) still shoots - leave inputs alone
		gPreAim = true; gPreAimByte = 1;
		return;
	}
	if (gPadScheme && Pressed(attack) && !lmbDown)
	{
		// controller scheme: LB aims, RB shoots
		gPreAim = true; gPreAimByte = 1;
		bool fire = Pressed(hb) || Pressed(hb2);
		SetPressed(attack, fire);
		SetPressed(hb, false);
		SetPressed(hb2, false);
		return;
	}
	gPreAim = false; gPreAimByte = 0;
}

// ---------- optional: aimed-at people flee ----------
static const char* kPolice[] = { "M_Y_COP", "M_M_FATCOP_01", "M_Y_COP_TRAFFIC", "M_Y_STROOPER", "M_Y_SWAT", "M_M_FBI" };
static unsigned gPoliceHash[6];
static std::map<int, DWORD> gFledAt;

static bool IsPolice(int h) { for (unsigned x : gPoliceHash) if (IS_CHAR_MODEL(h, x)) return true; return false; }

static void FleeAimedAt(Ped player)
{
	int cam = 0; GET_GAME_CAM(&cam); if (!cam) return;
	float cx, cy, cz, rx, ry, rz;
	GET_CAM_POS(cam, &cx, &cy, &cz); GET_CAM_ROT(cam, &rx, &ry, &rz);
	const float d2r = 3.14159265f / 180.0f;
	float fx = -sinf(rz * d2r) * cosf(rx * d2r), fy = cosf(rz * d2r) * cosf(rx * d2r), fz = sinf(rx * d2r);
	Vehicle myCar = 0; if (IS_CHAR_IN_ANY_CAR(player)) GET_CAR_CHAR_IS_USING(player, &myCar);
	DWORD now = GetTickCount();
	auto pool = CPools::ms_pPedPool;
	for (int i = 0; pool && i < pool->m_nCount; i++)
	{
		auto p = pool->Get(i); if (!p) continue;
		int h = pool->GetIndex(p);
		if (h == player || IS_CHAR_INJURED(h)) continue;
		float x, y, z; GET_CHAR_COORDINATES(h, &x, &y, &z);
		float vx = x - cx, vy = y - cy, vz = z - cz;
		float along = vx * fx + vy * fy + vz * fz;
		if (along < 2.0f || along > 60.0f) continue;
		float px = vx - along * fx, py = vy - along * fy, pz = vz - along * fz;
		if (sqrtf(px * px + py * py + pz * pz) > 1.6f + along * 0.02f) continue;
		auto it = gFledAt.find(h); if (it != gFledAt.end() && now - it->second < 12000) continue;
		if (IsPolice(h)) continue;
		if (IS_CHAR_IN_ANY_CAR(h))
		{
			Vehicle car = 0; GET_CAR_CHAR_IS_USING(h, &car);
			if (!car || car == myCar || IS_CAR_DEAD(car)) continue;
			Ped driver = 0; GET_DRIVER_OF_CAR(car, &driver);
			if (!driver || driver == player || IsPolice(driver)) continue;
			// drive hard to a point ~300 m away, directly away from the player
			float plx, ply, plz, dx, dy, dz;
			GET_CHAR_COORDINATES(player, &plx, &ply, &plz);
			GET_CHAR_COORDINATES(driver, &dx, &dy, &dz);
			float ax = dx - plx, ay = dy - ply; float len = sqrtf(ax * ax + ay * ay); if (len < 0.1f) { ax = fx; ay = fy; len = 1.0f; }
			float tx = dx + ax / len * 300.0f, ty = dy + ay / len * 300.0f;
			TASK_CAR_MISSION_COORS_TARGET(driver, car, tx, ty, dz, 4, 28.0f, 2, 5, 10);
			SET_CHAR_KEEP_TASK(driver, true);
			gFledAt[driver] = now; gFledAt[h] = now;
		}
		else { TASK_SMART_FLEE_CHAR(h, player, 100.0f, 15000); gFledAt[h] = now; }
	}
	for (auto it = gFledAt.begin(); it != gFledAt.end(); ) it = (now - it->second > 30000) ? gFledAt.erase(it) : std::next(it);
}

// ---------- script tick ----------
static void LoadSettings()
{
	GetModuleFileNameW(plugin::GetCurrentModule(), gIni, MAX_PATH);
	wchar_t logPath[MAX_PATH]; wcscpy(logPath, gIni);
	if (wchar_t* d = wcsrchr(gIni, L'.')) wcscpy(d, L".ini");
	if (wchar_t* d = wcsrchr(logPath, L'.')) wcscpy(d, L".log");
	gEnabled   = GetPrivateProfileIntW(L"MAIN", L"Enabled", 1, gIni) != 0;
	gPadScheme = GetPrivateProfileIntW(L"MAIN", L"Controller_L1Aim_R1Shoot", 1, gIni) != 0;
	gMouseRMB  = GetPrivateProfileIntW(L"MAIN", L"Mouse_RightButtonAim", 1, gIni) != 0;
	gFlee      = GetPrivateProfileIntW(L"MAIN", L"TargetsFlee", 1, gIni) != 0;
	WritePrivateProfileStringW(L"MAIN", L"Enabled", gEnabled ? L"1" : L"0", gIni);
	WritePrivateProfileStringW(L"MAIN", L"Controller_L1Aim_R1Shoot", gPadScheme ? L"1" : L"0", gIni);
	WritePrivateProfileStringW(L"MAIN", L"Mouse_RightButtonAim", gMouseRMB ? L"1" : L"0", gIni);
	WritePrivateProfileStringW(L"MAIN", L"TargetsFlee", gFlee ? L"1" : L"0", gIni);
	gLog = _wfopen(logPath, L"w");
	Log("DriveByPreAim loaded: enabled=%d pad=%d mouse=%d flee=%d", gEnabled, gPadScheme, gMouseRMB, gFlee);
}

static void Tick()
{
	static bool init = false;
	if (!init)
	{
		LoadSettings();
		for (int i = 0; i < 6; i++) gPoliceHash[i] = rage::atStringHash(kPolice[i]);
		gHooked = gEnabled && InstallHook();
		if (gHooked) { InstallAimPatch(); InstallReticlePatches(); }
		init = true;
	}
	Player pl = CONVERT_INT_TO_PLAYERINDEX(GET_PLAYER_ID());
	if (!IS_PLAYER_PLAYING(pl)) { gInVehicle = false; return; }
	Ped player = 0; GET_PLAYER_CHAR(pl, &player);
	if (!player) { gInVehicle = false; return; }
	gInVehicle = IS_CHAR_SITTING_IN_ANY_CAR(player) != 0;

	bool pre = gPreAim && gInVehicle;

	if (gFlee && gEnabled && (pre || IS_PED_DOING_DRIVEBY(player)))
	{
		unsigned w = 0; GET_CURRENT_CHAR_WEAPON(player, &w);
		if (w) FleeAimedAt(player);
	}
}

void plugin::gameStartupEvent()
{
	plugin::processScriptsEvent::Add(Tick);
	plugin::processPadEvent::Add(PadHook);
}
