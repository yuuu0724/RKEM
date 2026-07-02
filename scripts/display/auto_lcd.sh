#!/bin/bash

set -u

# Let the display and backlight drivers finish probing before refreshing GDM.
sleep 5

if [ -w /sys/class/graphics/fb0/blank ]; then
    printf '0\n' > /sys/class/graphics/fb0/blank
fi

for backlight in /sys/class/backlight/*; do
    [ -d "$backlight" ] || continue
    if [ -r "$backlight/max_brightness" ] && [ -w "$backlight/brightness" ]; then
        cat "$backlight/max_brightness" > "$backlight/brightness"
    fi
done

systemctl restart display-manager
