#!/usr/bin/env sh
# Re-apply after: rm -rf managed_components && idf.py reconfigure
# nixy4/u8g2 port includes driver/gpio.h; ESP-IDF 6 needs esp_driver_gpio on REQUIRES.
set -e
root="$(cd "$(dirname "$0")/.." && pwd)"
f="$root/managed_components/nixy4__u8g2/CMakeLists.txt"
[ -f "$f" ] || { echo "missing $f — run from repo: idf.py reconfigure"; exit 1; }
if grep -q 'esp_driver_gpio' "$f"; then
  echo "already patched"
  exit 0
fi
sed -i 's/REQUIRES driver esp_driver_i2c esp_driver_spi$/REQUIRES driver esp_driver_i2c esp_driver_spi esp_driver_gpio/' "$f"
echo "patched $f"
