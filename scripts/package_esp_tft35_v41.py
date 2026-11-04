#!/usr/bin/env python3
"""Package an already-built ESP-TFT35 v4.1 image; never connect to hardware."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = root / "build/esp_tft35_v41"
image = "ESP-TFT35-v4.1-fluidnc-pendant-merged.bin"
parts = ["bootloader/bootloader.bin", "partition_table/partition-table.bin",
         "ota_data_initial.bin", "ESP3D-TFT.bin"]
subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32", "merge_bin",
                "-o", image, "--flash_mode", "dio", "--flash_freq", "40m", "--flash_size", "8MB",
                "0x1000", parts[0], "0x8000", parts[1], "0xe000", parts[2], "0x10000", parts[3]],
               cwd=build, check=True)
subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32", "image_info", parts[3]],
               cwd=build, check=True)

def digest(path):
    data = path.read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}

manifest = {
    "status": "EXPERIMENTAL FluidNC UART pendant; host-tested; physical hardware NOT TESTED",
    "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
    "source_has_local_changes": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root)),
    "idf": json.loads((build / "project_description.json").read_text()).get("idf_ver"),
    "python": sys.version,
    "toolchain": "xtensa-esp32-elf GCC 12.2.0 (esp-12.2.0_20230208)",
    "flash_offset": "0x0 (merged image); screen ESP32 only",
    "hardware_validation": "NOT TESTED; no device flashed by packaging",
    "host_test_command": "scripts/test_grbl.sh (separate from build/packaging)",
    "build_environment_note": os.environ.get("ESP3D_BUILD_NOTE", "Standard activated ESP-IDF environment"),
    "files": {name: digest(build / name) for name in parts + [image]},
    "sdkconfig": digest(build / "sdkconfig"),
}
# Identify all locally modified/new source inputs, including this board port.
paths = subprocess.check_output(["git", "ls-files", "-m", "-o", "--exclude-standard", "-z"], cwd=root).decode().split("\0")
manifest["local_source_files"] = {p: digest(root / p) for p in sorted(set(paths)) if p and (root / p).is_file()}
(build / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
shutil.copy2(root / "hardware/ESP32_ESP_TFT35_V41/README.md", build / "README.md")
print(f"Packaged {build / image}")
