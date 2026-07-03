import os

def fix_cmake():
    print("1. Updating CMakeLists.txt for C++ compilation...")
    cmake_path = os.path.normpath("main/CMakeLists.txt")
    
    if os.path.exists(cmake_path):
        with open(cmake_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Tell the build system to look for the new C++ file instead of the old C file
        if "app_main.c" in content:
            content = content.replace("app_main.c", "app_main.cpp")
            with open(cmake_path, 'w', encoding='utf-8') as f:
                f.write(content)
            print(" -> Success: CMakeLists.txt updated to target app_main.cpp.")
        elif "app_main.cpp" in content:
            print(" -> CMakeLists.txt is already targeting app_main.cpp.")
        else:
            print(" -> WARNING: Could not find explicit app_main.c reference. Check your CMakeLists.txt manually.")
    else:
        print(" -> ERROR: main/CMakeLists.txt not found!")

if __name__ == "__main__":
    fix_cmake()