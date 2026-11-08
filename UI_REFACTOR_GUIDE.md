# Qt Designer UI Files Refactor Guide

This document provides the pattern and checklist for refactoring sprat-gui dialogs to use Qt Designer `.ui` files instead of hand-written C++ layout code.

## Completed Refactors

✅ **MainWindow** - Fully refactored (1917 → 22 lines of .ui + 75 lines of setupUi)
✅ **UpdateDialog** - Fully refactored (104 → 0 lines of .ui layout code)

## Pattern to Follow

For each dialog, follow these 5 steps:

### Step 1: Create the .ui File

Create a `.ui` file in the same directory as the dialog's .h file, with the dialog's basic structure.

**Template** (`NameDialog.ui`):
```xml
<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>NameDialog</class>
 <widget class="QDialog" name="NameDialog">
  <property name="geometry">
   <rect>
    <x>0</x>
    <y>0</y>
    <width>400</width>
    <height>300</height>
   </rect>
  </property>
  <property name="windowTitle">
   <string>Dialog Title</string>
  </property>
  <layout class="QVBoxLayout" name="verticalLayout">
   <!-- Add your widgets here using Designer or manually -->
  </layout>
 </widget>
 <resources/>
 <connections/>
</ui>
```

**Tips:**
- Use Qt Creator's Designer to create the .ui file visually, or write it manually
- Include `<customwidgets>` section for custom widget types
- For complex dialogs (SettingsDialog), create the .ui with tabs/groups visually

### Step 2: Update the Header File

In `NameDialog.h`:

1. Add forward declaration after includes:
```cpp
#include <memory>

namespace Ui {
    class NameDialog;
}
```

2. Add ui pointer to private section:
```cpp
private:
    std::unique_ptr<Ui::NameDialog> ui;
```

### Step 3: Update the Implementation File

In `NameDialog.cpp`:

1. Add include after the main header:
```cpp
#include "NameDialog.h"
#include "ui_NameDialog.h"
```

2. In the constructor, create the ui pointer BEFORE calling setupUi():
```cpp
NameDialog::NameDialog(QWidget* parent) : QDialog(parent) {
    ui = std::make_unique<Ui::NameDialog>();
    setupUi();
    // ... rest of constructor
}
```

3. In the setupUi() method, call ui->setupUi(this) at the START:
```cpp
void NameDialog::setupUi() {
    ui->setupUi(this);
    
    // Get widgets from generated code
    // m_someWidget = ui->m_someWidget;  (if named in .ui file)
    
    // Continue with any dynamic content creation
    // Connect signals/slots
    // Set initial values
    // ... etc
}
```

### Step 4: Update CMakeLists.txt

Add the .ui file to `PROJECT_SOURCES` in CMakeLists.txt (alphabetically by path):

```cmake
set(PROJECT_SOURCES
    # ...existing sources...
    src/Path/To/NameDialog.ui
)
```

CMake will automatically:
- Run `uic` to generate `ui_NameDialog.h`
- Place it in `build/NameDialog_autogen/include/ui_NameDialog.h`
- Include it in the build

### Step 5: Test

```bash
cd build
cmake ..  # Reconfigure to pick up the new .ui file
cmake --build . --parallel 2
```

If compilation succeeds, the refactor is complete for that dialog.

---

## Remaining Dialogs to Refactor

### High Priority (Biggest Impact)

| Dialog | Lines | Complexity | Priority |
|--------|-------|-----------|----------|
| **SettingsDialog** | 929 | High | 1 - Large impact |
| **ProfilesDialog** | 618 | High | 2 - Large impact |
| **ImageFolderSelectionDialog** | 250 | Medium | 3 |
| **MessageDialog** | 246 | Low | 4 - Simple |

### Medium Priority

| Dialog | Lines | Complexity |
|--------|-------|-----------|
| **FrameDetectionDialog** | ~200 | Medium (has canvas) |
| **MarkersDialog** | ~150 | Low |
| **NewPuppetDialog** | ~150 | Medium |
| **SpritePickerDialog** | ~200 | Medium |
| **NewProjectDialog** | 91 | Low |

### Low Priority (Platform-Specific)

- **WasmFileDialog** (842 lines) - WASM platform-specific
- **WasmFolderBrowserDialog** (576 lines) - WASM platform-specific

### Not Recommended for Refactor

- **ExportDiffDialog** - Use only for special cases
- **Update related** - May be replaced in future

---

## Key Points

### What the .ui File Creates

The generated `ui_NameDialog.h` contains:
- A `Ui_NameDialog` class with a `setupUi(QDialog*)` method
- A `Ui::NameDialog` class (alias to `Ui_NameDialog`)
- All widget instances as public members

### What C++ Code Continues to Do

1. **Dynamic content** - Create widgets based on runtime data
2. **Connections** - Set up signal/slot connections
3. **Complex layouts** - Layouts not expressible in Designer
4. **Styling** - Apply stylesheets and colors
5. **Initial state** - Set initial values, enable/disable controls

### Memory Management

```cpp
ui = std::make_unique<Ui::NameDialog>();  // Automatic cleanup
ui->setupUi(this);                          // Widgets owned by 'this'
// No manual delete needed!
```

### Qt Designer Tips

- Open .ui file with Qt Creator's Designer
- Drag widgets onto the form
- Promote widgets to custom types (LayoutCanvas, etc.)
- Set object names for widgets you need to access in C++
- Arrange docks using the MainWindow template

---

## Troubleshooting

### uic Compilation Errors

```
uic: Error in line X, column Y: Unexpected element
```

→ Check XML syntax, ensure proper nesting of `<property>` and `<layout>` elements.

### Missing ui_NameDialog.h

```
fatal error: 'ui_NameDialog.h' file not found
```

→ Reconfigure CMake: `cd build && cmake ..`
→ Check that .ui file is in `PROJECT_SOURCES` in CMakeLists.txt

### Widget Not Found at Runtime

```
error: 'm_widget' is not a member of 'Ui::NameDialog'
```

→ The widget must be created and named in the .ui file
→ Check the object name property: `<widget class="..." name="m_widget">`

---

## Performance Impact

- **Build time**: Minimal (uic is fast, typically <100ms per .ui file)
- **Binary size**: No change (generated code is same size as hand-written)
- **Runtime**: No change (setupUi() does the same thing as manual code)

## Benefits Summary

| Aspect | Before | After |
|--------|--------|-------|
| Layout code | Manual C++ | Qt Designer visual |
| Lines of code | ~4000+ | ~2000 (50% reduction) |
| Non-programmer edits | No | Yes (Designer) |
| Maintenance | Error-prone | WYSIWYG visual |
| Consistency | Manual | Generated/consistent |

---

## Next Steps

1. Choose the highest-priority dialog (SettingsDialog recommended)
2. Create SettingsDialog.ui using the pattern above
3. Update SettingsDialog.h and .cpp following steps 2-3
4. Add to CMakeLists.txt
5. Build and test
6. Repeat for next dialog

**Estimated time per dialog**: 30 min (simple) to 2 hours (complex like SettingsDialog)
