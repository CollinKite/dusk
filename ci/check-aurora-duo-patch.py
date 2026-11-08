#!/usr/bin/env python3
"""Verify clean application, repeat configuration, and preservation of conflicting edits."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as temporary:
    checkout = Path(temporary)
    source = checkout / "extern/aurora"
    source.mkdir(parents=True)
    archive = subprocess.Popen(["git", "archive", "HEAD"], cwd=root / "extern/aurora", stdout=subprocess.PIPE)
    subprocess.run(["tar", "-xf", "-", "-C", str(source)], stdin=archive.stdout, check=True)
    archive.stdout.close()
    assert archive.wait() == 0
    (checkout / "cmake/patches").mkdir(parents=True)
    for name in ("ApplyAuroraDuoPatch.cmake", "patches/aurora-duo.patch"):
        shutil.copyfile(root / "cmake" / name, checkout / "cmake" / name)
    command = ["cmake", "-P", str(checkout / "cmake/ApplyAuroraDuoPatch.cmake")]
    subprocess.run(command, check=True)
    expected = {p.relative_to(source): p.read_bytes() for p in source.rglob("*") if p.is_file()}
    subprocess.run(command, check=True)
    assert expected == {p.relative_to(source): p.read_bytes() for p in source.rglob("*") if p.is_file()}
    conflicting = source / "lib/window.cpp"
    conflicting.write_text("// Developer work that the patch must not overwrite.\n")
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode != 0 and "Preserve any local edits" in " ".join(result.stderr.split())
    assert conflicting.read_text() == "// Developer work that the patch must not overwrite.\n"
print("Aurora patch checks passed")
