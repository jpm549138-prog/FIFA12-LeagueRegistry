#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "LeagueConfig.h"
#include "LogPath.h"

namespace {

constexpr wchar_t kPluginVersion[] = L"FIFA12LeagueRegistry Generic v0.6 Candidate";
constexpr wchar_t kExpectedSha256[] =
    L"42fd471f7437849b936db605cdd51e78ebdadf536c6653c87870e0127e532f70";
constexpr DWORD kExpectedFileSize = 23449504;
constexpr std::uintptr_t kExpectedLookupRva = 0x010A6220;
constexpr std::uintptr_t kCountsRva = 0x01568C14;
constexpr std::uintptr_t kPointersRva = 0x01568C30;
constexpr DWORD kRetryCount = 120;
constexpr DWORD kRetryDelayMs = 1000;
constexpr SIZE_T kStolenLength = 5;
constexpr std::uintptr_t kLoopGuardRva = 0x010C07E3;
constexpr std::uintptr_t kFourGroupCountsRva = 0x01568868;
constexpr std::uintptr_t kFourGroupPointersRva = 0x01568878;
constexpr SIZE_T kLoopGuardStolenLength = 7;
constexpr BYTE kLoopGuardBytes[7] = {0x8B,0x40,0x4C,0x89,0x44,0x24,0x10};
constexpr BYTE kLoopGuardSignature[34] = {
    0x8B,0x54,0x38,0x0C,0x83,0xC4,0x04,0x03,0xC7,0x8B,0x02,0x89,0x44,0x24,0x14,
    0x8B,0x40,0x4C,0x89,0x44,0x24,0x10,0x8D,0x7C,0x24,0x34,0xBB,0x09,0x00,0x00,0x00,
    0x8B,0xCF,0xE8
};
constexpr SIZE_T kLoopGuardTargetOffset = 15;

HMODULE g_plugin = nullptr;
BYTE* g_target = nullptr;
BYTE* g_trampoline = nullptr;
BYTE g_original[kStolenLength]{};
volatile LONG g_hookInstalled = 0;
volatile LONG g_league11Calls = 0;
volatile LONG g_processCallCount = 0;
BYTE* g_loopGuardContinuation = nullptr;
volatile LONG g_loopGuardSanitizedCount = 0;
LeagueConfig g_config{};
DWORD* g_extendedFourGroups[4]{};
SRWLOCK g_logLock = SRWLOCK_INIT;
wchar_t g_logPath[MAX_PATH]{};

using LookupFn = int(__stdcall*)(int);
LookupFn g_originalLookup = nullptr;

constexpr BYTE kLookupPattern[64] = {
    0x53,0x8B,0x5C,0x24,0x08,0x56,0x57,0x83,0xC8,0xFF,0x33,0xFF,0x8D,0x64,0x24,0x00,
    0x8B,0x34,0xBD,0x14,0x8C,0x96,0x01,0x33,0xC9,0x85,0xF6,0x7E,0x17,0x8B,0x14,0xBD,
    0x30,0x8C,0x96,0x01,0x3B,0x1A,0x74,0x0A,0x41,0x83,0xC2,0x04,0x3B,0xCE,0x7C,0xF4,
    0xEB,0x02,0x8B,0xC7,0x47,0x83,0xFF,0x07,0x7C,0xD6,0x5F,0x5E,0x5B,0xC2,0x04,0x00
};

constexpr BYTE kLookupMask[64] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,0,0,0,0,1,1,1,1,1,1,1,1,1,
    0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
};

bool IsReadableProtection(DWORD protection) {
    if ((protection & PAGE_GUARD) != 0 || (protection & PAGE_NOACCESS) != 0) return false;
    const DWORD base = protection & 0xFF;
    return base == PAGE_READONLY || base == PAGE_READWRITE || base == PAGE_WRITECOPY ||
           base == PAGE_EXECUTE || base == PAGE_EXECUTE_READ ||
           base == PAGE_EXECUTE_READWRITE || base == PAGE_EXECUTE_WRITECOPY;
}

void InitializeLogPath() {
    g_logPath[0] = L'\0';
    wchar_t tempPath[MAX_PATH]{};
    const DWORD length = GetTempPathW(_countof(tempPath), tempPath);
    if (length == 0 || length >= _countof(tempPath)) return;
    BuildLeagueRegistryLogPath(tempPath, g_logPath);
}

void Log(const wchar_t* format, ...) {
    if (g_logPath[0] == L'\0') return;
    wchar_t line[768]{};
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(line, _TRUNCATE, format, args);
    va_end(args);

    AcquireSRWLockExclusive(&g_logLock);
    HANDLE file = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t record[896]{};
        _snwprintf_s(record, _TRUNCATE, L"[%04u-%02u-%02u %02u:%02u:%02u] %s\r\n",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, line);
        char utf8[2048]{};
        const int count = WideCharToMultiByte(CP_UTF8, 0, record, -1, utf8,
                                               static_cast<int>(sizeof(utf8)), nullptr, nullptr);
        if (count > 1) {
            DWORD written = 0;
            WriteFile(file, utf8, static_cast<DWORD>(count - 1), &written, nullptr);
        }
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&g_logLock);
}

bool LoadConfiguration() {
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(g_plugin, path, MAX_PATH) == 0) return false;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash == nullptr) return false;
    *slash = L'\0';
    wchar_t configPath[MAX_PATH]{};
    _snwprintf_s(configPath, _TRUNCATE, L"%s\\FIFA12LeagueRegistry.ini", path);
    HANDLE file = CreateFileW(configPath, GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            Log(L"CONFIG=ABSENT NATIVE_BEHAVIOR=PRESERVED");
            return true;
        }
        Log(L"CONFIG=REJECTED REASON=OPEN_FAILED ERROR=%lu", GetLastError());
        return true; // fail safe: valid binary may run with no extensions
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart >= 65536) {
        CloseHandle(file);
        Log(L"CONFIG=REJECTED REASON=INVALID_SIZE");
        return true;
    }
    char buffer[65536]{};
    DWORD read = 0;
    const bool readOk = ReadFile(file, buffer, static_cast<DWORD>(size.QuadPart), &read, nullptr) != FALSE;
    CloseHandle(file);
    if (!readOk || read != static_cast<DWORD>(size.QuadPart)) {
        Log(L"CONFIG=REJECTED REASON=READ_FAILED");
        return true;
    }
    if (std::memchr(buffer,0,read)!=nullptr || (read>=3 && static_cast<unsigned char>(buffer[0])==0xEF &&
        static_cast<unsigned char>(buffer[1])==0xBB && static_cast<unsigned char>(buffer[2])==0xBF)) {
        Log(L"CONFIG=REJECTED REASON=NUL_OR_BOM NATIVE_BEHAVIOR=PRESERVED");return true;
    }
    LeagueConfig parsed{};
    std::size_t errorLine = 0;
    const char* reason = "";
    if (ParseLeagueConfig(buffer, parsed, errorLine, reason) != ConfigStatus::Ok) {
        wchar_t wideReason[64]{};
        MultiByteToWideChar(CP_UTF8, 0, reason, -1, wideReason, _countof(wideReason));
        Log(L"CONFIG=REJECTED LINE=%zu REASON=%s NATIVE_BEHAVIOR=PRESERVED", errorLine, wideReason);
        return true;
    }
    g_config = parsed;
    Log(L"CLASSIFIER_CONFIG=PASS ADDITIONS=%zu",g_config.classifierCount);
    Log(L"CONFIG=PASS MAPPINGS=%zu", g_config.count);
    for (std::size_t i = 0; i < g_config.count; ++i) {
        Log(L"CONFIG_MAPPING leagueid=%d group=%d", g_config.mappings[i].leagueId,
            g_config.mappings[i].group);
    }
    return true;
}

bool HashFileSha256(const wchar_t* path, wchar_t output[65], DWORD& fileSize) {
    fileSize = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > MAXDWORD) {
        CloseHandle(file);
        return false;
    }
    fileSize = static_cast<DWORD>(size.QuadPart);

    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool ok = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) != FALSE &&
              CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != FALSE;
    BYTE buffer[64 * 1024]{};
    while (ok) {
        DWORD read = 0;
        if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) { ok = false; break; }
        if (read == 0) break;
        if (!CryptHashData(hash, buffer, read, 0)) { ok = false; break; }
    }
    BYTE digest[32]{};
    DWORD digestSize = sizeof(digest);
    if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &digestSize, 0) != FALSE && digestSize == 32;
    if (hash != 0) CryptDestroyHash(hash);
    if (provider != 0) CryptReleaseContext(provider, 0);
    CloseHandle(file);
    if (!ok) return false;

    for (DWORD i = 0; i < 32; ++i) _snwprintf_s(output + i * 2, 65 - i * 2, _TRUNCATE, L"%02x", digest[i]);
    output[64] = L'\0';
    return true;
}

bool MatchPattern(const BYTE* address) {
    for (SIZE_T i = 0; i < sizeof(kLookupPattern); ++i) {
        if (kLookupMask[i] != 0 && address[i] != kLookupPattern[i]) return false;
    }
    return true;
}

bool GetExecutableSections(HMODULE module, BYTE*& imageBase, IMAGE_SECTION_HEADER*& first,
                           WORD& sectionCount, DWORD& sizeOfImage) {
    imageBase = reinterpret_cast<BYTE*>(module);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(imageBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;
    first = IMAGE_FIRST_SECTION(const_cast<IMAGE_NT_HEADERS32*>(nt));
    sectionCount = nt->FileHeader.NumberOfSections;
    sizeOfImage = nt->OptionalHeader.SizeOfImage;
    return sectionCount > 0 && sizeOfImage > kExpectedLookupRva + sizeof(kLookupPattern);
}

SIZE_T FindUniqueSignature(HMODULE module, BYTE*& match) {
    match = nullptr;
    BYTE* imageBase = nullptr;
    IMAGE_SECTION_HEADER* sections = nullptr;
    WORD sectionCount = 0;
    DWORD imageSize = 0;
    if (!GetExecutableSections(module, imageBase, sections, sectionCount, imageSize)) return 0;

    SIZE_T count = 0;
    for (WORD i = 0; i < sectionCount; ++i) {
        if ((sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        BYTE* begin = imageBase + sections[i].VirtualAddress;
        SIZE_T size = sections[i].Misc.VirtualSize;
        if (size < sizeof(kLookupPattern)) continue;
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(begin, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT ||
            !IsReadableProtection(mbi.Protect)) continue;
        for (SIZE_T j = 0; j + sizeof(kLookupPattern) <= size; ++j) {
            if (MatchPattern(begin + j)) {
                ++count;
                match = begin + j;
                if (count > 1) return count;
            }
        }
    }
    return count;
}

bool ValidateLookupStructure(HMODULE module, BYTE* target) {
    BYTE* base = reinterpret_cast<BYTE*>(module);
    if (target != base + kExpectedLookupRva || !MatchPattern(target)) return false;

    const std::uint32_t countsAddress = *reinterpret_cast<const std::uint32_t*>(target + 19);
    const std::uint32_t pointersAddress = *reinterpret_cast<const std::uint32_t*>(target + 32);
    if (countsAddress != reinterpret_cast<std::uintptr_t>(base + kCountsRva) ||
        pointersAddress != reinterpret_cast<std::uintptr_t>(base + kPointersRva)) return false;

    constexpr std::uint32_t expectedCounts[7] = {5, 2, 5, 12, 6, 1, 3};
    const auto counts = reinterpret_cast<const std::uint32_t*>(countsAddress);
    for (int i = 0; i < 7; ++i) if (counts[i] != expectedCounts[i]) return false;

    const auto pointers = reinterpret_cast<const std::uint32_t*>(pointersAddress);
    constexpr std::uint32_t expectedGroup4[6] = {7, 385, 386, 387, 388, 389};
    const auto group4 = reinterpret_cast<const std::uint32_t*>(pointers[4]);
    for (int i = 0; i < 6; ++i) if (group4[i] != expectedGroup4[i]) return false;
    return true;
}

bool FitsRel32(const BYTE* sourceAfterInstruction, const BYTE* destination) {
    const auto delta = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(destination)) -
                       static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(sourceAfterInstruction));
    return delta >= INT32_MIN && delta <= INT32_MAX;
}

void WriteRelativeJump(BYTE* source, const BYTE* destination) {
    source[0] = 0xE9;
    const auto wideDelta = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(destination)) -
                           static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(source + 5));
    const auto delta = static_cast<std::int32_t>(wideDelta);
    std::memcpy(source + 1, &delta, sizeof(delta));
}

struct HookEntrySnapshot {
    DWORD edi;
    DWORD esi;
    DWORD ebp;
    DWORD pushadEsp;
    DWORD ebx;
    DWORD edx;
    DWORD ecx;
    DWORD eax;
    DWORD eflags;
};

bool CallerRva(const void* caller, std::uintptr_t& rva) {
    HMODULE game = GetModuleHandleW(nullptr);
    if (game == nullptr) return false;
    BYTE* base = reinterpret_cast<BYTE*>(game);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const auto value = reinterpret_cast<std::uintptr_t>(caller);
    const auto imageBase = reinterpret_cast<std::uintptr_t>(base);
    if (value < imageBase || value >= imageBase + nt->OptionalHeader.SizeOfImage) return false;
    rva = value - imageBase;
    return true;
}

int __stdcall LookupWithDiagnostics(int leagueId, const HookEntrySnapshot* snapshot) {
    const LONG processCallCount = InterlockedIncrement(&g_processCallCount);
    LookupFn original = g_originalLookup;
    const int nativeResult = original != nullptr ? original(leagueId) : -1;
    int configuredGroup = -1;
    const bool hasMapping = FindConfiguredGroup(g_config, leagueId, configuredGroup);
    const bool overrideApplied = nativeResult == -1 && hasMapping;
    const int finalResult = overrideApplied ? configuredGroup : nativeResult;

    if (leagueId == 11) {
        const LONG count = InterlockedIncrement(&g_league11Calls);
        LARGE_INTEGER qpc{};
        QueryPerformanceCounter(&qpc);
        const DWORD entryEsp = snapshot->pushadEsp + sizeof(DWORD);
        const void* caller = *reinterpret_cast<void* const*>(entryEsp);
        std::uintptr_t callerRva = 0;
        const bool callerInFifa = CallerRva(caller, callerRva);
        if (hasMapping) {
            Log(L"DIAG process_call_count=%ld league11_call_count=%ld qpc=%lld thread_id=%lu leagueid=%d native_result=%d configured_group=%d final_result=%d override=%s return_address=0x%08IX caller_rva=%s0x%08IX esp_entry=0x%08lX ebp_entry=0x%08lX ecx_entry=0x%08lX eax_entry=0x%08lX edx_entry=0x%08lX",
                processCallCount, count, qpc.QuadPart, GetCurrentThreadId(), leagueId,
                nativeResult, configuredGroup, finalResult, overrideApplied ? L"YES" : L"NO",
                reinterpret_cast<std::uintptr_t>(caller), callerInFifa ? L"" : L"NONE/",
                callerInFifa ? callerRva : 0, entryEsp, snapshot->ebp, snapshot->ecx,
                snapshot->eax, snapshot->edx);
        } else {
            Log(L"DIAG process_call_count=%ld league11_call_count=%ld qpc=%lld thread_id=%lu leagueid=%d native_result=%d configured_group=NONE final_result=%d override=NO return_address=0x%08IX caller_rva=%s0x%08IX esp_entry=0x%08lX ebp_entry=0x%08lX ecx_entry=0x%08lX eax_entry=0x%08lX edx_entry=0x%08lX",
                processCallCount, count, qpc.QuadPart, GetCurrentThreadId(), leagueId,
                nativeResult, finalResult, reinterpret_cast<std::uintptr_t>(caller),
                callerInFifa ? L"" : L"NONE/", callerInFifa ? callerRva : 0,
                entryEsp, snapshot->ebp, snapshot->ecx, snapshot->eax, snapshot->edx);
        }
    }
    return finalResult;
}

__declspec(naked) void HookedLookupNativeLeagueGroup() {
    __asm {
        pushfd
        pushad
        mov eax, esp
        push eax
        mov ecx, dword ptr [esp + 44]
        push ecx
        call LookupWithDiagnostics
        mov dword ptr [esp + 28], eax
        popad
        popfd
        ret 4
    }
}

bool InstallHook(BYTE* target) {
    if (!FitsRel32(target + 5, reinterpret_cast<const BYTE*>(&HookedLookupNativeLeagueGroup))) return false;
    std::memcpy(g_original, target, kStolenLength);
    if (std::memcmp(g_original, kLookupPattern, kStolenLength) != 0) return false;

    g_trampoline = static_cast<BYTE*>(VirtualAlloc(nullptr, 10, MEM_COMMIT | MEM_RESERVE,
                                                   PAGE_EXECUTE_READWRITE));
    if (g_trampoline == nullptr || !FitsRel32(g_trampoline + 10, target + kStolenLength)) return false;
    std::memcpy(g_trampoline, g_original, kStolenLength);
    WriteRelativeJump(g_trampoline + kStolenLength, target + kStolenLength);
    FlushInstructionCache(GetCurrentProcess(), g_trampoline, 10);
    g_originalLookup = reinterpret_cast<LookupFn>(g_trampoline);

    BYTE patch[5]{};
    patch[0] = 0xE9;
    const auto hookWideDelta =
        static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(&HookedLookupNativeLeagueGroup)) -
        static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(target + 5));
    const auto hookDelta = static_cast<std::int32_t>(hookWideDelta);
    std::memcpy(patch + 1, &hookDelta, sizeof(hookDelta));
    DWORD oldProtection = 0;
    if (!VirtualProtect(target, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtection)) return false;
    std::memcpy(target, patch, sizeof(patch));
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(patch));
    DWORD ignored = 0;
    if (!VirtualProtect(target, sizeof(patch), oldProtection, &ignored)) {
        Log(L"Hook installed, warning: failed to restore original page protection error=%lu", GetLastError());
    }
    g_target = target;
    InterlockedExchange(&g_hookInstalled, 1);
    return true;
}

SIZE_T FindUniqueLoopGuardSignature(HMODULE module, BYTE*& target) {
    target = nullptr;
    BYTE* imageBase = nullptr;
    IMAGE_SECTION_HEADER* sections = nullptr;
    WORD sectionCount = 0;
    DWORD imageSize = 0;
    if (!GetExecutableSections(module, imageBase, sections, sectionCount, imageSize)) return 0;
    SIZE_T count = 0;
    for (WORD i = 0; i < sectionCount; ++i) {
        if ((sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        BYTE* begin = imageBase + sections[i].VirtualAddress;
        const SIZE_T size = sections[i].Misc.VirtualSize;
        if (size < sizeof(kLoopGuardSignature)) continue;
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(begin, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT ||
            !IsReadableProtection(mbi.Protect)) continue;
        for (SIZE_T j = 0; j + sizeof(kLoopGuardSignature) <= size; ++j) {
            if (std::memcmp(begin + j, kLoopGuardSignature, sizeof(kLoopGuardSignature)) == 0) {
                target = begin + j + kLoopGuardTargetOffset;
                if (++count > 1) return count;
            }
        }
    }
    return count;
}

void __stdcall LogLoopGuardSanitization(LONG originalLimit) {
    const LONG count = InterlockedIncrement(&g_loopGuardSanitizedCount);
    Log(L"LOOP_GUARD sanitized_count=%ld original_limit=%ld sanitized_limit=0 thread_id=%lu",
        count, originalLimit, GetCurrentThreadId());
}

extern "C" __declspec(naked) void CareerLoopGuardStub() {
    __asm {
        mov eax, dword ptr [eax + 4Ch]
        pushfd
        cmp eax, 0
        jl invalid_limit
        cmp eax, 10
        jg invalid_limit
        mov dword ptr [esp + 14h], eax
        popfd
        jmp dword ptr [g_loopGuardContinuation]
    invalid_limit:
        mov dword ptr [esp + 14h], 0
        pushad
        push eax
        call LogLoopGuardSanitization
        popad
        popfd
        jmp dword ptr [g_loopGuardContinuation]
    }
}

bool InstallCareerLoopGuard(BYTE* target) {
    if (std::memcmp(target, kLoopGuardBytes, sizeof(kLoopGuardBytes)) != 0 ||
        !FitsRel32(target + 5, reinterpret_cast<const BYTE*>(&CareerLoopGuardStub))) return false;
    BYTE patch[kLoopGuardStolenLength] = {0xE9,0,0,0,0,0x90,0x90};
    const auto wideDelta = static_cast<std::int64_t>(
        reinterpret_cast<std::uintptr_t>(&CareerLoopGuardStub)) -
        static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(target + 5));
    const auto delta = static_cast<std::int32_t>(wideDelta);
    std::memcpy(patch + 1, &delta, sizeof(delta));
    g_loopGuardContinuation = target + kLoopGuardStolenLength;
    DWORD oldProtection = 0;
    if (!VirtualProtect(target, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtection)) return false;
    std::memcpy(target, patch, sizeof(patch));
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(patch));
    DWORD ignored = 0;
    VirtualProtect(target, sizeof(patch), oldProtection, &ignored);
    return true;
}

bool InstallConfiguredFourGroupExtension(HMODULE game) {
    BYTE* base = reinterpret_cast<BYTE*>(game);
    auto counts = reinterpret_cast<DWORD*>(base + kFourGroupCountsRva);
    auto pointers = reinterpret_cast<DWORD**>(base + kFourGroupPointersRva);
    for (int g=0;g<4;++g) {
        if (counts[g]!=NativeCounts[g] || !pointers[g] ||
            std::memcmp(pointers[g],NativeGroups[g],NativeCounts[g]*sizeof(DWORD))!=0) return false;
    }
    const auto layout=BuildClassifierLayout(g_config);
    auto releaseUnpublished=[]() {
        for (int g=0;g<4;++g) if(g_extendedFourGroups[g]) {
            VirtualFree(g_extendedFourGroups[g],0,MEM_RELEASE);g_extendedFourGroups[g]=nullptr;
        }
    };
    bool changed=false;
    for(int g=0;g<4;++g) if(layout.counts[g]>NativeCounts[g]) {
        changed=true;
        const SIZE_T bytes=layout.counts[g]*sizeof(DWORD);
        auto copy=static_cast<DWORD*>(VirtualAlloc(nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        if(!copy){releaseUnpublished();return false;}
        g_extendedFourGroups[g]=copy;
        std::memcpy(copy,layout.rows[g],bytes);
        DWORD old=0;
        if(!VirtualProtect(copy,bytes,PAGE_READONLY,&old)){releaseUnpublished();return false;}
    }
    if(!changed) return true;
    DWORD oldProtection=0;
    constexpr SIZE_T span=sizeof(DWORD)*8;
    if(!VirtualProtect(counts,span,PAGE_READWRITE,&oldProtection)){releaseUnpublished();return false;}
    // Superset pointer first, count second. Native prefixes support either count.
    // Published storage lives until process exit; never free on detach/rollback.
    for(int g=0;g<4;++g) if(g_extendedFourGroups[g]) {
        pointers[g]=g_extendedFourGroups[g];MemoryBarrier();counts[g]=static_cast<DWORD>(layout.counts[g]);
        Log(L"CLASSIFIER_GROUP=%d NATIVE_COUNT=%zu RUNTIME_COUNT=%zu",g,NativeCounts[g],layout.counts[g]);
    }
    DWORD ignored=0;
    if(!VirtualProtect(counts,span,oldProtection,&ignored)) Log(L"CLASSIFIER_PROTECTION_RESTORE=FAILED DATA_VALID=YES");
    return true;
}

DWORD WINAPI InitializePlugin(void*) {
    InitializeLogPath();
    Log(L"%s", kPluginVersion);

    HMODULE game = GetModuleHandleW(nullptr);
    wchar_t gamePath[MAX_PATH]{};
    if (game == nullptr || GetModuleFileNameW(game, gamePath, MAX_PATH) == 0) {
        Log(L"STATUS=ABORTED REASON=MAIN_MODULE_NOT_FOUND");
        return 0;
    }
    const wchar_t* baseName = wcsrchr(gamePath, L'\\');
    baseName = baseName != nullptr ? baseName + 1 : gamePath;
    if (_wcsicmp(baseName, L"fifa.exe") != 0 && _wcsicmp(baseName, L"fifa12.exe") != 0) {
        Log(L"STATUS=ABORTED REASON=UNEXPECTED_PROCESS NAME=%s", baseName);
        return 0;
    }

    wchar_t hash[65]{};
    DWORD fileSize = 0;
    if (!HashFileSha256(gamePath, hash, fileSize)) {
        Log(L"STATUS=ABORTED REASON=SHA256_FAILED ERROR=%lu", GetLastError());
        return 0;
    }
    if (fileSize != kExpectedFileSize || _wcsicmp(hash, kExpectedSha256) != 0) {
        Log(L"STATUS=ABORTED REASON=BUILD_MISMATCH SIZE=%lu SHA256=%s", fileSize, hash);
        return 0;
    }
    Log(L"Game: FIFA12 1.7.0.0 SIZE=%lu SHA256=MATCH", fileSize);

    LoadConfiguration();

    if (!InstallConfiguredFourGroupExtension(game)) {
        Log(L"FOUR_GROUP_CLASSIFIER=NOT_INSTALLED REASON=VALIDATION_OR_PATCH_FAILURE");
        return 0;
    }
    Log(L"FOUR_GROUP_CLASSIFIER=CONFIGURED NATIVE_PREFIXES_PRESERVED=YES DATA_ONLY=YES");

    BYTE* target = nullptr;
    SIZE_T matches = 0;
    for (DWORD attempt = 1; attempt <= kRetryCount; ++attempt) {
        matches = FindUniqueSignature(game, target);
        if (matches == 1) break;
        if (matches > 1) {
            Log(L"STATUS=ABORTED REASON=SIGNATURE_AMBIGUOUS MATCHES=%zu", matches);
            return 0;
        }
        Sleep(kRetryDelayMs);
    }
    if (matches != 1 || target == nullptr) {
        Log(L"STATUS=ABORTED REASON=SIGNATURE_TIMEOUT MATCHES=0");
        return 0;
    }
    const auto resolvedRva = reinterpret_cast<std::uintptr_t>(target) -
                             reinterpret_cast<std::uintptr_t>(game);
    Log(L"Signature: UNIQUE Target RVA=0x%08IX", resolvedRva);
    if (!ValidateLookupStructure(game, target)) {
        Log(L"STATUS=ABORTED REASON=LOOKUP_STRUCTURE_MISMATCH RVA=0x%08IX", resolvedRva);
        return 0;
    }
    if (!InstallHook(target)) {
        Log(L"STATUS=ABORTED REASON=HOOK_INSTALL_FAILED ERROR=%lu", GetLastError());
        return 0;
    }
    Log(L"REGISTRY_HOOK=INSTALLED RVA=0x%08IX", resolvedRva);

    BYTE* loopTarget = nullptr;
    SIZE_T loopMatches = 0;
    for (DWORD attempt = 1; attempt <= kRetryCount; ++attempt) {
        loopMatches = FindUniqueLoopGuardSignature(game, loopTarget);
        if (loopMatches != 0) break;
        Sleep(kRetryDelayMs);
    }
    const auto expectedLoopTarget = reinterpret_cast<BYTE*>(game) + kLoopGuardRva;
    if (loopMatches != 1 || loopTarget != expectedLoopTarget ||
        !InstallCareerLoopGuard(loopTarget)) {
        Log(L"CAREER_LOOP_GUARD=NOT_INSTALLED matches=%zu target_rva=%s", loopMatches,
            loopTarget == nullptr ? L"NONE" : L"MISMATCH_OR_PATCH_FAILURE");
        return 0;
    }
    Log(L"BUILD_VALIDATION=PASS");
    Log(L"CAREER_LOOP_GUARD=INSTALLED RVA=0x%08IX", kLoopGuardRva);
    return 0;
}

} // namespace

extern "C" __declspec(dllexport) const wchar_t* __stdcall FIFA12LeagueRegistryVersion() {
    return kPluginVersion;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_plugin = instance;
        DisableThreadLibraryCalls(instance);
        HANDLE worker = CreateThread(nullptr, 0, InitializePlugin, nullptr, 0, nullptr);
        if (worker != nullptr) CloseHandle(worker);
    }
    return TRUE;
}
