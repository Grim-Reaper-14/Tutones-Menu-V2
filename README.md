# Tutones Menu V2

Clean-room V2 DLL base for GTA V Enhanced development.

## Design goals

- Completely separate from Tutones Menu V1.
- Lightweight startup and shutdown lifecycle.
- Render path remains UI/render-only.
- Backend/game work is isolated from the DX12 hot path.
- Services have explicit initialize/shutdown ownership.
- New systems are added incrementally and verified before wiring them into the menu.

## Build

From a Visual Studio Developer PowerShell:

```powershell
cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Output:

`build\\Release\\Tutones-Menu-V2.dll`
