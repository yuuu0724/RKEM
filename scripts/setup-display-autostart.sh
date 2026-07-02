#!/bin/bash

set -euo pipefail

if [ "${EUID}" -ne 0 ]; then
    echo "Please run this installer as root: sudo $0" >&2
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

install -d -m 0755 /etc/systemd/system/gdm.service.d /userdata/scripts
install -m 0644 "$script_dir/display/gdm-override.conf" \
    /etc/systemd/system/gdm.service.d/override.conf
install -m 0644 "$script_dir/display/lcd-auto-start.service" \
    /etc/systemd/system/lcd-auto-start.service
install -m 0755 "$script_dir/display/auto_lcd.sh" \
    /userdata/scripts/auto_lcd.sh

systemctl daemon-reload
systemctl set-default graphical.target
systemctl enable lcd-auto-start.service
systemctl reset-failed display-manager.service lcd-auto-start.service || true
systemctl restart lcd-auto-start.service

systemctl is-enabled display-manager.service
systemctl is-enabled lcd-auto-start.service
systemctl get-default
systemctl --no-pager --full status lcd-auto-start.service display-manager.service
