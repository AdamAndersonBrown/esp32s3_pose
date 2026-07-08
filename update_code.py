import os

def protect_apb_bus():
    filepath = "main/core/app_main.cpp"
    if not os.path.exists(filepath):
        print(f"ERROR: {filepath} not found.")
        return

    with open(filepath, 'r') as f:
        content = f.read()

    # The exact string injected in the previous DFS patch
    bad_config = ".min_freq_mhz = 40,"
    safe_config = ".min_freq_mhz = 80, // ARCHITECT FIX: Lock APB bus to 80MHz to prevent legacy I2C corruption"

    if bad_config in content:
        content = content.replace(bad_config, safe_config)
        with open(filepath, 'w') as f:
            f.write(content)
        print("Patched: app_main.cpp (Raised DFS floor to 80MHz to protect I2C APB clock)")
    else:
        print("Error: Could not find the 40MHz DFS configuration in app_main.cpp.")

if __name__ == '__main__':
    protect_apb_bus()