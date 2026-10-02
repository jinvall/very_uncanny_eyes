# Quarantine

Files in this directory have been moved out of the active source tree because they are dead code, unused assets, or redundant build artifacts. They are preserved here for reference but are not compiled or loaded by the application.

## Quarantined Files

| File | Original Location | Reason |
|------|-------------------|--------|
| `generated/customDragonEye.h` | `src/generated/customDragonEye.h` | Dead code — never `#include`d by any source file. 1.2 MB of unused generated artwork data. The active custom dragon designs are `customDragonEye_A.h` and `customDragonEye_B.h`. |
| `351dm3rhjd861.jpg` | Project root | Unidentified JPEG (2500x2500), no references in any source or build file. |
| `clipart_iris_source.png` | `eye_conversion/` | 900x507 PNG, no references in any source or build file. |
