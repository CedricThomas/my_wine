# advapi32.dll — 5 functions

All require a custom in-memory registry. No SDL2 equivalent.

## Registry (5)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `RegCreateKeyA` | cosmetic | In-memory key creation | Create node in hashmap (path→key node). Return mock HKEY. |
| `RegOpenKeyA` | cosmetic | Lookup key in hashmap | Return mock HKEY or ERROR_FILE_NOT_FOUND. |
| `RegCloseKey` | cosmetic | Decrement refcount on mock HKEY | Return ERROR_SUCCESS. |
| `RegQueryValueExA` | cosmetic | Lookup value under key | Return ERROR_SUCCESS+data. If not set, return ERROR_FILE_NOT_FOUND. |
| `RegSetValueExA` | cosmetic | Store value under key | Write to hashmap. Return ERROR_SUCCESS. |

**Implementation approach:**
- Single global `struct registry { hashmap<string, node> keys; }` where each node maps value name → { type, data, size }
- HKEY values are `uintptr_t` pointers to nodes or sentinels like `HKEY_CURRENT_USER`
- DOOM95 reads: install path, last resolution, saved options
- DOOM95 writes: settings changes
- Path: `HKEY_CURRENT_USER\Software\Id Software\DOOM`
