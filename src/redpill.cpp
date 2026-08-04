#include <Windows.h>
#include <intrin.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string>

#pragma pack(push, 1)
struct Dtr {
    uint16_t limit;
    uint64_t base;
};
#pragma pack(pop)

extern "C" void get_idt(void*);
extern "C" void get_gdt(void*);
extern "C" uint16_t get_ldt(void);
extern "C" uint16_t get_tr(void);

static void cpuid_at(int leaf, int sub, int* r) {
    __cpuidex(r, leaf, sub);
}

static std::string regs_to_str(int b, int c, int d) {
    std::string s;
    auto put = [&](int v) {
        for (int i = 0; i < 4; i++) s.push_back((char)((v >> (8 * i)) & 0xff));
    };
    put(b); put(c); put(d);
    return s;
}

struct Probe {
    std::string cpu_vendor;
    uint32_t max_leaf = 0;
    uint32_t max_ext_leaf = 0;
    uint32_t max_hv_leaf = 0;
    std::string hv_vendor;
    bool vmx = false;
    bool smx = false;
    bool hypervisor_present = false;
    bool svm = false;
    bool xsaves = false;
    uint16_t idt_limit = 0;
    uint64_t idt_base = 0;
    uint16_t gdt_limit = 0;
    uint64_t gdt_base = 0;
    uint16_t ldt = 0;
    uint16_t tr = 0;
    uint32_t leaf7_ebx = 0;
    uint32_t hv_signature = 0;
};

static Probe collect() {
    Probe p;
    int r[4] = {};

    cpuid_at(0, 0, r);
    p.max_leaf = (uint32_t)r[0];
    p.cpu_vendor = regs_to_str(r[1], r[3], r[2]);

    cpuid_at(1, 0, r);
    /* revisit */
    p.vmx = (r[2] & (1 << 5)) != 0;
    p.smx = (r[2] & (1 << 6)) != 0;
    p.hypervisor_present = (r[2] & (1 << 31)) != 0;

    cpuid_at(7, 0, r);
    p.leaf7_ebx = (uint32_t)r[1];
    p.xsaves = (r[2] & (1 << 3)) != 0;

    cpuid_at(0x80000000, 0, r);
    p.max_ext_leaf = (uint32_t)r[0];
    if (p.max_ext_leaf >= 0x80000001) {
        cpuid_at(0x80000001, 0, r);
        p.svm = (r[2] & (1 << 2)) != 0;
    }

    if (p.hypervisor_present) {
        cpuid_at(0x40000000, 0, r);
        p.max_hv_leaf = (uint32_t)r[0];
        p.hv_vendor = regs_to_str(r[1], r[2], r[3]);
        if (p.max_hv_leaf >= 0x40000001) {
            cpuid_at(0x40000001, 0, r);
            p.hv_signature = (uint32_t)r[0];
        }
    }

    Dtr idt{}, gdt{};
    get_idt(&idt);
    get_gdt(&gdt);
    p.idt_limit = idt.limit; p.idt_base = idt.base;
    p.gdt_limit = gdt.limit; p.gdt_base = gdt.base;
    p.ldt = get_ldt();
    p.tr = get_tr();
    return p;
}

static const char* hv_name(const std::string& v) {
    if (v.rfind("Microsoft Hv", 0) == 0) return "Hyper-V / VBS";
    if (v.rfind("VMwareVMware", 0) == 0) return "VMware";
    if (v.rfind("VBoxVBoxVBox", 0) == 0) return "VirtualBox";
    if (v.rfind("KVMKVMKVM", 0) == 0) return "KVM";
    if (v.rfind("XenVMMXenVMM", 0) == 0) return "Xen";
    if (v.rfind("TCGTCGTCGTCG", 0) == 0) return "QEMU TCG";
    if (v.rfind("bhyve bhyve", 0) == 0) return "bhyve";
    return "unknown";
}

static void report(const Probe& p) {
    printf("redpill 0.3  |  x64 ring-3 capability probe\n");
    printf("------------------------------------------------------------\n");
    printf("cpu vendor      %s   max leaf 0x%X  max ext 0x%X\n",
           p.cpu_vendor.c_str(), p.max_leaf, p.max_ext_leaf);
    printf("leaf 1 ecx      VMX %s  SMX %s  HV-present %s\n",
           p.vmx ? "yes" : "no ", p.smx ? "yes" : "no ",
           p.hypervisor_present ? "yes" : "no ");
    printf("ext 80000001    SVM (amd-v) %s\n", p.svm ? "yes" : "no");
    printf("leaf 7 ebx      0x%08X  XSAVES %s\n",
           p.leaf7_ebx, p.xsaves ? "yes" : "no");

    if (p.hypervisor_present) {
        printf("hv vendor       %s  (%s)\n", p.hv_vendor.c_str(), hv_name(p.hv_vendor));
        printf("hv max leaf     0x%X  signature 0x%08X\n",
               p.max_hv_leaf, p.hv_signature);
    /* revisit */
    }
    printf("\n");

    printf("idt             base 0x%016llX  limit 0x%04X\n",
           (unsigned long long)p.idt_base, p.idt_limit);
    printf("gdt             base 0x%016llX  limit 0x%04X\n",
           (unsigned long long)p.gdt_base, p.gdt_limit);
    printf("ldt             0x%04X  tr 0x%04X\n", p.ldt, p.tr);
    printf("\n");

    printf("verdict\n");
    if (!p.hypervisor_present)
        printf("  running on bare metal\n");
    else
        printf("  already inside %s\n", hv_name(p.hv_vendor));
    if (p.vmx && !p.hypervisor_present)
        printf("  vmx exposed and no outer hv -> vmxon should be reachable from ring 0\n");
    else if (p.vmx && p.hypervisor_present)
        printf("  vmx bit set but an outer hv owns it; needs nested virtualization\n");
    else if (!p.vmx && p.hypervisor_present)
        printf("  vmx masked by the outer hv (typical for hyper-v without nesting)\n");
    else
        printf("  no vmx in cpuid; check bios enablement / smx / feature control msr\n");

    printf("\nring-3 limits\n");
    printf("  IA32_FEATURE_CONTROL (0x3A), IA32_VMX_BASIC (0x480) and CR4.VMXE\n");
    printf("  are not readable here; that needs a ring-0 driver.\n");
    printf("  the classic redpill idt-base check is useless on x64 windows:\n");
    printf("  the idt lives in the kernel half of the address space regardless.\n");
}

static void report_json(const Probe& p) {
    printf("{\n");
    printf("  \"cpu_vendor\": \"%s\",\n", p.cpu_vendor.c_str());
    printf("  \"max_leaf\": %u,\n", p.max_leaf);
    printf("  \"max_ext_leaf\": %u,\n", p.max_ext_leaf);
    printf("  \"vmx\": %s,\n", p.vmx ? "true" : "false");
    printf("  \"smx\": %s,\n", p.smx ? "true" : "false");
    printf("  \"svm\": %s,\n", p.svm ? "true" : "false");
    printf("  \"leaf7_ebx\": \"0x%08X\",\n", p.leaf7_ebx);
    printf("  \"xsaves\": %s,\n", p.xsaves ? "true" : "false");
    printf("  \"hypervisor_present\": %s,\n", p.hypervisor_present ? "true" : "false");
    printf("  \"hv_vendor\": \"%s\",\n", p.hv_vendor.c_str());
    printf("  \"hv_max_leaf\": %u,\n", p.max_hv_leaf);
    printf("  \"hv_signature\": \"0x%08X\",\n", p.hv_signature);
    printf("  \"idt_base\": \"0x%016llX\",\n", (unsigned long long)p.idt_base);
    printf("  \"idt_limit\": %u,\n", p.idt_limit);
    printf("  \"gdt_base\": \"0x%016llX\",\n", (unsigned long long)p.gdt_base);
    printf("  \"gdt_limit\": %u,\n", p.gdt_limit);
    printf("  \"ldt\": %u,\n  \"tr\": %u\n", p.ldt, p.tr);
    printf("}\n");
}

static int selftest() {
    int bad = 0;
    auto ck = [&](bool c, const char* what) {
        printf("  %s  %s\n", c ? "ok  " : "BAD ", what);
        if (!c) bad++;
    };
    printf("redpill selftest\n");

    ck(sizeof(Dtr) == 10, "Dtr is 10 bytes");
    ck(offsetof(Dtr, base) == 2, "Dtr.base at offset 2");
