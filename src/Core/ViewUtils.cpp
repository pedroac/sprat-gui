#include "ViewUtils.h"

#ifdef Q_OS_WASM
#include <emscripten.h>

extern "C" bool jsIsAsyncBusy() {
    return EM_ASM_INT({
        var asyncify = (typeof Asyncify !== 'undefined' ? Asyncify : (typeof Module !== 'undefined' ? Module['Asyncify'] : null));
        if (asyncify && asyncify['currData']) {
            var state = asyncify['state'];
            var normal = asyncify['State'] ? asyncify['State']['Normal'] : 0;
            if (state !== undefined && state === normal) {
                return 0;
            }
            return 1;
        }
        // For JSPI / modern Emscripten, we check if there is an active suspension
        if (typeof Module !== 'undefined' && Module['asm'] && Module['asm']['async_status'] && Module['asm']['async_status']() !== 0) {
            return 1;
        }
        return 0;
    }) != 0;
}

extern "C" bool jsHaveAsyncify() {
    return EM_ASM_INT({
        return (typeof Asyncify !== 'undefined' || (typeof Module !== 'undefined' && !!Module['Asyncify'])) ? 1 : 0;
    }) != 0;
}

extern "C" bool jsHaveJspi() {
    return EM_ASM_INT({
        return (typeof WebAssembly !== 'undefined' && typeof WebAssembly.Suspender !== 'undefined') ? 1 : 0;
    }) != 0;
}
#endif
