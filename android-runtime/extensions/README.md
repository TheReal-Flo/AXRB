# Runtime Extension Policy

Do not add vendor extension behavior until the base OpenXR lifecycle works with first-party samples.

Unsupported extensions should be handled explicitly:

- log the extension name
- return a safe OpenXR result
- avoid crashing the calling APK
- document any dummy behavior here
