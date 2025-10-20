#ifndef XMATERIAL_INSTANCE_INFO_H
#define XMATERIAL_INSTANCE_INFO_H

#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "Plugins/xtexture.plugin/source/xtexture_xgpu_rsc_loader.h"
#include "Plugins/xmaterial.plugin/source/xmaterial_xgpu_rsc_loader.h"

namespace xmaterial_instance
{
    inline static constexpr auto resource_type_guid_v = xresource::type_guid(xresource::guid_generator::Instance64FromString("MaterialInstance"));

    struct default_texture_entry
    {
        std::string         m_Name                  = {};
        int                 m_Index                 = {};

        XPROPERTY_DEF
        ( "default_texture_entry", default_texture_entry
        , obj_member<"Name",                &default_texture_entry::m_Name>
        , obj_member<"Index",               &default_texture_entry::m_Index>
        )
    };
    XPROPERTY_REG(default_texture_entry)

    struct final_texture_entry
    {
        xrsc::texture_ref   m_TextureRef        = {};

        XPROPERTY_DEF
        ( "final_texture_entry", final_texture_entry
        , obj_member<"TextureRef",          &final_texture_entry::m_TextureRef>
        )
    };
    XPROPERTY_REG(final_texture_entry)

    struct descriptor : xresource_pipeline::descriptor::base
    {
        XPROPERTY_VDEF
        ( "material_instance_descriptor",   descriptor
        , obj_member<"MaterialRef",         &descriptor::m_MaterialRef >
        , obj_member<"Textures",            &descriptor::m_lTextures, member_ui_open<true>>
        , obj_member<"TextureDefaults",     &descriptor::m_lTextureDefaults, member_flags<flags::DONT_SHOW>>
        , obj_member<"FinalTextures",       &descriptor::m_lFinalTextures, member_flags<flags::DONT_SHOW>>
        )

        void SetupFromSource(std::string_view FileName)
        {
            
        }

        void Validate(std::vector<std::string>& Errors) const noexcept override
        {
        }

        xrsc::material_ref                  m_MaterialRef           = {};
        std::vector<default_texture_entry>  m_lTextureDefaults      = {};
        std::vector<xrsc::texture_ref>      m_lTextures             = {};
        std::vector<final_texture_entry>    m_lFinalTextures        = {};

    };
    XPROPERTY_VREG(descriptor)


    //--------------------------------------------------------------------------------------

    struct factory final : xresource_pipeline::factory_base
    {
        using xresource_pipeline::factory_base::factory_base;

        std::unique_ptr<xresource_pipeline::descriptor::base> CreateDescriptor(void) const noexcept override
        {
            return std::make_unique<descriptor>();
        };

        xresource::type_guid ResourceTypeGUID(void) const noexcept override
        {
            return resource_type_guid_v;
        }

        const char* ResourceTypeName(void) const noexcept override
        {
            return "Material Instance";
        }

        const xproperty::type::object& ResourceXPropertyObject(void) const noexcept override
        {
            return *xproperty::getObjectByType<descriptor>();
        }
    };

    inline static factory g_Factory{};
}

#endif