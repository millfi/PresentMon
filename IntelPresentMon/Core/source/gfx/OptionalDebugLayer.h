#pragma once
#include <Core/source/win/WinAPI.h>

namespace p2c::gfx
{
    template<class Create>
    HRESULT CreateWithOptionalDebugLayer(bool& debugLayerEnabled, Create&& create)
    {
        auto result = create(debugLayerEnabled);
        if (debugLayerEnabled && result == DXGI_ERROR_SDK_COMPONENT_MISSING) {
            debugLayerEnabled = false;
            result = create(false);
        }
        return result;
    }
}
