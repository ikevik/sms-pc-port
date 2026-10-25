# uwp-probe

A small UWP app that checks whether the sub-4 GiB layout the port needs
(see `docs/64-BIT.md`) is possible inside the UWP sandbox on Xbox Dev Mode.

It reports, in a dialog and in `LocalState\probe.log`:

| Check | Why the port needs it |
| --- | --- |
| image base below 4 GiB | non-PIE code and globals whose addresses sit in 32-bit slots |
| MEM1, 64 MiB at `0x80000000` | `map_mem1()` in `platform/port_runtime.cpp` |
| 128 MiB pool in `0x10000000..0x80000000` | host thread stacks (`port_low_alloc`) |
| 64 KiB at `0xCC000000` | GX write-gather sink |
| code running on a stack from that pool, on the UI thread and a worker | game threads run on low stacks |

It also lists the free low address space and the app memory limit.
It does not exercise the TEB stack-bounds fix-up the Windows port does.

Two variants are built. The port's Windows build links at `0x400000` with ASLR off, but UWP's
`/APPCONTAINER` rejects `/DYNAMICBASE:NO` (LNK1295), so that is not possible here. `low` links with
`/BASE:0x400000 /HIGHENTROPYVA:NO`, which should load the image below 4 GiB at a random address
(save states and the lockstep tracer assume fixed addresses, so that would need handling). `default` is
the ordinary high-entropy image, as a control.

## Building

Needs Windows with Visual Studio 2022 (v143, UWP workload and Windows SDK). From the repo root,
the `azure-pipelines.yml` job does this and publishes `uwp-probe-low` and `uwp-probe-default`
artifacts (`.msix` plus `SmsDev.cer`). Locally:

```powershell
cmake -S tools/uwp-probe -B build-probe -G "Visual Studio 17 2022" -A x64 -T v143 `
  -DCMAKE_TOOLCHAIN_FILE="$PWD/tools/uwp-probe/toolchain.cmake" -DPROBE_LOW_BASE=ON
cmake --build build-probe --config Release
```

`build-probe/layout/` is the loose package; pack and sign it with `makeappx` and `signtool` (see the pipeline).

## Running on the console

Install `Microsoft.VCLibs.x64.14.00.appx` (in the artifact) first if the console doesn't have it, then
install through the Xbox Device Portal. Each CI run signs with a fresh certificate, so uninstall the
previous probe first. If the console rejects the package, upload `SmsDev.cer` as well.
Read the dialog, or open the app's `LocalState\probe.log` from the Device Portal file explorer.

Unverified: the manifest's `EntryPoint`, MASM support under the UWP VS generator, and whether the console
accepts the self-signed package. Fix those first if the build or install fails.
