#ifndef XMATERIAL_INSTANCE_RUNTIME_H
#define XMATERIAL_INSTANCE_RUNTIME_H

#include "xmaterial_instance_data_file.h"
#include "dependencies/xresource_mgr/source/xresource_mgr.h"

namespace xmaterial_instance
{
    struct rt : data_file
    {
        // Was something this material instance is made of compiled again (xresource::mgr::MarkUpdated): the instance itself, its material
        // (a shader it includes changed) or one of its textures? Whoever made something from them (a renderer material) makes it again:
        // getResource on the references then moves them to the new data.
        static bool isStale(const xresource::full_guid& Ref) noexcept
        {
            if (!Ref.m_Instance.isValid() || !Ref.m_Instance.isPointer()) return false;
            if (xresource::g_Mgr.isStale(Ref)) return true;
            auto& MI = *static_cast<rt*>(Ref.m_Instance.m_Pointer);
            if (xresource::g_Mgr.isStale(MI.m_MaterialRef)) return true;
            for (const auto& E : MI.getTextures()) if (xresource::g_Mgr.isStale(E.m_TexureRef)) return true;
            return false;
        }
    };
}

#endif
