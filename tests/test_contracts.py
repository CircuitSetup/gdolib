import json
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class GdolibContracts(unittest.TestCase):
    def test_lock_toggle_symbols_are_preserved(self):
        source = (ROOT / "gdo.c").read_text(encoding="utf-8")

        self.assertTrue(
            "esp_err_t gdo_toggle_lock(void)" in source,
            "documented gdo_toggle_lock implementation is missing",
        )
        self.assertTrue(
            "esp_err_t gdo_lock_toggle(void)" in source,
            "legacy gdo_lock_toggle implementation is missing",
        )

    def test_host_protocol_and_string_contracts(self):
        platformio_gcc = (
            Path.home()
            / ".platformio"
            / "packages"
            / "toolchain-gccmingw32"
            / "bin"
            / "gcc.exe"
        )
        compiler = shutil.which("gcc") or (
            str(platformio_gcc) if platformio_gcc.exists() else shutil.which("clang")
        )
        self.assertIsNotNone(compiler, "clang or gcc is required")

        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            include = temp / "include"
            (include / "driver").mkdir(parents=True)
            (include / "driver" / "uart.h").write_text(
                textwrap.dedent(
                    """
                    #pragma once
                    #include <stdbool.h>
                    #include <stddef.h>
                    #include <stdint.h>
                    typedef int esp_err_t;
                    typedef int uart_port_t;
                    typedef struct { int type; size_t size; } uart_event_t;
                    enum { UART_EVENT_MAX = 16, UART_NUM_MAX = 3 };
                    """
                ),
                encoding="ascii",
            )
            (include / "driver" / "gpio.h").write_text(
                "#pragma once\ntypedef int gpio_num_t;\nenum { GPIO_NUM_MAX = 64 };\n",
                encoding="ascii",
            )
            (include / "esp_timer.h").write_text(
                "#pragma once\ntypedef void *esp_timer_handle_t;\n", encoding="ascii"
            )
            (include / "esp_log.h").write_text(
                "#pragma once\n#define ESP_LOGD(...) ((void) 0)\n", encoding="ascii"
            )
            program = temp / "contracts.c"
            program.write_text(
                textwrap.dedent(
                    """
                    #include <stdint.h>
                    #include "gdo.h"
                    #include "secplus.h"

                    #define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

                    static int same_string(const char *left, const char *right) {
                        while (*left != '\\0' && *left == *right) {
                            ++left;
                            ++right;
                        }
                        return *left == *right;
                    }

                    int main(void) {
                        CHECK(same_string(gdo_battery_state_to_string(GDO_BATT_STATE_UNKNOWN), "Unknown"));
                        CHECK(same_string(gdo_battery_state_to_string(GDO_BATT_STATE_CHARGING), "Charging"));
                        CHECK(same_string(gdo_battery_state_to_string(GDO_BATT_STATE_FULL), "Full"));
                        CHECK(same_string(gdo_battery_state_to_string((gdo_battery_state_t) 7), "Invalid"));
                        CHECK(same_string(gdo_door_state_to_string((gdo_door_state_t) -1), "Invalid"));

                        const uint32_t rolling = 0x1234567;
                        const uint64_t fixed = 0x123456789a;
                        const uint32_t data = 0x12340678;
                        uint8_t packet[19];
                        uint32_t decoded_rolling;
                        uint64_t decoded_fixed;
                        uint32_t decoded_data;

                        CHECK(encode_wireline(rolling, fixed, data, packet) == 0);
                        CHECK(decode_wireline(packet, &decoded_rolling, &decoded_fixed, &decoded_data) == 0);
                        CHECK(decoded_rolling == rolling);
                        CHECK(decoded_fixed == fixed);
                        CHECK((decoded_data & ~0xf000U) == data);
                        return 0;
                    }
                    """
                ),
                encoding="ascii",
            )
            executable = temp / "contracts.exe"
            build = subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-parameter",
                    "-Wno-unused-variable",
                    f"-I{include}",
                    f"-I{ROOT / 'include'}",
                    f"-I{ROOT}",
                    str(ROOT / "gdo_utils.c"),
                    str(ROOT / "secplus.c"),
                    str(program),
                    "-o",
                    str(executable),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run(
                [executable], check=False, capture_output=True, text=True
            )
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_queue_and_lifecycle_contracts(self):
        source = (ROOT / "gdo.c").read_text(encoding="utf-8")
        private_header = (ROOT / "gdo_priv.h").read_text(encoding="utf-8")

        self.assertIn("uint8_t packet[19];", private_header)
        self.assertNotIn("malloc(19)", source)
        self.assertNotIn("free(tx_message.packet)", source)
        self.assertIn("GDO_ROLLING_CODE_MAX", source)
        self.assertIn("g_openings_known", source)

        obstruction_timer = source.split("static void obst_timer_cb(void* arg) {", 1)[1]
        obstruction_timer = obstruction_timer.split("\n}\n", 1)[0]
        self.assertNotIn("queue_event((gdo_event_t){GDO_EVENT_OBST})", obstruction_timer)

    def test_release_metadata_and_example_contracts(self):
        metadata = json.loads((ROOT / "library.json").read_text(encoding="utf-8"))
        example = (ROOT / "examples" / "main" / "main.c").read_text(
            encoding="utf-8"
        )

        self.assertEqual(metadata["version"], "1.2.1")
        self.assertIn("status->door_position / 100.0f", example)
        self.assertIn("esp_err_t err = gdo_init(&gdo_conf);", example)
        self.assertIn("err = gdo_start(gdo_event_handler, NULL);", example)


if __name__ == "__main__":
    unittest.main()
