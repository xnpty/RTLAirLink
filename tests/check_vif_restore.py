#!/usr/bin/env python3
"""Compile the production VIF lifecycle with mocked radio boundaries."""
from pathlib import Path
import json
import os
import platform
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def extract_function(source, name):
    start = re.search(r"^(?:static )?(?:void |int |bool |u32 |u64 |__le32 |struct ieee80211_channel \*|struct ieee80211_chanctx_conf \*)" + re.escape(name)
                      + r"\s*\([^;{}]*\)\s*\{", source, re.M)
    if start is None:
        raise RuntimeError(f"Production function missing: {name}")
    depth = 1
    for brace in re.finditer(r"[{}]", source[start.end():]):
        depth += 1 if brace[0] == "{" else -1
        if depth == 0:
            return source[start.start():start.end() + brace.end()]
    raise RuntimeError(f"Production function incomplete: {name}")


def run(source, output, cc=None):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    cc = cc or shutil.which("cc")
    source = (Path(source) / "src/compat_rtw89/rtw89_glue.c").read_text()
    functions = ["owly_core04c_vif_register", "rtw89_glue_awdl_vif_create",
                 "rtw89_glue_awdl_vif_destroy", "rtw89_glue_awdl_vif_present",
                 "owly_restore_awdl_vif", "glue_up_locked", "rtw89_glue_up"]
    sdk = []
    if platform.system() == "Darwin":
        sdk = ["-isysroot", subprocess.check_output(
            ["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()]
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
    results = {}
    with tempfile.TemporaryDirectory(prefix="owly-vif-restore-") as temp:
        temp = Path(temp)
        (temp / "production_vif.inc").write_text("\n\n".join(extract_function(source, f) for f in functions))
        for mode, flags in (("normal", ["-O2"]),
                            ("asan_ubsan", ["-O1", "-g", "-fsanitize=address,undefined",
                                           "-fno-omit-frame-pointer"])):
            if mode == "asan_ubsan" and platform.system() == "Linux":
                flags.append("-no-pie")
            exe = temp / mode
            commands = {"compile": [cc, *sdk, *flags, "-std=c11", "-Wall", "-Werror",
                                    "-I" + str(temp), str(ROOT / "tests/vif_restore_model.c"), "-o", str(exe)],
                        "run": [str(exe)]}
            for stage, args in commands.items():
                result = subprocess.run(args, capture_output=True, text=True, env=env, timeout=45)
                log_path = output / f"{mode}-{stage}.txt"
                log_path.write_text(result.stdout + result.stderr)
                if result.returncode:
                    detail = '\n'.join((result.stderr or result.stdout).splitlines()[-12:])
                    raise RuntimeError(f"AWDL VIF restore {mode} {stage} failed; {log_path}\n{detail}")
            results[mode] = result.stdout.strip()
    return {"production_functions": functions, "mocked_radio_boundaries": True, "results": results}


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "Source-Build")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.source, args.out), indent=2))
