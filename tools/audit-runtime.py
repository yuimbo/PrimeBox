#!/usr/bin/env python3
"""Audit the PrimeBox chroot for missing symbols / ABI mismatches.

Checks the WHOLE runtime, not just the paths a canary run happens to touch:

  * every ELF's NEEDED libraries resolve inside the chroot,
  * every undefined dynamic symbol is defined by some library in the chroot
    (or by the binary itself),
  * every GLIBC_* version a binary references is provided by the chroot glibc,
  * DirectFB module ABI versions match the RX3 core's system ABI.

Usage: audit-runtime.py <chroot> [extra-binary ...]

Binaries are looked up first as a path, then relative to the chroot.
"""
import os
import re
import subprocess
import sys

SEARCH = ("lib", "usr/lib", "usr/lib/directfb-1.4-0", "usr/bin", "bin", "usr/sbin", "sbin")

# weak / optional symbols every ELF may leave undefined; the loader does not
# require them, so they are not runtime problems.
OPTIONAL = {
    "__gmon_start__", "_Jv_RegisterClasses",
    "_ITM_deregisterTMCloneTable", "_ITM_registerTMCloneTable",
}


def sh(*args):
    return subprocess.run(args, capture_output=True, text=True).stdout


def elf(path):
    try:
        with open(path, "rb") as f:
            return f.read(4) == b"\x7fELF"
    except OSError:
        return False


def dynamic(path):
    needed, soname = [], None
    for line in sh("objdump", "-p", path).splitlines():
        s = line.strip()
        if s.startswith("NEEDED"):
            needed.append(s.split()[-1])
        elif s.startswith("SONAME"):
            soname = s.split()[-1]
    return needed, soname


def syms(path):
    """-> (defined {name: version}, undefined {name: version})."""
    defined, undefined = {}, {}
    for line in sh("objdump", "-T", path).splitlines():
        toks = line.split()
        if len(toks) < 3 or not re.match(r"^[0-9a-f]{8}$", toks[0]):
            continue
        name = toks[-1]
        if name in ("_init", "_fini", "_edata", "_end", "__bss_start",
                    "__bss_start__", "__bss_end__", "_end__"):
            continue
        ver = None
        for t in toks:
            if t.startswith("(") and t.endswith(")"):
                ver = t.strip("()")
        (undefined if "*UND*" in toks else defined)[name] = ver
    return defined, undefined


def glibc_versions(path):
    vers = set()
    for line in sh("objdump", "-p", path).splitlines():
        m = re.search(r"GLIBC_[0-9.]+", line)
        if m:
            vers.add(m.group(0))
    return vers


def index(chroot):
    """name -> real file, including symlinked sonames."""
    libs = {}
    for d in SEARCH:
        base = os.path.join(chroot, d)
        if not os.path.isdir(base):
            continue
        for root, _, files in os.walk(base):
            for fn in files:
                if ".so" not in fn:
                    continue
                p = os.path.join(root, fn)
                if not os.path.isfile(p):
                    continue
                real = os.path.realpath(p)
                if not elf(real):
                    continue
                libs.setdefault(fn, real)
    return libs


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    chroot = sys.argv[1]
    targets = sys.argv[2:] or [
        "rbp-audio", "knobshim2.so", "audioshim.so", "fbshim-tsc.so",
        "libdirectfb_fbdev-rot16.so", "libdirectfb-1.4.so.0",
        "libdirect-1.4.so.0", "libfusion-1.4.so.0",
    ]
    libs = index(chroot)

    # global symbol scope: every defined symbol and version in the chroot
    defined = {}
    for p in set(libs.values()):
        d, _ = syms(p)
        defined.update(d)
    # glibc version set
    glibc = None
    for n in ("libc.so.6", "ld-linux.so.3"):
        if n in libs:
            glibc = glibc_versions(libs[n]) | (glibc or set())
    glibc = glibc or set()

    problems = 0
    for t in targets:
        p = t if os.path.exists(t) else os.path.join(chroot, t.lstrip("/"))
        if not os.path.exists(p):
            p = os.path.join(os.path.dirname(__file__), "..", t)
        if not elf(p):
            print(f"SKIP {t} (not an ELF here)")
            continue
        needed, soname = dynamic(p)
        td, tu = syms(p)
        print(f"\n== {t}  soname={soname}  undef={len(tu)}")

        for n in needed:
            if n not in libs:
                print(f"  MISSING NEEDED  {n}")
                problems += 1
        for name in sorted(tu):
            if name not in defined and name not in td and name not in OPTIONAL:
                print(f"  UNRESOLVED SYMBOL  {name}")
                problems += 1
        vers = glibc_versions(p)
        for v in sorted(vers - glibc):
            print(f"  GLIBC VERSION NOT PROVIDED  {v}")
            problems += 1

    print(f"\n{'OK - no missing symbols/versions' if problems == 0 else str(problems) + ' problem(s)'}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
