#ifndef XMATERIAL_INSTANCE_IMPORT_H
#define XMATERIAL_INSTANCE_IMPORT_H
#pragma once

// Materials imported from a model: a geometry compiler (static or skin) makes one virtual Material Instance per material of the model, with
// virtual Textures for its slots (Compiler/xmaterial_instance_import_compiler.h). This half is what the descriptors and the editors share:
// the settings and the guids of what gets generated, which are derived from the geometry's guid and the material's name so that every
// compile makes the same resources.
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_xgpu_rsc_loader.h"
#include "plugins/xmaterial.plugin/source/xmaterial_xgpu_rsc_loader.h"
#include "plugins/xtexture.plugin/source/xtexture_rsc_descriptor.h"

#include <format>

namespace xmaterial_instance::import
{
    // The PBR materials of the example project (Assets/DefaultAssets/Materials): what a new descriptor starts with
    inline constexpr std::uint64_t default_pbr_material_v            = 0x8D45F86DC5F2C003ull;   // "Default PBR Material"
    inline constexpr std::uint64_t default_pbr_alpha_test_material_v = 0xFF22104E737E58EBull;   // "PBR Alpha Test"

    struct settings
    {
        bool                    m_bImport       = true;          // a new geometry imports the materials of its model
        xrsc::material_ref      m_Material      = { .m_Instance = { default_pbr_material_v } };
        xrsc::material_ref      m_AlphaTest     = { .m_Instance = { default_pbr_alpha_test_material_v } };

        // The compression the generated textures are made with, by what they hold. They are ordinary textures once made (edit one in its own editor): a
        // change here is for the textures made from now on. Textures of a constant (a plain color, a flat normal) are never compressed.
        xtexture_rsc::compression_format_color              m_AlbedoCompression      = xtexture_rsc::compression_format_color::RGB_BC1;
        xtexture_rsc::compression_format_color_a            m_AlbedoAlphaCompression = xtexture_rsc::compression_format_color_a::RGBA_BC7;
        xtexture_rsc::compression_format_tangent_normal     m_NormalCompression      = xtexture_rsc::compression_format_tangent_normal::RG_BC5;
        xtexture_rsc::compression_format_color_a            m_ORMECompression        = xtexture_rsc::compression_format_color_a::RGBA_BC7;

        // (the enums of the subsets have the values of the texture's own compression_format)
        xtexture_rsc::compression_format Albedo()      const noexcept { return static_cast<xtexture_rsc::compression_format>(m_AlbedoCompression); }
        xtexture_rsc::compression_format AlbedoAlpha() const noexcept { return static_cast<xtexture_rsc::compression_format>(m_AlbedoAlphaCompression); }
        xtexture_rsc::compression_format Normal()      const noexcept { return static_cast<xtexture_rsc::compression_format>(m_NormalCompression); }
        xtexture_rsc::compression_format ORME()        const noexcept { return static_cast<xtexture_rsc::compression_format>(m_ORMECompression); }

        void Validate(std::vector<std::string>& Errors) const noexcept
        {
            if (!m_bImport) return;
            if (m_Material.empty())  Errors.push_back("Import Materials: pick the PBR material the imported materials are made from");
            if (m_AlphaTest.empty()) Errors.push_back("Import Materials: pick the PBR material used by the imported materials that cut out (alpha test)");
        }

        XPROPERTY_DEF
        ( "import_materials", settings
        , obj_member<"bImport",     &settings::m_bImport,   member_help<"Import the materials and textures of the model: the compiler makes a Material Instance (and its Textures) for each material of the model. A material slot can still take your own instance: it is then an override (the arrow beside it dismisses it)">>
        , obj_member<"Material",    &settings::m_Material,  member_help<"The PBR material the imported instances are made from (slots SamplerAlbedo, SamplerNormal, SamplerORME)">>
        , obj_member<"AlphaTest",   &settings::m_AlphaTest, member_help<"The PBR material for the imported materials that cut out (an opacity map, or a color map with an alpha channel)">>
        , obj_member<"AlbedoCompression",      &settings::m_AlbedoCompression,      member_enum_span<xtexture_rsc::compression_format_color_v>,          member_help<"The compression of the color maps (no alpha). For the textures made from now on">>
        , obj_member<"AlbedoAlphaCompression", &settings::m_AlbedoAlphaCompression, member_enum_span<xtexture_rsc::compression_format_color_a_v>,        member_help<"The compression of the color maps that cut out (color and alpha). For the textures made from now on">>
        , obj_member<"NormalCompression",      &settings::m_NormalCompression,      member_enum_span<xtexture_rsc::compression_format_tangent_normal_v>, member_help<"The compression of the normal maps. For the textures made from now on">>
        , obj_member<"ORMECompression",        &settings::m_ORMECompression,        member_enum_span<xtexture_rsc::compression_format_color_a_v>,        member_help<"The compression of the occlusion/roughness/metalness/emission maps (packed in one texture). For the textures made from now on">>
        )
    };
    XPROPERTY_REG(settings)

    // The generated Material Instance of one material of the model
    inline xrsc::material_instance_ref InstanceRef(xresource::instance_guid Owner, std::string_view MaterialName) noexcept
    {
        return { .m_Instance = xresource::instance_guid::GenerateGUIDCopy(std::format("{:016X}_material_{}", Owner.m_Value, MaterialName).c_str()) };
    }

    // The slots of a geometry: Names[i] is the material the slot Refs[i] is for. With the import on, an empty slot takes the imported instance;
    // with it off, a slot that still has the imported one is emptied. Returns whether anything changed.
    template<typename T_NAME_LIST>
    bool SyncSlots(const settings& S, xresource::instance_guid Owner, const T_NAME_LIST& Names, std::vector<xrsc::material_instance_ref>& Refs) noexcept
    {
        bool bChanged = false;
        for (std::size_t i = 0; i < Refs.size() && i < Names.size(); ++i)
        {
            const auto Imported = InstanceRef(Owner, Names[i]);
            if      ( S.m_bImport && Refs[i].empty())     { Refs[i] = Imported; bChanged = true; }
            else if (!S.m_bImport && Refs[i] == Imported) { Refs[i] = {};       bChanged = true; }
        }
        return bChanged;
    }

    // A slot with an instance of the user's own while the import is on
    inline bool isOverride(const settings& S, xresource::instance_guid Owner, std::string_view MaterialName, const xrsc::material_instance_ref& Ref) noexcept
    {
        return S.m_bImport && !Ref.empty() && Ref != InstanceRef(Owner, MaterialName);
    }
}

#endif
