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

    // What something made from a material instance (a renderer material: GPU objects with pointers to its textures and its material's shader)
    // holds on to: references of its own to the instance, its material and its textures. The instance's references are shared by everyone who
    // reads it - a getResource on one of them, anyone's (a thumbnail, an editor's preview), moves it to the new data of a resource compiled again
    // and frees the old one under whatever took pointers from it. References of its own keep the data it was made from alive until Release,
    // and say when that data is old (isStale): then it is made again.
    struct held
    {
        xrsc::material_instance_ref     m_Instance;
        xrsc::material_ref              m_Material;
        std::vector<xrsc::texture_ref>  m_Textures;     // the instance's textures in its order (empty: did not load)

        // References of its own to the instance From names and to what it is made of, as loaded now (the instance's own references are
        // moved to the new data first). The instance, or nullptr when it does not load.
        rt* Take(const xrsc::material_instance_ref& From) noexcept
        {
            Release();
            xresource::g_Mgr.CloneRef(m_Instance, From);
            auto* pMI = xresource::g_Mgr.getResource(m_Instance);
            if (pMI == nullptr) return nullptr;
            if (xresource::g_Mgr.getResource(pMI->m_MaterialRef)) xresource::g_Mgr.CloneRef(m_Material, pMI->m_MaterialRef);
            m_Textures.resize(pMI->m_nTexturesList);
            for (auto& E : pMI->getTextures())
                if (xresource::g_Mgr.getResource(E.m_TexureRef)) xresource::g_Mgr.CloneRef(m_Textures[&E - pMI->getTextures().data()], E.m_TexureRef);
            return pMI;
        }

        bool isStale() const noexcept
        {
            if (xresource::g_Mgr.isStale(m_Instance) || xresource::g_Mgr.isStale(m_Material)) return true;
            for (const auto& T : m_Textures) if (xresource::g_Mgr.isStale(T)) return true;
            return false;
        }

        void Release() noexcept
        {
            for (auto& T : m_Textures) xresource::g_Mgr.ReleaseRef(T);
            m_Textures.clear();
            xresource::g_Mgr.ReleaseRef(m_Material);
            xresource::g_Mgr.ReleaseRef(m_Instance);
        }
    };
}

#endif
