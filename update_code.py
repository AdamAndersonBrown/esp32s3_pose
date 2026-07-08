import os
import re

def revert_rf_severance():
    filepath = "main/core/app_main.cpp"
    if not os.path.exists(filepath):
        print(f"ERROR: {filepath} not found.")
        return

    with open(filepath, 'r') as f:
        content = f.read()

    # 1. Remove the injected headers
    content = content.replace('#include "esp_wifi.h"\n', '')
    content = content.replace('#include "esp_bt.h"\n', '')
    content = content.replace('#include "esp_bt_main.h"\n', '')

    # 2. Remove the shutdown block
    shutdown_pattern = r'[ \t]*// ARCHITECT FIX: Explicitly sever the RF Baseband.*?\n[ \t]*esp_wifi_stop\(\);\n'
    content = re.sub(shutdown_pattern, '', content, flags=re.DOTALL)

    with open(filepath, 'w') as f:
        f.write(content)
        
    print("Patched: app_main.cpp (Reverted unnecessary RF baseband severance)")

if __name__ == '__main__':
    revert_rf_severance()