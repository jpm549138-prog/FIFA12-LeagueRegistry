# FIFA12 LeagueRegistry

Generic LeagueRegistry v0.6 is the project's runtime-validated operational baseline.
The distributed binary retains its historical `_Candidate` filename; it is the exact approved binary, not a rebuild.

## Implemented behavior

- Native-first seven-group LeagueRegistry lookup fallback: preserve a valid native result; use the configured fallback only for a native -1 result.
- Four-group classifier registration: native lists remain authoritative; append explicitly configured custom IDs using process-lifetime owned arrays and updated counts/pointers.
- Career Loop Guard: preserve signed local counts 0..10; replace an invalid local count with zero. The original object field at +0x4C is untouched.

These are separate mechanisms. A seven-group fallback value is not a four-group classifier index.
The plugin patches process memory only, not fifa.exe on disk. It does not create leagues, DB records, Compdata, localization, teams, kits or assets.

## Supported build and runtime evidence

Windows Win32/x86, validated FIFA 12 executable build 1.7.0.0:
- file size: 23,449,504 bytes
- SHA-256: `42fd471f7437849b936db605cdd51e78ebdadf536c6653c87870e0127e532f70`

The plugin checks executable integrity and hook structure. Other executables/builds are not supported by evidence here.
Existing hooks: LeagueRegistry RVA 0x010A6220; Career Loop Guard RVA 0x010C07E3.
Four-group count/pointer tables: RVA 0x01568868 / 0x01568878.

User-confirmed manual runtime regression: normal boot, Chile ID2 and Uruguay ID11 registration and match loading, Career start, second Career in the same session, switching between those leagues without a crash, and Chile ID2 transition through a full season into season two.
This does not prove arbitrary new IDs, Uruguay season transition, every native Career scenario, or the unique semantic meaning of each classifier group.

## Installation

1. Close FIFA. Back up your current plugin and matching INI.
2. An ASI loader is required and is not included. Use your established working loader; loader-specific paths are not universal.
3. In the validated setup, place the two files from `release/` together in `Game\plugins\`.
4. Ensure only one LeagueRegistry ASI is loaded. Remove the previous version from the loader's search path after backing it up; do not load both.
5. Keep the supplied INI paired with this ASI. No DB, Compdata, LOC, saves, kits, or assets are installed by this package.

Removal/rollback: close FIFA and restore the previous ASI AND INI pair. Existing Chile RC1 remains a historical rollback/reference authority but is not bundled.
Publication preparation did not install anything or launch FIFA.

## Configuration

The matching INI includes:

```ini
[LeagueCompatibility]
11=4
2=4

[ClassifierGroup3]
11=1
2=1
```

Fallback values are 0..6. Classifier sections are `ClassifierGroup0` through `ClassifierGroup3`; `ID=1` declares membership, not a fallback value.
The shipped configuration reproduces native Group3 plus 11 and 2 in that order.
Do not infer a universal country-to-group rule. New memberships require explicit technical policy and separate runtime validation.

Native entries cannot be reassigned. Same-native-group declarations are no-ops.
Duplicate sections/keys, conflicting memberships, unknown sections, invalid values, BOM/NUL, capacity overflow or prohibited IDs reject the entire configuration.
Absent/invalid configuration preserves native-only registration; custom IDs 11/2 are then NOT implicitly registered.
Known prohibited additions: 382..389, 2000, 2028, nonpositive IDs and IDs above 65535. Native reserved entries remain intact; native membership is not permission to create a league using a reserved ID.
DB/Compdata collisions must be checked separately.

Diagnostics are written to `%TEMP%\FIFA12LeagueRegistry.log`. Review locally before sharing logs.

## Build and fixture tests

Install Visual Studio C++ desktop development, MSVC v143 and Windows 10 SDK.
Open `FIFA12-LeagueRegistry.sln`, select **Release | Win32**, and build.
From a Visual Studio Developer Command Prompt:

```bat
MSBuild FIFA12-LeagueRegistry.sln /p:Configuration=Release /p:Platform=Win32
build\PluginFixtureTests.exe
build\PluginFixtureTests.exe release\FIFA12LeagueRegistry.ini
```

C++17, static CRT, /W4 and /WX. Only Windows SDK/system libraries are required (including advapi32); all project-local headers are supplied.
The source uses x86-specific hooks/assembly: do not build x64.
Build output goes to `build/`, separate from the preserved validated `release/` binary.
Fixture tests cover configuration/layout; they do not launch FIFA or replace runtime validation.
A rebuild is not claimed to reproduce the original PE hash across toolchains.

## Preservation and publication notes

All .cpp/.h files and the plugin .vcxproj are byte-identical to the approved source authority.
Packaging adds this solution and changes only the fixture project's duplicate ProjectGuid to permit both projects in one solution. No symbols or behavior were changed.
See `SHA256SUMS.txt` and `MANIFEST.md` for integrity and inclusions.

No EA executable, game data, assets, loader or third-party binaries are supplied.
No license was invented: redistribution rights for the inherited project source have not been established by this packaging task. The owner should confirm rights and choose an appropriate license before public publication. This repository does not assert an open-source license grant.

## Not implemented / not established

No general Career compatibility guarantee for arbitrary league IDs; no automatic classifier-group semantics; no new -1 guard; no generalized upstream repair of object+0x4C; no installer or automatic game-file edits. Tournament Editor integration is outside this standalone plugin package.
