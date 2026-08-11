Import("env")

from os.path import join
from subprocess import run


def _unlock_once() -> int:
    platform = env.PioPlatform()
    openocd_dir = platform.get_package_dir("tool-openocd")
    if not openocd_dir:
        print("ERROR: OpenOCD package not installed, cannot run recovery unlock.")
        return 1

    openocd_bin = join(openocd_dir, "bin", "openocd")
    interface_cfg = join(openocd_dir, "openocd", "scripts", "interface", "stlink.cfg")
    target_cfg = join(openocd_dir, "openocd", "scripts", "target", "stm32f4x.cfg")

    print("Running STM32F4 unlock-only recovery...")
    result = run(
        [
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
        ],
        check=False,
    )
    return result.returncode


def _recover_unlock_target(*_args, **_kwargs):
    code = _unlock_once()
    print("\n=== RECOVERY RESULT ===")
    if code != 0:
        print("Unlock failed.")
        print("Fix probe/wiring first, then retry:")
        print("  /home/interplain/.platformio/penv/bin/platformio run -e genericSTM32F405RG_recovery")
        return code

    print("Unlock command completed.")
    print("Power-cycle the target board now (remove and re-apply board power).")
    print("After power-cycle, flash normally:")
    print("  /home/interplain/.platformio/penv/bin/platformio run -e genericSTM32F405RG -t upload")
    print("=======================\n")
    return 0


env.AddCustomTarget(
    name="recover_unlock",
    dependencies=None,
    actions=[_recover_unlock_target],
    title="STM32 Unlock Recovery",
    description="Unlock readout protection only. No firmware flash is attempted.",
    always_build=True,
)
