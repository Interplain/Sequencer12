Import("env")

import os
from os.path import join
from subprocess import run


def unlock_before_upload(source, target, env):
    recovery_enabled = os.getenv("PIO_UPLOAD_RECOVERY", "0").lower()
    if recovery_enabled in ("0", "false", "no", "off"):
        return 0

    if env.get("UPLOAD_PROTOCOL") != "stlink":
        return 0

    platform = env.PioPlatform()
    openocd_dir = platform.get_package_dir("tool-openocd")
    if not openocd_dir:
        print("Skipping flash recovery: OpenOCD package not installed.")
        return 0

    openocd_root = openocd_dir
    openocd_bin = join(openocd_root, "bin", "openocd")
    interface_cfg = join(openocd_root, "openocd", "scripts", "interface", "stlink.cfg")
    target_cfg = join(openocd_root, "openocd", "scripts", "target", "stm32f4x.cfg")

    print("Running STM32F4 unlock before upload...")
    result = run([
        openocd_bin,
        "-f",
        interface_cfg,
        "-f",
        target_cfg,
        "-c",
        "init",
        "-c",
        "reset halt",
        "-c",
        "stm32f2x unlock 0",
        "-c",
        "shutdown",
    ], check=False)

    return result.returncode


env.AddPreAction("upload", unlock_before_upload)