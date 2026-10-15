#ifndef XMATERIAL_INSTANCE_IMPORT_H
#define XMATERIAL_INSTANCE_IMPORT_H
#pragma once

// Materials imported from a model: a geometry compiler (static or skin) makes one virtual Material Instance per material of the model, with
// virtual Textures for its slots (Compiler/xmaterial_instance_import_compiler.h). This half is what the descriptors and the editors share:
// the settings and the guids of what gets generated, which are derived from the geometry's guid and the material's name so that every
// compile makes the same resources.
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_xgpu_rsc_loader.h"
#include "plugins/xmaterial.plugin/source/xmaterial_xgpu_rsc_loader.h"

#include <format>

namespace xmaterial_instance::import
{
    // The PBR materials of the example project (Assets/DefaultAssets/Materials): what a new descriptor starts with
    inline constexpr std::uint64_t default_pbr_material_v            = 0x8D45F86DC5F2C003ull;   // "Default PBR Material"
    inline constexpr std::uint64_t default_pbr_alpha_test_material_v = 0xFF22104E737E58EBull;   // "PBR Alpha Test"

    struct settings
    {
        bool                    m_bImport       = false;
        xrsc::material_ref      m_Material      = { .m_Instance = { default_pbr_material_v } };
        xrsc::material_ref      m_AlphaTest     = { .m_Instance = { default_pbr_alpha_test_material_v } };

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
