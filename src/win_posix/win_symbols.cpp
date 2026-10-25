// Absolute symbols the GNU link line defines with --defsym, which lld-link has no option
// for (msvc-target build). Absolute symbols are 32-bit pc-relative reachable from the low
// image, so the image must stay below 2 GiB (CMake links it at /BASE:0x400000).

// dsptask.c points a DSP task at the fixed low-memory address 0x80004600. It declares
// `extern u8 lbl_80004600[]` as C++, so the MSVC ABI wants the decorated name; the plain
// one is for C users.
__asm__(
	".globl lbl_80004600\n.set lbl_80004600, 0x80004600\n"
	".globl \"?lbl_80004600@@3PAEA\"\n.set \"?lbl_80004600@@3PAEA\", 0x80004600\n");
