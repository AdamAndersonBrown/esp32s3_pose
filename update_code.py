import os

fusion_cpp = os.path.join("main", "fusion", "eskf_fusion.cpp")

with open(fusion_cpp, "r") as f:
    content = f.read()

old_init = """    // ARCHITECT FIX: Load localized Hard-Iron footprint from Flash Memory
    if (eskf_load_calibration(&mag_profile) != ESP_OK || !mag_profile.is_calibrated) {
        ESP_LOGW(TAG, "No localized NVS profile found. Falling back to compiled baseline.");
        mag_profile.offset_x = -143.5f;
        mag_profile.offset_y = 85.0f;
        mag_profile.offset_z = 325.0f;
        mag_profile.is_calibrated = true; // Actively use the fallback
    }"""

new_init = """    // ARCHITECT FIX: Load localized Hard-Iron footprint from Flash Memory
    if (eskf_load_calibration(&mag_profile) != ESP_OK || !mag_profile.is_calibrated) {
        ESP_LOGW(TAG, "NVS empty. Provisioning experimental baseline to physical flash memory...");
        mag_profile.offset_x = -143.5f;
        mag_profile.offset_y = 85.0f;
        mag_profile.offset_z = 325.0f;
        mag_profile.is_calibrated = true; 
        
        // Committing the values to NVS so future boots load from storage, not source code
        eskf_save_calibration(&mag_profile);
    }"""

content = content.replace(old_init, new_init)

with open(fusion_cpp, "w") as f:
    f.write(content)

print("Successfully added NVS Factory Provisioning!")