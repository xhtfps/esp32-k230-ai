"""CanMV v1.2.2 LVGL and touch API diagnostic.

Run this script from CanMV IDE before enabling the assistant page.  It prints
the APIs exposed by this exact firmware and renders one label when the active
LVGL screen API is available.  It intentionally does not initialise Sensor or
Display, and it does not replace main.py.
"""

import time

import lvgl as lv
from machine import TOUCH


def _sleep_ms(milliseconds):
    sleep_ms = getattr(time, "sleep_ms", None)
    if sleep_ms is not None:
        sleep_ms(milliseconds)
    else:
        time.sleep(float(milliseconds) / 1000.0)


def _print_public_api(name, value):
    names = [item for item in dir(value) if not item.startswith("_")]
    print("%s public API:" % name)
    print(names)


def _active_screen():
    for name in ("screen_active", "scr_act"):
        getter = getattr(lv, name, None)
        if getter is not None:
            print("LVGL active-screen API:", name)
            return getter()
    print("LVGL active-screen API not found")
    return None


def main():
    print("=== K230 CanMV LVGL / TOUCH diagnostic ===")
    print("LVGL module:", lv)
    print("TOUCH class:", TOUCH)
    _print_public_api("lv", lv)
    _print_public_api("TOUCH", TOUCH)

    screen = _active_screen()
    if screen is not None:
        label = lv.label(screen)
        label.set_text("LVGL diagnostic OK")
        align_center = getattr(lv.ALIGN, "CENTER", None)
        if align_center is not None:
            label.align(align_center, 0, 0)
        print("LVGL label created; verify it on the LCD")

    print("Do not guess TOUCH constructor or read method.")
    print("Send this complete serial output before enabling touch in main.py.")
    while True:
        handler = getattr(lv, "timer_handler", None)
        if handler is not None:
            handler()
        _sleep_ms(10)


if __name__ == "__main__":
    main()
