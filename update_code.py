import os
import re

def deploy_active_power_optimizations():
    # ---------------------------------------------------------
    # Option 2: Dynamic Frequency Scaling (DFS)
    # ---------------------------------------------------------
    app_path = "main/core/app_main.cpp"
    if os.path.exists(app_path):
        with open(app_path, 'r') as f:
            content = f.read()

        if "esp_pm_configure" not in content:
            # Inject headers safely
            if "esp_pm.h" not in content:
                content = '#include "esp_pm.h"\n' + content

            dfs_block = """
    // ARCHITECT FIX: Enable Dynamic Frequency Scaling (DFS)
#if CONFIG_PM_ENABLE
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 40,
        .light_sleep_enable = true
    };
    esp_pm_configure(&pm_config);
#endif

    ui_render_init();"""
            
            # Anchor to the UI initialization
            content = content.replace("ui_render_init();", dfs_block)
            with open(app_path, 'w') as f:
                f.write(content)
            print("Patched: app_main.cpp (Injected Dynamic Frequency Scaling config)")
        else:
            print("DFS already configured in app_main.cpp.")

    # ---------------------------------------------------------
    # Option 4: Adaptive UI Frame Pacing
    # ---------------------------------------------------------
    ui_path = "main/ui/ui_render.cpp"
    if os.path.exists(ui_path):
        with open(ui_path, 'r') as f:
            content = f.read()

        if "lv_timer_set_period" not in content:
            # We explicitly target the existing state transition block you already built
            anchor = "was_moving = is_moving;"
            adaptive_logic = "lv_timer_set_period(timer, is_moving ? 33 : 200); // 30Hz active, 5Hz idle\n        was_moving = is_moving;"
            
            if anchor in content:
                content = content.replace(anchor, adaptive_logic)
                with open(ui_path, 'w') as f:
                    f.write(content)
                print("Patched: ui_render.cpp (Injected Adaptive UI Frame Pacing: 30Hz/5Hz)")
            else:
                print("Error: Could not find state transition anchor in ui_render.cpp")
        else:
            print("Adaptive pacing already configured in ui_render.cpp.")

if __name__ == "__main__":
    deploy_active_power_optimizations()