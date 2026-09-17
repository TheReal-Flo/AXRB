"""Legacy entry point. Game-library rewriting has been removed.

Only emulator-side configuration is applied. Unsupported ARM instructions still
require a translator fix; this entry point does not claim to provide one.
"""
from android_runtime_policy import main

if __name__ == "__main__": main()
