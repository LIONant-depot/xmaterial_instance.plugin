#ifndef XMATERIAL_INSTANCE_INFO_H
#define XMATERIAL_INSTANCE_INFO_H

#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "Plugins/xtexture.plugin/source/xtexture_xgpu_rsc_loader.h"
#include "Plugins/xmaterial.plugin/source/xmaterial_xgpu_rsc_loader.h"

namespace xmaterial_instance
{
    struct intance_entry
    {
        std::string         m_Name                  = {};
        xrsc::texture_ref   m_DefaultTextureRef     = {};
        xrsc::texture_ref   m_TextureRef            = {};
        int                 m_Index                 = {};

        XPROPERTY_DEF
        ("material_instance_entry", intance_entry
        , obj_member<"Name",                &intance_entry::m_Name,                 member_flags<flags::DONT_SHOW>>
        , obj_member<"DefaultTextureRef",   &intance_entry::m_DefaultTextureRef,    member_flags<flags::DONT_SHOW>>
        , obj_member<"TextureRef",          &intance_entry::m_TextureRef>
        , obj_member<"Index",               &intance_entry::m_Index,                member_flags<flags::SHOW_READONLY> >
        )
    };
    XPROPERTY_REG(intance_entry)

    struct descriptor
    {
        XPROPERTY_DEF
        ( "material_instance_descriptor", descriptor
        , obj_member<"MaterialRef", &descriptor::m_MaterialRef >
        , obj_member<"Textures",    &descriptor::m_lTextures >
        )

        xrsc::material_ref          m_MaterialRef   = {};
        std::vector<intance_entry>  m_lTextures     = {};
    };
    XPROPERTY_REG(descriptor)
}

#endif