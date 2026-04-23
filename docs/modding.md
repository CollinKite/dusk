# Dusk Mod API

Mods are shared libraries packaged into a `.dusk` zip archive. The loader scans the `mods/` directory at startup, extracts each library, and calls your exports each frame.

## Table of Contents

1. [Getting Started](#getting-started)
2. [mod.json](#modjson)
3. [Required Exports](#required-exports)
4. [DuskModAPI Reference](#duskmodapi-reference)
5. [Logging](#logging)
6. [Loading Resources](#loading-resources)
7. [ImGui Integration](#imgui-integration)
8. [Hooking Game Functions](#hooking-game-functions)
   - [Pre-hooks](#pre-hooks)
   - [Post-hooks](#post-hooks)
   - [Replace hooks](#replace-hooks)
   - [Reading and writing arguments](#reading-and-writing-arguments)
9. [Full Example](#full-example)

---

## Getting Started

Fork the [mod template](../tools/mod_template/), it is a self-contained CMake project that references dusk as a subdirectory.

```
my_mod/
├── CMakeLists.txt
├── mod.json
├── src/mod.cpp
└── res/ (optional bundled resources)
```

**CMakeLists.txt:**

```cmake
cmake_minimum_required(VERSION 3.25)
project(my_mod CXX)

set(DUSK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dusk" CACHE PATH "Path to dusk source root")
add_subdirectory("${DUSK_DIR}" dusk EXCLUDE_FROM_ALL)

add_dusk_mod(my_mod
    SOURCES  src/mod.cpp
    MOD_JSON mod.json
    RES_DIR  res    # optional
)
```

After building, `my_mod.dusk` is placed in `mods/` next to the project root (`DUSK_MODS_OUTPUT_DIR` cache variable). Copy it to the game's `mods/` folder and launch.

The `.dusk` archive is a standard zip containing `mod.json`, the compiled library, and an optional `res/` tree. `add_dusk_mod()` creates it automatically.

---

## mod.json

```json
{
    "name":        "My Mod",
    "version":     "1.0.0",
    "author":      "Your Name",
    "description": "A short description shown in the mod manager."
}
```

All fields are optional but recommended. `name` falls back to the filename, `version` to `"?"`.

---

## Required Exports

```cpp
#include "dusk/mod_api.h"

extern "C" {

void mod_init   (DuskModAPI* api);  // required, called once at startup
void mod_tick   (DuskModAPI* api);  // required, called every frame
void mod_cleanup(DuskModAPI* api);  // optional, called on shutdown

}
```

---

## DuskModAPI Reference

The `api` pointer is valid for the lifetime of the mod. When using `hook.hpp`, call `dusk::init(api)` once and `dusk::g_api` is set for you.

| Field | Description |
|-------|-------------|
| `api_version` | ABI version, check against `DUSK_MOD_API_VERSION` if needed |
| `mod_dir` | Absolute path to the extracted mod cache directory |
| `log_info` / `log_warn` / `log_error` | `printf`-style logging, prefixed with the mod name |
| `load_resource` / `free_resource` | Load files from the `res/` tree in the `.dusk` archive |
| `register_tab_content` | Add a panel to the mod manager's per-mod tab |
| `register_menu_item` | Add an item to the quick-access menu |
| `hook_dispatch_pre` / `hook_dispatch_post` | Called by the trampoline, do not call directly |

---

## Logging

```cpp
api->log_info("Player health: %d", hp);
api->log_warn("Something looks wrong");
api->log_error("Fatal: %s", msg);
```

Output appears in the dusk console as `[My Mod] ...`

The format string is `printf`-compatible.

---

## Loading Resources

```cpp
size_t size = 0;
void* data = api->load_resource("config.txt", &size);
if (data) {
    std::string text(static_cast<char*>(data), size);
    api->free_resource(data);
}
```

- Path is relative to `res/`, pass `"config.txt"` not `"res/config.txt"`
- Always call `free_resource`, the buffer is owned by miniz
- For writable storage, write files under `api->mod_dir`

---

## ImGui Integration

**Tab content:** shown in the mod's panel in the Mods window, called every frame while visible:

```cpp
static void DrawPanel(void* userdata) {
    ImGui::Text("Hello!");
}
api->register_tab_content(DrawPanel, nullptr);
```

Pass a pointer through `userdata` if your callback needs state:

```cpp
api->register_tab_content(DrawPanel, &g_state);
```

**Menu items:** added to the quick-access menu. Use `ImGui::MenuItem`, `ImGui::Separator`, etc.:

```cpp
static void DrawMenuEntry(void*) {
    if (ImGui::MenuItem("Reset rotation")) { ... }
}
api->register_menu_item(DrawMenuEntry, nullptr);
```

---

## Hooking Game Functions

Call `dusk::init(api)` first.

```cpp
#include "dusk/hook.hpp"

extern "C" void mod_init(DuskModAPI* api) {
    dusk::init(api);
    dusk::hookAddPre<&ClassName::Method>(callback);
}
```

The trampoline is installed once per address. Multiple mods can register pre/post callbacks for the same function independently.

### Pre-hooks

Run before the original. Return `0` to let it proceed, non-zero to cancel it. Post-hooks still run either way.

```cpp
static int32_t on_posMove_pre(void* args) {
    daAlink_c* link = dusk::arg<daAlink_c*>(args, 0);  // this
    if (link->shape_angle.y > 10000)
        return 1;  // cancel
    return 0;
}
dusk::hookAddPre<&daAlink_c::posMove>(on_posMove_pre);
```

### Post-hooks

Run after the original (or replace-hook).

```cpp
static void on_posMove_post(void* args) {
    daAlink_c* link = dusk::arg<daAlink_c*>(args, 0);
    dusk::g_api->log_info("New Y angle: %d", (int)link->shape_angle.y);
}
dusk::hookAddPost<&daAlink_c::posMove>(on_posMove_post);
```

### Replace hooks

Completely substitutes the original. Only one replace-hook per function, a second install overwrites with a warning.

```cpp
static void on_posMove_replace(void* args) {
    daAlink_c* link = dusk::arg<daAlink_c*>(args, 0);
    link->shape_angle.y += 100;
}
dusk::hookSetReplace<&daAlink_c::posMove>(on_posMove_replace);
```

To call the original from inside a replace-hook:

```cpp
using Entry = dusk::HookEntry<&daAlink_c::posMove>;

static void on_posMove_replace(void* args) {
    daAlink_c* link = dusk::arg<daAlink_c*>(args, 0);
    link->shape_angle.y = 0;
    Entry::g_orig(link);
}
```

### Reading and writing arguments

`args` is a `void*[N]` array. Index `0` is `this`, subsequent indices are parameters in declaration order.

```cpp
T   value = dusk::arg   <T>(args, n);  // copy
T&  ref   = dusk::argRef<T>(args, n);  // reference (read/write)
```

**Example:** halve incoming damage

```cpp
// void daEnemy_c::takeDamage(int amount, daActor_c* source)
static int32_t on_takeDamage_pre(void* args) {
    dusk::argRef<int>(args, 1) /= 2;
    return 0;
}
dusk::hookAddPre<&daEnemy_c::takeDamage>(on_takeDamage_pre);
```

For reference parameters (e.g. `const cXyz& pos`), use `argRef<cXyz>` to get a direct reference.


---

## Full Example

```cpp
#include "d/actor/d_a_alink.h"
#include "dusk/hook.hpp"
#include "dusk/mod_api.h"
#include "imgui.h"
#include "m_Do/m_Do_controller_pad.h"

static int g_ticks = 0;

static int32_t on_posMove_pre(void* args) {
    daAlink_c* link = dusk::arg<daAlink_c*>(args, 0);
    if (mDoCPd_c::getHoldR(PAD_1)) {
        link->shape_angle.y -= 2048;
    }
    return 0;
}

static void DrawPanel(void*) {
    daAlink_c* link = daAlink_getAlinkActorClass();
    ImGui::Text("Ticks: %d", g_ticks);
    if (link) {
        ImGui::Text("Y angle: %d", (int)link->shape_angle.y);
        if (ImGui::Button("Reset rotation")) {
            link->shape_angle.y = 0;
        }
    }
}

static void DrawMenuEntry(void*) {
    daAlink_c* link = daAlink_getAlinkActorClass();
    if (ImGui::MenuItem("Reset rotation", nullptr, false, link != nullptr)) {
        link->shape_angle.y = 0;
    }
}

extern "C" {

void mod_init(DuskModAPI* api) {
    dusk::init(api);
    dusk::hookAddPre<&daAlink_c::posMove>(on_posMove_pre);
    api->register_tab_content(DrawPanel, nullptr);
    api->register_menu_item(DrawMenuEntry, nullptr);
}

void mod_tick(DuskModAPI* api) {
    ++g_ticks;
}

void mod_cleanup(DuskModAPI* api) {
    api->log_info("Unloaded after %d ticks.", g_ticks);
}
}
```
