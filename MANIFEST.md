# Publication manifest

Included:
- source/FIFA12LeagueRegistry.cpp: complete approved plugin implementation.
- source/LeagueConfig.h, NativeClassifiers.h, LogPath.h: required project-local headers.
- source/FIFA12LeagueRegistry.vcxproj: original Release Win32 project.
- source/FixtureTests.cpp and FixtureTests.vcxproj: source-only fixture suite; unique project GUID for solution coexistence.
- FIFA12-LeagueRegistry.sln: standalone solution for both projects.
- release/: exact validated ASI and matching INI, SHA-256 checked against operational baseline.
- README.md, CHANGELOG.md, .gitignore, SHA256SUMS.txt and this manifest.

Intentionally excluded:
- All EA/FIFA executables, DB/LOC/Compdata, saves, graphics and game-root copies.
- ASI loaders, old plugin binaries and unrelated Tournament Editor code/research.
- obj, .vs, PDB, caches, logs, temporary files, fixture executables and generated build outputs.
- Redundant ZIPs, full snapshots, user-specific absolute paths and historical bulk artifacts.
- LICENSE: no redistribution/license grant was assumed; owner review remains necessary.

Validation: all copied implementation/header bytes and original plugin project match the source authority; shipped ASI and INI match the runtime-approved hashes. No live writes or FIFA launch. Packaged source was not recompiled in this packaging round.
