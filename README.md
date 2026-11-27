# redpill

A ring-3 x64 capability probe that answers one question before any driver work:
can this machine do VMXON at all. It reads CPUID leaves, the hypervisor-present
bit, the hypervisor vendor leaf, and the IDT/GDT/LDT/TR descriptor registers via
a small ml64 object — then states what it cannot tell you.

status: done. built and run on this machine; `example/` is one live run with
line endings normalized to LF. no kernel code, no MSRs, no driver.

---

## Why this exists

A kernel anti-cheat or hypervisor project starts with an assumption: that VT-x is
available to you. With VBS or Hyper-V enabled, Hyper-V owns VT-x and a normal
guest is not given nested virtualization. Building a driver that calls `__vmx_on`
on such a machine wastes a week on a failure that a small CPUID check predicts.

`redpill` is that check. It is deliberately user-mode: it needs no signing, no
test mode, no reboot, and it cannot destabilise the machine.

---

## Repository layout

```
redpill/
├── src/
│   ├── redpill.cpp        probe, report, json output, selftest
│   └── desc.asm           sidt / sgdt / sldt / str, emitted as opcode bytes
├── example/
│   ├── out.txt            captured text output from this machine
│   ├── out.json           captured json output
│   └── selftest.txt       captured selftest output
├── build.bat              ml64 → cl → selftest → smoke → json
├── notes.txt              notes from the cpuid runs
├── LICENSE                Unlicense
└── README.md
```

**How the pieces fit.** `redpill.cpp` is the whole program: 213 lines, one
`Probe` struct, one `collect()`, two reporters. `desc.asm` exists only because
MSVC x64 has no inline assembler and ml64 is awkward about the 10-byte `m16&64`
forms — emitting the opcode bytes directly is shorter and makes the encoding
explicit. The object is linked straight into the exe; there is no DLL.

**Layer discipline.** `collect()` is the only function that touches hardware.
Everything above it is pure formatting, which is why the selftest can verify the
vendor-string ordering and struct layout without any particular CPU being
present.

---

## Build from source

Requires **MSVC 2022 x64** (`cl`, `ml64`). No CMake, no WDK, no libraries beyond
the CRT. Run from any prompt — `build.bat` finds `vcvars64.bat` itself if `cl` is
not already on `PATH`.

```
git clone https://github.com/vmgexit/redpill.git
cd redpill
build.bat
```

Output: `build\redpill.exe`. The build has five failure-checked stages:
`ml64` for the descriptor object, `cl` for the exe, then `selftest`, `smoke`
and `json` runs of the binary. Abridged output:

```
[*] selftest
  ok    Dtr is 10 bytes
  ...
  PASS
[*] smoke
redpill 0.3  |  x64 ring-3 capability probe
  ...
[*] json
{
  "cpu_vendor": "GenuineIntel",
  ...
}
[+] build\redpill.exe
```

Flags: `ml64 /nologo /c /Fobuild\desc.obj src\desc.asm`, then
`cl /nologo /std:c++17 /EHsc /O2 /W3 /MT /DUNICODE /D_UNICODE
/Fe:build\redpill.exe src\redpill.cpp build\desc.obj`. `/MT` keeps the exe
self-contained; CRT only, no WDK and no extra libraries.

---

## What it reads

| source | field | meaning |
| --- | --- | --- |
| CPUID 0 | `cpu_vendor`, `max_leaf` | 12-byte vendor string, highest basic leaf |
| CPUID 1 ECX | `vmx`, `smx`, `hypervisor_present` | bits 5, 6, 31 |
| CPUID 7 (only when `max_leaf >= 7`) | `leaf7_ebx`, `xsaves` | feature bits, ECX bit 3 |
| CPUID `0x80000000` | `max_ext_leaf` | highest extended leaf |
| CPUID `0x80000001` ECX | `svm` | AMD-V, bit 2 |
| CPUID `0x40000000` | `hv_vendor`, `max_hv_leaf` | 12-byte hypervisor vendor string |
| CPUID `0x40000001` EAX | `hv_signature` | Hyper-V interface ID (`Hv#1` = `0x31237648`) |
| `sidt` / `sgdt` | `idt_base/limit`, `gdt_base/limit` | descriptor table registers |
| `sldt` / `str` | `ldt`, `tr` | selector values |

Two ordering rules that are easy to get wrong:

- CPUID 0 returns the vendor in **EBX / EDX / ECX** order. `GenuineIntel` is
  `EBX="Genu"`, `EDX="ineI"`, `ECX="ntel"`.
- CPUID `0x40000000` returns the hypervisor vendor in **EBX / ECX / EDX**
  order. `Microsoft Hv` is `EBX="Micr"`, `ECX="osof"`, `EDX="t Hv"`.

Both are asserted in `--selftest` with synthetic register values, so a swapped
pair fails the build.

Hypervisor vendor strings are matched exactly, not by prefix:
`Microsoft Hv` (Hyper-V / VBS), `VMwareVMware`, `VBoxVBoxVBox`, `KVMKVMKVM`,
`XenVMMXenVMM`, `TCGTCGTCGTCG` (QEMU TCG), `bhyve bhyve`. Anything else prints
as `unknown`.

---

## Reading the output

```
redpill 0.3  |  x64 ring-3 capability probe
------------------------------------------------------------
cpu vendor      GenuineIntel   max leaf 0x16  max ext 0x80000008
leaf 1 ecx      VMX no   SMX no   HV-present yes
ext 80000001    SVM (amd-v) no
leaf 7 ebx      0x009C6FBD  XSAVES no
hv vendor       Microsoft Hv  (Hyper-V / VBS)
hv max leaf     0x4000000C  signature 0x31237648

idt             base 0xFFFFF80000001000  limit 0x0FFF
gdt             base 0xFFFFF8534B590000  limit 0x007F
ldt             0x0000  tr 0x0040
```

`--json` emits the same fields as a flat object, with addresses as hex strings:

```json
{
  "cpu_vendor": "GenuineIntel",
  "max_leaf": 22,
  "max_ext_leaf": 2147483656,
  "vmx": false,
  "smx": false,
  "svm": false,
  "leaf7_ebx": "0x009C6FBD",
  "xsaves": false,
  "hypervisor_present": true,
  "hv_vendor": "Microsoft Hv",
  "hv_max_leaf": 1073741836,
  "hv_signature": "0x31237648",
  "idt_base": "0xFFFFF80000001000",
  "idt_limit": 4095,
  "gdt_base": "0xFFFFF8534B590000",
  "gdt_limit": 127,
  "ldt": 0,
  "tr": 64
}
```

Captured output in [`example/out.txt`](example/out.txt),
[`example/out.json`](example/out.json) and
[`example/selftest.txt`](example/selftest.txt). Files on disk use LF line
endings; the live Windows binary emits CRLF.

---

## The descriptor registers

`sidt` and `sgdt` store a 2-byte limit followed by an 8-byte base in 64-bit mode
— 10 bytes total. The C struct must be packed:

```cpp
#pragma pack(push, 1)
struct Dtr {
    uint16_t limit;
    uint64_t base;
};
#pragma pack(pop)
```

Without the pragma, `base` lands at offset 8 and every printed IDT/GDT base is
garbage. `sizeof(Dtr) == 10` and `offsetof(Dtr, base) == 2` are asserted in the
selftest.

The asm helpers emit raw bytes because ml64 rejects the `fword ptr` forms for
these instructions:

```
; sidt m16&64 : 0F 01 /1, modrm 00 001 001 -> [rcx]
get_idt PROC
    db 00Fh, 001h, 009h
    ret
get_idt ENDP

; sgdt m16&64 : 0F 01 /0, modrm 00 000 001 -> [rcx]
get_gdt PROC
    db 00Fh, 001h, 001h
    ret
get_gdt ENDP

; sldt ax : 0F 00 /0, modrm 11 000 000
get_ldt PROC
    db 00Fh, 000h, 0C0h
    ret
get_ldt ENDP

; str ax : 0F 00 /1, modrm 11 001 000
get_tr PROC
    db 00Fh, 000h, 0C8h
    ret
get_tr ENDP
```

`sldt` and `str` are unprivileged. They return **selectors**, not descriptor
contents — reading the actual segment descriptor would need the GDT base plus the
selector index, which would require kernel memory access.

---

## The verdict

The `verdict` block is a decision table, not a hardware report:

| VMX | HV-present | conclusion |
| --- | --- | --- |
| yes | no | VMX exposed, no outer hypervisor reported; VMXON not tested here |
| yes | yes | VMX bit set but an outer hypervisor owns it; needs nested virtualization |
| no | yes | VMX masked by the outer hypervisor (typical for Hyper-V without nesting) |
| no | no | check BIOS enablement, SMX, and the feature-control MSR |

On the machine this was built on, the third row applies:

```
verdict
  already inside Hyper-V / VBS
  vmx masked by the outer hv (typical for hyper-v without nesting)
```

That result is why a ring-0 `__vmx_on` is blocked on this boot configuration.
It is a statement about the boot configuration, not the CPU: VMX exposure depends
on settings this tool does not change.

Do not read `VMX no` as "the CPU lacks VT-x". Read it as "the outer hypervisor
did not expose VT-x to this partition".

---

## What ring 3 cannot tell you

The text report names `IA32_FEATURE_CONTROL`, `IA32_VMX_BASIC` and `CR4.VMXE`
directly. The full list of what ring 3 cannot determine:

- whether `IA32_FEATURE_CONTROL` (0x3A) is locked
- whether VMXON is allowed outside SMX
- the VMXON/VMCS region size from `IA32_VMX_BASIC` (0x480)
- the allowed-1 control MSRs (0x481–0x491)
- `CR4.VMXE`, and the control registers generally

All of those need ring 0. That is the boundary between this repo and a driver;
nothing here tries to cross it.

---

## Why the old redpill check is dead

The name refers to the classic `SIDT` trick: on 32-bit Windows, a VMware guest's
IDT base sat above `0xD0000000` while a native one did not, so a single
unprivileged instruction could detect virtualization.

On x64 Windows the check is meaningless. Kernel addresses occupy the upper half
of the canonical range on every system, hypervisor or not — the IDT base here is
`0xFFFFF80000001000`, which would have "detected" a VM on bare metal too. The
tool still prints the IDT/GDT values as raw register state. It does not use
them to claim anything about virtualization.

---

## Verification

`redpill --selftest` runs eleven assertions with no hardware dependency:

```
  ok    Dtr is 10 bytes
  ok    Dtr.base at offset 2
  ok    cpu vendor string order is EBX/EDX/ECX
  ok    hypervisor vendor string order is EBX/ECX/EDX
  ok    Hyper-V vendor name
  ok    VMware vendor name
  ok    KVM vendor name
  ok    vendor names match exactly
  ok    unknown vendor name
  ok    json escapes quote and backslash
  ok    Hyper-V interface signature decodes to Hv#1
  PASS
```

The remaining output is a live probe and therefore not asserted — the CPUID and
descriptor values differ per machine and per boot configuration, so
`example/out.txt` is one specific run from this box with line endings
normalized to LF. The probe printed all fields and exited 0; no driver, no
reboot, no firmware access. `--help` prints usage and unknown flags exit 2.

---

## License

Unlicense — public domain. Do what you like with it; no strings attached.
