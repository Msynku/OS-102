#!/usr/bin/env python3
"""Run extra Lab3 regressions in a disposable copy, without changing the kernel."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", default="make")
    parser.add_argument("--gccprefix", default="riscv64-unknown-elf-")
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    logs = root / "validation"
    logs.mkdir(exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="lab3-extra-") as temporary:
        work = Path(temporary).resolve()
        assert work.parent == Path(tempfile.gettempdir()).resolve()
        for name in ("kern", "libs", "tools"):
            shutil.copytree(root / name, work / name)
        shutil.copy2(root / "Makefile", work / "Makefile")
        subprocess.run([sys.executable, str(root / "tools/inject.py"), str(work)], check=True)
        for name in ("test_lab3_extra.c", "test_lab3_extra_context.S"):
            shutil.copy2(root / "tools" / name, work / "kern/test" / name)
        init = work / "kern/init/init.c"
        text = init.read_text(encoding="utf-8")
        text = text.replace("#include <test_lab3.h>",
                            "#include <test_lab3.h>\nextern void test_lab3_extra(void);")
        text = text.replace("test_lab3_exception_context();", "test_lab3_extra();")
        init.write_text(text, encoding="utf-8")
        build = subprocess.run([args.make, "-j2", "GCCPREFIX=" + args.gccprefix],
                               cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (logs / "extra-build.log").write_bytes(build.stdout)
        if build.returncode:
            print(build.stdout.decode("utf-8", errors="replace"))
            raise SystemExit(build.returncode)
        result = subprocess.run([
            args.qemu, "-machine", "virt", "-nographic", "-bios", "default",
            "-kernel", "bin/ucore.img", "-monitor", "none", "-no-reboot",
        ], cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10)
        output = result.stdout.decode("utf-8", errors="replace").replace("\r", "")
        (logs / "extra.log").write_text(output, encoding="utf-8")
        assert result.returncode == 0, result.returncode
        assert "[Extra] 16/32-bit breakpoint and illegal instruction, sp/ra/gp/tp/a0/SIE: PASS" in output
        assert "[Extra] local_intr_save/restore preserves enabled and disabled states: PASS" in output
        assert sum(line.startswith("Breakpoint at") for line in output.splitlines()) == 4
        assert sum(line.startswith("Illegal instruction at") for line in output.splitlines()) == 4
        print("Extra regressions: PASS (2/4-byte traps, register/SIE restoration, interrupt guards)")


if __name__ == "__main__":
    main()
