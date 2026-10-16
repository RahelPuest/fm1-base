#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build an fm1-base firmware: the app, the update loader (felucca-bsp's) and an installable .fwsc package.

  tools/build.py APP_DIR [--release X.Y[.Z][-suffix]]
  tools/build.py APP_DIR --host        the host simulator instead: APP_DIR/build/host/NAME_host (cc, no toolchain)

APP_DIR holds the app (app.h, app.c, its sources) and its fm1.toml:
  name = "TEMPLATE"          the build's file names (lower case)
  id_prefix = "FM-1_0"       package identity: dev builds are the prefix + "00", releases the prefix + XXYYZZ
  usb_manufacturer = "..."   the USB names (felucca-bsp FELUCCA_USB_MANUFACTURER / FELUCCA_USB_PRODUCT)
  usb_product = "..."
  o2 = ["dsp/engine.c"]      separate units compiled at -O2 (the app's DSP), relative to APP_DIR
  words = "words.tsv"        the screen reader's words (fm1-reader), relative to APP_DIR
Outputs in APP_DIR/build/: NAME.bin (app), loader/ota.bin (update loader), NAME.fwsc (package).

The JieLi toolchain is Linux x86-64 only. JIELI_TOOLCHAIN points at it; on
macOS (or with JIELI_DOCKER=1) each tool runs in a linux/amd64 container.
"""
import argparse
import hashlib
import os
import platform
import re
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]        # fm1-base
BSP = SRC / "bsp"                                 # felucca-bsp (git submodule)
APP = OUT = GEN = LDR = ROOT = None               # set by main() from APP_DIR
CONF = {}
sys.path.insert(0, str(SRC / "tools"))
import fm1pkg_make  # noqa: E402
import lz4blk  # noqa: E402

APP_XIP = 0x02000120                # app.bin offset 0 in the XIP map; the SPL jumps here
APP_SLOT = fm1pkg_make.APP_SLOT
LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"    # the file name the SPL looks for
DOCKER_IMAGE = os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim")
CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function"]
# the AC79's single-precision FPU (the SDK's own flags); no fused multiply-add, so the host
# build (-ffp-contract=off) computes the same samples
FPU = ["-mcpu=r3", "-mfprev1", "-ffp-contract=off"]
LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)$")

# SDK files of AC79NN_SDK_V1.2.1_2023-12-13 (the tested version)
SDK_SHA256 = {
    "uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
    "cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
    "cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
}

PRODUCT = None                      # package identity (fm1.toml id_prefix; see main)
VERSION = None                      # APP_VERSION for release builds (default: "DEV", device/unity.c)


def read_conf(path):
    """fm1.toml: key = "string" | number | ["string", ...], # comments (the little TOML a firmware needs)"""
    conf = {}
    for no, line in enumerate(path.read_text().splitlines(), 1):
        line = re.sub(r"\s+#.*$", "", line).strip()
        if not line or line.startswith("#"):
            continue
        m = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)", line)
        if not m:
            raise SystemExit(f"{path}:{no}: not key = value")
        v = m.group(2)
        if v.startswith("["):
            conf[m.group(1)] = re.findall(r'"([^"]*)"', v)
        elif v.startswith('"'):
            conf[m.group(1)] = v.strip('"')
        else:
            conf[m.group(1)] = int(v, 0)
    return conf


def toolchain():
    tc = os.environ.get("JIELI_TOOLCHAIN")
    if not tc or not (Path(tc) / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit("JIELI_TOOLCHAIN must point at the JieLi Linux toolchain "
                         "(the directory with pi32v2/ and common/; see tools/get_toolchain.sh)")
    return Path(tc).resolve()


def use_docker():
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    return os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1"


_BUILDER = None
_BUILDER_LOCK = __import__("threading").Lock()


def builder():
    """one long-lived toolchain container per source tree (starting a container per tool call cost
    most of the build); it stops by itself after an hour without a build"""
    global _BUILDER
    with _BUILDER_LOCK:            # the loader and the app build in parallel: start it once
        return _builder_start()


def _builder_start():
    global _BUILDER
    if _BUILDER:
        return _BUILDER
    name = "jieli-" + hashlib.sha1(str(ROOT).encode()).hexdigest()[:10]
    st = subprocess.run(["docker", "inspect", "-f", "{{.State.Running}}", name], capture_output=True, text=True)
    if st.stdout.strip() != "true":
        subprocess.run(["docker", "rm", "-f", name], capture_output=True)
        r = subprocess.run(["docker", "run", "-d", "--rm", "--name", name, "--platform", "linux/amd64",
                            "-v", f"{ROOT}:/work", "-v", f"{toolchain()}:/opt/jieli:ro", "-w", "/work",
                            DOCKER_IMAGE, "sh", "-c", "sleep 3600"], capture_output=True, text=True)
        if r.returncode:
            sys.stderr.write(r.stderr)
            raise SystemExit("build: cannot start the toolchain container")
    else:                           # an hour from now again
        subprocess.run(["docker", "exec", "-d", name, "sh", "-c", "sleep 3600 && kill 1"], capture_output=True)
    _BUILDER = name
    return name


def tc(tool, *args):
    """run a toolchain binary (pi32v2/bin/..., common/bin/...) with cwd ROOT; paths relative to ROOT"""
    rel = [str(Path(a).resolve().relative_to(ROOT)) if isinstance(a, Path) else a for a in args]
    if tool == "cc":                # the toolchain's cc wrapper needs python3; call clang directly
        tool, rel = "pi32v2/bin/clang", ["-target", "pi32v2", *rel]
    if use_docker():
        cmd = ["docker", "exec", "-w", "/work", builder(), f"/opt/jieli/{tool}", *rel]
    else:
        cmd = [str(toolchain() / tool), *rel]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build: {tool} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def tc_all(*cmds):
    # at most 4 at once: a podman machine drops concurrent container connections (ssh EOF)
    with ThreadPoolExecutor(min(len(cmds), int(os.environ.get("FM1B_JOBS", "4")))) as ex:
        return list(ex.map(lambda c: tc(*c), cmds))


def generate():
    """generated headers: the fonts, the screen reader's words link (fm1-reader)"""
    GEN.mkdir(parents=True, exist_ok=True)
    tools = SRC / "tools"
    for c in [[tools / "gen_font.py", GEN / "felucca_font.h"],
              [tools / "gen_words.py", APP / CONF.get("words", "words.tsv"), APP / "web" / "words", GEN / "ann_words.h"]]:
        r = subprocess.run([sys.executable, *map(str, c)], capture_output=True, text=True)
        sys.stdout.write(r.stdout)
        if r.returncode:
            sys.stderr.write(r.stderr)
            raise SystemExit(f"build: {c[0].name} failed")


# ---- update loader

def crc16(d, c=0):
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def ldr_head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def ldr_wrap(image):
    """ota.bin = outer JLFS head (32 B) + inner head (32 B, load address) +
    blocks [clen u32][dlen u32][LZ4 block], dlen 4096 except the last"""
    blocks = bytearray()
    for i in range(0, len(image), 4096):
        raw = image[i:i + 4096]
        c = lz4blk.compress(raw)
        if len(c) > 4096 or lz4blk.decompress(c) != raw:
            raise SystemExit(f"loader: block {i // 4096} does not compress below 4096 B")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = ldr_head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    return ldr_head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body


def build_loader():
    LDR.mkdir(parents=True, exist_ok=True)
    src = BSP / "loader"
    flags = [*CFLAGS, *inc(BSP / "chip", BSP / "boards" / "fm1")]
    tc_all(("cc", "-c", src / "crt0_ldr.S", "-o", LDR / "crt0_ldr.o"),
           ("cc", *flags, "-c", src / "loader.c", "-o", LDR / "loader.o"))
    elf = LDR / "loader.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", src / "loader.ld",
       LDR / "crt0_ldr.o", LDR / "loader.o", "-o", elf)
    _, dis, hdr, syms = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, LDR / "loader.bin"),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-h", elf),
                               ("common/bin/objdump", "-t", elf))
    (LDR / "loader.dis").write_text(dis)
    for ln in hdr.splitlines():     # the loader rewrites the flash: nothing may run from XIP
        p = ln.split()
        if len(p) > 4 and p[1].startswith(".") and int(p[3], 16) >= 0x02000000 and int(p[2], 16):
            raise SystemExit(f"loader: section {p[1]} at {p[3]} is outside RAM")
    image = (LDR / "loader.bin").read_bytes()
    if not image or len(image) > 0x14000:
        raise SystemExit("loader: image empty or too big")
    ota = ldr_wrap(image)
    (LDR / "ota.bin").write_bytes(ota)
    bss = [int(ln.split()[0], 16) for ln in syms.splitlines() if ln.rstrip().endswith("_bss_end")]
    print(f"loader: image {len(image)} B at {LOADER_LOAD:#x}" + (f", bss end {bss[0]:#x}" if bss else ""))
    print(f"loader: ota.bin {len(ota)} B")
    return ota


# ---- app

def inc(*dirs):
    return [f"-I{Path(d).resolve().relative_to(ROOT)}" for d in dirs]


def build_app():
    flags = [*CFLAGS, *FPU, *inc(BSP / "chip", BSP / "boards" / "fm1", SRC / "device", SRC / "include", APP, GEN)]
    for flag in ("FM1B_CDC", "FM1B_UAC", "FM1B_TRS"):
        v = os.environ.get(flag)    # unset: the app's (app.h) or the platform's default (device/unity.c)
        if v in ("0", "1"):
            flags.append(f"-D{flag}={v}")
    flags.append(f'-DFELUCCA_ID="{PRODUCT}"')
    for key, macro in (("usb_manufacturer", "FELUCCA_USB_MANUFACTURER"), ("usb_product", "FELUCCA_USB_PRODUCT")):
        if CONF.get(key):
            flags.append(f'-D{macro}="{CONF[key]}"')
    if VERSION:
        flags.append(f'-DAPP_VERSION="{VERSION}"')
    o2 = [f if f != "-Os" else "-O2" for f in flags]
    units = [("cc", "-c", BSP / "chip" / "crt0.S", "-o", OUT / "crt0.o"),
             ("cc", "-c", BSP / "chip" / "fm1_vec.S", "-o", OUT / "fm1_vec.o"),
             ("cc", "-c", BSP / "chip" / "fm1_isr.S", "-o", OUT / "fm1_isr.o"),
             ("cc", *flags, "-c", SRC / "device" / "unity.c", "-o", OUT / "unity.o")]
    objs = [OUT / "crt0.o", OUT / "fm1_vec.o", OUT / "fm1_isr.o", OUT / "unity.o"]
    (OUT / "o2").mkdir(parents=True, exist_ok=True)
    for u in CONF.get("o2", []):
        o = OUT / "o2" / (u.replace("/", "_").rsplit(".", 1)[0] + ".o")
        units.append(("cc", *o2, "-c", APP / u, "-o", o))
        objs.append(o)
    tc_all(*units)
    elf = OUT / (NAME + ".elf")
    # unreferenced functions (each in its own section: -ffunction-sections) are dropped: the DSP keeps
    # mono reference paths for the host tests that the firmware no longer calls
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", SRC / "device" / "app.ld", *objs, "-o", elf)
    for sect in ("text.bin", "data.bin", "ramtext.bin"):
        (OUT / sect).unlink(missing_ok=True)
    *_, syms, dis, rt = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, OUT / "text.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".data", elf, OUT / "data.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_text", elf, OUT / "ramtext.bin"),
                               ("common/bin/objdump", "-t", elf),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-d", "-j", ".ram_text", elf))
    (OUT / (NAME + ".dis")).write_text(dis)

    def symv(name):
        return int(re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M).group(1), 16)
    img = bytearray((OUT / "text.bin").read_bytes())
    # .ram_text and .data follow .text at their load addresses; crt0 copies them by words
    for sect, lname in (("ramtext.bin", "_rt_load"), ("data.bin", "_data_load")):
        load = symv(lname)
        if load % 4:
            raise SystemExit(f"{lname} {load:#x} is not word aligned")
        blob = (OUT / sect).read_bytes() if (OUT / sect).exists() else b""
        if blob:
            if load - APP_XIP < len(img):
                raise SystemExit(f"{lname} overlaps the image")
            img += b"\xff" * (load - APP_XIP - len(img))
            img += blob
    img += b"\xff" * (-len(img) % 4)
    (OUT / (NAME + ".bin")).write_bytes(img)
    return bytes(img), syms, dis, rt


def check(img, syms, dis, rt):
    errors, notes = [], []
    m = re.search(r"^([0-9a-f]+) .*\s_start$", syms, re.M)
    if not m or int(m.group(1), 16) != APP_XIP:
        errors.append(f"_start is not at {APP_XIP:#x}")
    if img[:4] != bytes.fromhex("04818000"):
        errors.append(f"image starts with {img[:4].hex()}, not the entry stub")
    rt_calls = [ln for ln in rt.splitlines() if re.search(r"\bcall\b", ln)]
    if rt_calls:                    # RAM code runs with the flash off: no calls into XIP
        errors.append(f".ram_text contains calls: {rt_calls[:3]}")
    else:
        notes.append(f".ram_text: {len([ln for ln in rt.splitlines() if LINE.match(ln)])} insns, no calls")
    for ln in dis.splitlines():     # nothing may call or load an address in the chip ROM
        mm = LINE.match(ln)
        if not mm:
            continue
        for v in re.findall(r"= (-?\d+) <|call -?\d+ <[^:>]*: ([0-9a-f]+) >", mm.group(3)):
            val = (int(v[0]) & 0xFFFFFFFF) if v[0] else int(v[1], 16) & 0xFFFFFFFF
            if 0xFFC00000 <= val < 0xFFD00000:
                errors.append(f"reference to ROM address {val:#010x}")
    if len(img) > APP_SLOT:
        errors.append(f"image {len(img)} B exceeds the app slot")
    # the FPU is single precision; a soft-double routine in the image means a double crept in
    soft = sorted(set(re.findall(r"\s(__(?:add|sub|mul|div|neg|cmp|eq|ne|lt|le|gt|ge|un|extendsf|truncdf|fixdf|fixunsdf|floatsidf|floatunsidf|floatdidf)\w*df\w*)$", syms, re.M)))
    if soft:
        errors.append(f"soft-double routines linked: {soft[:6]} (a double in the code)")
    else:
        notes.append("no soft-double routines (single-precision FPU only)")

    def sym(name):
        mm = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        return int(mm.group(1), 16) if mm else 0
    bss = sym("_bss_end") - 0x01C08000
    pool = sym("_pool_end") - sym("_pool_start")
    notes.append(f"image {len(img)} B; RAM .data+.bss {bss} B of 98304; pool {pool} B of {0x54000}")
    if bss > 96 * 1024:
        errors.append("RAM region overflow")
    if 0x54000 - pool < 8192:                     # keep >= 8 KiB of the pool spare
        errors.append(f"pool headroom {0x54000 - pool} B < 8192 B")
    return errors, notes


# Every hardware register access lives in felucca-bsp's chip/ and boards/. In device/, include/ and the app
# (comments stripped):
#  - no volatile pointer cast, except of a C object's address (`*(volatile T *)&x`: a read-once
#    of a RAM flag shared with an ISR, e.g. usb.c ota_wire_send);
#  - no literal in a register or reserved window (core SFRs 0x10000-0x13FFF, SFC 0x40000-0x43FFF,
#    GPIO/IOMAP 0x50000-0x51FFF, CPU 0x1EE0000-0x1EEFFFF, RAM top 0x01C7F000- (boot info,
#    mailbox, vectors), XIP 0x02000000-0x020FFFFF);
#  - no inline asm, except the empty compiler barrier RING_PUBLISH() (emits no instruction);
#  - no HAL register macro (a hal/ #define that is, or expands to, a volatile access).
# Linker symbols (_bss_start[], _rt_load[], ...) are plain C objects and pass.
MMIO_LIT = re.compile(r"\b0x0*(1[0-3][0-9a-f]{3}|4[0-3][0-9a-f]{3}|5[01][0-9a-f]{3}|1ee[0-9a-f]{4}|"
                      r"1c7f[0-9a-f]{3}|20[0-9a-f]{5})u?l?\b", re.I)
MMIO_CAST = re.compile(r"\(\s*(?:const\s+)?volatile\b[^()]*\*\s*\)(?!\s*&)")
MMIO_ASM = re.compile(r"\b(?:__asm__|asm)\b(?!\s+volatile\s*\(\s*\"\"\s*:::\s*\"memory\"\s*\))")


def mmio_check():
    def strip(s):
        s = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), s, flags=re.S)
        return re.sub(r"//[^\n]*", "", s)
    defs = {}
    for h in sorted([*(BSP / "chip").glob("*.h"), *(BSP / "boards" / "fm1").glob("*.h")]):
        for m in re.finditer(r"^#define\s+(\w+)(?:\([^)]*\))?\s+(.*)$", strip(h.read_text()), re.M):
            defs[m.group(1)] = m.group(2)
    regs = {n for n, b in defs.items() if re.search(r"\bvolatile\b", b)}
    while True:                                   # macros built on register macros (FM1_WR_LIMIT_H -> FM1_X2)
        more = {n for n, b in defs.items() if n not in regs and set(re.findall(r"\w+", b)) & regs}
        if not more:
            break
        regs |= more
    errors = []
    files = [*(SRC / "device").glob("*.[ch]"), *(SRC / "include").glob("*.h"),
             *[f for f in APP.rglob("*.[ch]") if "build" not in f.relative_to(APP).parts]]
    for f in sorted(files):
        for no, ln in enumerate(strip(f.read_text()).splitlines(), 1):
            where = f"{f.relative_to(ROOT)}:{no}"
            for rx, what in ((MMIO_LIT, "register/window address"), (MMIO_CAST, "volatile pointer cast"),
                             (MMIO_ASM, "inline asm")):
                m = rx.search(ln)
                if m:
                    errors.append(f"{where}: {what} outside the BSP ({m.group(0).strip()}); add a BSP helper")
            used = set(re.findall(r"\b[A-Z_][A-Z0-9_]*\b", ln)) & regs
            if used:
                errors.append(f"{where}: BSP register macro {sorted(used)[0]} outside the BSP; use a BSP helper")
    return errors


def build_host():
    """the app against a simulated FM-1 (host/plat_host.c), with this machine's compiler; -ffp-contract=off like the
    device (no fused multiply-add), so the samples match"""
    out = OUT / "host"
    out.mkdir(parents=True, exist_ok=True)
    exe = out / (NAME + "_host")
    cmd = [os.environ.get("CC", "cc"), "-O2", "-ffp-contract=off", "-std=c99", "-Wall", "-Wextra",
           "-Wno-unused-function", "-Wno-unused-parameter", "-DPLAT_HOST",
           *(f"-I{d}" for d in (SRC / "device", SRC / "include", APP, GEN)),
           "-o", str(exe), str(SRC / "host" / "plat_host.c"), *(str(APP / u) for u in CONF.get("o2", [])), "-lm"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    sys.stderr.write(r.stderr)
    if r.returncode:
        raise SystemExit("build: the host simulator did not compile")
    print(f"host     {exe}")
    return 0


def main():
    global PRODUCT, VERSION, APP, OUT, GEN, LDR, ROOT, CONF, NAME
    ap = argparse.ArgumentParser()
    ap.add_argument("app", type=Path, help="the app's directory (with fm1.toml)")
    ap.add_argument("--release", metavar="X.Y[.Z]", help="release build: identity id_prefix + XXYYZZ, version string X.Y[.Z]")
    ap.add_argument("--sdk", type=Path, help="JieLi AC79 SDK checkout (default: $AC79_SDK)")
    ap.add_argument("--host", action="store_true", help="build the host simulator instead of the firmware")
    a = ap.parse_args()
    APP = a.app.resolve()
    CONF = read_conf(APP / "fm1.toml")
    NAME = CONF.get("name", APP.name).lower()
    OUT = APP / "build"
    GEN, LDR = OUT / "gen", OUT / "loader"
    ROOT = Path(os.path.commonpath([SRC, APP]))
    prefix = CONF.get("id_prefix", "FM-1_0")
    if not re.fullmatch(r"FM-1_\d+", prefix):
        raise SystemExit(f"fm1.toml id_prefix {prefix!r}: FM-1_ and digits only (the update tools parse it)")
    PRODUCT = prefix + "00"
    name = NAME + ".fwsc"
    if a.host:
        generate()
        return build_host()
    if a.release:
        # identity: the prefix + major, minor, patch as two digits each (0.1 -> FM-1_0000100): FM-1 is the
        # model the update tools match on; the digits after the underscore are the firmware's.
        m = re.fullmatch(r"(\d{1,2})\.(\d{1,2})(?:\.(\d{1,2}))?(-[A-Za-z0-9]+)?", a.release)
        if not m:
            raise SystemExit(f"--release {a.release}: use X.Y[.Z][-suffix], up to two digits each")
        PRODUCT = prefix + "%02d%02d%02d" % (int(m[1]), int(m[2]), int(m[3] or 0))
        VERSION = a.release.upper() if "BETA" in a.release.upper() else a.release.upper() + " BETA"
        name = f"{NAME}-{a.release}.fwsc"
    fm1pkg_make.SDK = a.sdk
    for rel, sha in SDK_SHA256.items():          # fail early without the SDK
        if hashlib.sha256(fm1pkg_make.sdk_file(rel)).hexdigest() != sha:
            print(f"warning: SDK {rel} differs from AC79NN_SDK_V1.2.1; the package will not match the reference")
    OUT.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(2) as ex:
        gen, ldr = ex.submit(generate), ex.submit(build_loader)
        gen.result()
        ota = ldr.result()
    img, syms, dis, rt = build_app()
    errors, notes = check(img, syms, dis, rt)
    hal_err = mmio_check()
    errors += hal_err
    if not hal_err:
        notes.append("register access: felucca-bsp only (device/, include/ and the app clean)")
    for n in notes:
        print("  ok   ", n)
    for e in errors:
        print("  FAIL ", e)
    if errors:
        raise SystemExit("build: checks failed")
    pkg = fm1pkg_make.ufw(fm1pkg_make.flash_image(img, fm1pkg_make.KEY), ota, PRODUCT)
    (OUT / name).write_bytes(pkg)
    print(f"app      {OUT / (NAME + '.bin')}  {len(img)} B")
    print(f"loader   {LDR / 'ota.bin'}  {len(ota)} B")
    print(f"package  {OUT / name}  {len(pkg)} B, identity {PRODUCT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
