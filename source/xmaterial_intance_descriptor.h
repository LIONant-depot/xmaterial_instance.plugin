#ifndef XMATERIAL_INSTANCE_INFO_H
#define XMATERIAL_INSTANCE_INFO_H

#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "plugins/xtexture.plugin/source/xtexture_xgpu_rsc_loader.h"
#include "plugins/xmaterial.plugin/source/xmaterial_xgpu_rsc_loader.h"
#include "plugins/xmaterial.plugin/source/xmaterial_params.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_xgpu_rsc_loader.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_resolve.h"

#include <map>

namespace xmaterial_instance
{
    inline static constexpr auto resource_type_guid_v = xresource::type_guid(xresource::guid_generator::Instance64FromString("MaterialInstance"));

    struct default_texture_entry
    {
        std::string         m_Name                  = {};
        int                 m_Index                 = {};
        std::uint64_t       m_GUID                  = {};

        XPROPERTY_DEF
        ( "default_texture_entry", default_texture_entry
        , obj_member<"Name",                &default_texture_entry::m_Name>
        , obj_member<"Index",               &default_texture_entry::m_Index>
        , obj_member<"GUID",                &default_texture_entry::m_GUID>
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

    // A parameter the instance sets (documentation/material_contract.md sections 1, 9): sparse, present = set (even when equal to the default), absent = inherit.
    // Keyed by the parameter id, never its name: a retyped declaration leaves the entry an orphan with its value kept. Values are linear.
    struct override_entry
    {
        xmaterial::param_id     m_ID            = 0;
        xmaterial::param_type   m_Type          = xmaterial::param_type::FLOAT;     // what it was set as
        xmaterial::float4v      m_Value;                                            // everything but TEXTURE
        xrsc::texture_ref       m_Texture       = {};                               // TEXTURE only
        std::string             m_NameHint;                                         // the name when set: carry-over fallback and readable diffs. Not a key

        XPROPERTY_DEF
        ( "override_entry", override_entry
        , obj_member<"ID",          &override_entry::m_ID>
        , obj_member<"Type",        &override_entry::m_Type,    member_enum_span<xmaterial::param_type_enum_v>>
        , obj_member<"Value",       &override_entry::m_Value>
        , obj_member<"Texture",     &override_entry::m_Texture>
        , obj_member<"NameHint",    &override_entry::m_NameHint>
        )
    };
    XPROPERTY_REG(override_entry)

    // v2 (m_DescVersion 2) keeps what the person set in m_Overrides. The Textures, TextureDefaults and FinalTextures of v1 are only read: an old file is turned
    // into overrides when it is read (FoldLegacy) and is written back as v2 on save. The material compiler still writes its template (MaterialInstance.txt) with
    // these lists: the textures of the material in sampler order and the slots it exposes, which material_layout reads.
    struct descriptor : xresource_pipeline::descriptor::base
    {
        XPROPERTY_VDEF
        ( "material_instance_descriptor",   descriptor
        , obj_member<"Version",             &descriptor::m_DescVersion,         member_flags<flags::DONT_SHOW>>
        , obj_member<"MaterialRef",         &descriptor::m_MaterialRef >
        , obj_member<"ParentRef",           &descriptor::m_ParentRef >
        , obj_member<"Overrides",           &descriptor::m_Overrides,           member_flags<flags::DONT_SHOW>>
        , obj_member<"Textures",            &descriptor::m_lTextures,           member_flags<flags::DONT_SHOW>>
        , obj_member<"TextureDefaults",     &descriptor::m_lTextureDefaults,    member_flags<flags::DONT_SHOW>>
        , obj_member<"FinalTextures",       &descriptor::m_lFinalTextures,      member_flags<flags::DONT_SHOW>>
        )

        void SetupFromSource(std::string_view FileName) override
        {

        }

        void Validate(std::vector<std::string>& Errors) const noexcept override
        {
            if (m_MaterialRef.empty())
            {
                Errors.push_back(std::format("The material reference is NULL"));
                return;
            }

            for (auto& E : m_Overrides)
                if (E.m_Type == xmaterial::param_type::TEXTURE && E.m_Texture.empty())
                    Errors.push_back(std::format("Forgot to assign a texture {} ({:016X})", E.m_NameHint, E.m_ID));
        }

        void setupDefaults()
        {
            m_lTextures.resize(m_lTextureDefaults.size());
            for (auto& E : m_lTextureDefaults)
            {
                const auto Index = static_cast<int>(&E - m_lTextureDefaults.data());
                m_lTextures[Index] = m_lFinalTextures[E.m_Index].m_TextureRef;
            }
        }

        const override_entry* Find(xmaterial::param_id ID) const noexcept
        {
            auto It = std::lower_bound(m_Overrides.begin(), m_Overrides.end(), ID, [](const override_entry& O, xmaterial::param_id I) { return O.m_ID < I; });
            return It != m_Overrides.end() && It->m_ID == ID ? &*It : nullptr;
        }

        // The entry of a parameter, made (empty) when there is none; m_Overrides stays sorted by id
        override_entry& Set(xmaterial::param_id ID) noexcept
        {
            auto It = std::lower_bound(m_Overrides.begin(), m_Overrides.end(), ID, [](const override_entry& O, xmaterial::param_id I) { return O.m_ID < I; });
            if (It == m_Overrides.end() || It->m_ID != ID) It = m_Overrides.insert(It, override_entry{ .m_ID = ID });
            return *It;
        }

        bool Clear(xmaterial::param_id ID) noexcept
        {
            const auto* pEntry = Find(ID);
            if (!pEntry) return false;
            m_Overrides.erase(m_Overrides.begin() + (pEntry - m_Overrides.data()));
            return true;
        }

        // v1 -> v2: a slot whose texture is not the one the file's own template holds was assigned by the person: it becomes an override keyed by the slot's input pin
        // guid (the id the material gives that texture), also when the material no longer has it (then it is an orphan, kept). The v1 lists are emptied.
        void FoldLegacy() noexcept
        {
            for (auto& E : m_lTextureDefaults)
            {
                const auto i = static_cast<std::size_t>(&E - m_lTextureDefaults.data());
                if (i >= m_lTextures.size() || E.m_Index < 0 || static_cast<std::size_t>(E.m_Index) >= m_lFinalTextures.size()) continue;
                if (m_lTextures[i] == m_lFinalTextures[E.m_Index].m_TextureRef) continue;
                auto& O = Set(E.m_GUID);
                O.m_Type = xmaterial::param_type::TEXTURE; O.m_Texture = m_lTextures[i]; O.m_NameHint = E.m_Name;
            }
            m_lTextures.clear(); m_lTextureDefaults.clear(); m_lFinalTextures.clear();
        }

        xerr getTemplatePathFromDescriptorPath(std::wstring& MaterialTemplatePath, std::wstring_view FileName) const noexcept
        {
            auto Pos = FileName.rfind(L".lionprj");
            if (Pos != std::wstring::npos) Pos += sizeof(".lionprj");
            else if (Pos = FileName.rfind(L".lionlib"); Pos != std::wstring::npos) Pos += sizeof(".lionlib");
            else xerr::create_f<xresource_pipeline::state, "Unable to locate the project path, fail to load the material instance descriptor">();

            const auto ProjectPath = FileName.substr(0, Pos - 1);
            MaterialTemplatePath = std::format(L"{}/Cache/Resources/Logs/Material/{:02X}/{:02X}/{:X}.log/MaterialInstance.txt"
                , ProjectPath
                , m_MaterialRef.m_Instance.m_Value & 0xff
                , (m_MaterialRef.m_Instance.m_Value & 0xff00) >> 8
                , m_MaterialRef.m_Instance.m_Value
            );

            return {};
        }

        xerr Serialize(bool isReading, std::wstring_view FileName, xproperty::settings::context& Context) noexcept override
        {
            if (not isReading)
            {
                FoldLegacy();
                m_DescVersion = 2;
                std::ranges::sort(m_Overrides, {}, &override_entry::m_ID);      // stable diffs
                return xresource_pipeline::descriptor::base::Serialize(false, FileName, Context);
            }

            m_DescVersion = 1;                                                  // a file with no Version row is v1
            if ( auto Err = xresource_pipeline::descriptor::base::Serialize(true, FileName, Context); Err)
                return Err;

            if (m_DescVersion < 2) FoldLegacy();
            else { m_lTextures.clear(); m_lTextureDefaults.clear(); m_lFinalTextures.clear(); }
            return {};
        }

        int                                 m_DescVersion           = 2;
        xrsc::material_ref                  m_MaterialRef           = {};       // the root material (with a parent: the parent's)
        xrsc::material_instance_ref         m_ParentRef             = {};       // optional: inherit from another instance
        std::vector<override_entry>         m_Overrides             = {};       // sorted by m_ID
        std::vector<default_texture_entry>  m_lTextureDefaults      = {};       // v1 / the material's template only
        std::vector<xrsc::texture_ref>      m_lTextures             = {};
        std::vector<final_texture_entry>    m_lFinalTextures        = {};

    };
    XPROPERTY_VREG(descriptor)

    // What an instance is made from, in the logs of the material's compile: its manifest (where each parameter lives) and its template (its textures in sampler order).
    // The manifest's declarations also list the slots a material exposes through the old way (an exposed texture input, id = its pin guid) so they resolve like parameters.
    // Read by the compiler and the editor, never changed.
    struct material_layout
    {
        xmaterial::manifest m_Manifest;
        descriptor          m_Template;

        // Empty, or why it did not load
        std::string Load(std::wstring_view Project, xrsc::material_ref Material) noexcept
        {
            const auto Folder = std::format(L"{}/Cache/Resources/Logs/Material/{:02X}/{:02X}/{:X}.log", Project
                , Material.m_Instance.m_Value & 0xff, (Material.m_Instance.m_Value & 0xff00) >> 8, Material.m_Instance.m_Value);

            auto Read = [&](const wchar_t* pFile, auto& Into) noexcept
            {
                xtextfile::stream            File;
                xproperty::settings::context C;
                if (auto Err = File.Open(true, Folder + L"/" + pFile, xtextfile::file_type::TEXT); Err) return false;
                return !xproperty::sprop::serializer::Stream(File, Into, C);
            };
            if (!Read(L"MaterialLayout.txt", m_Manifest))     return "the material has no layout yet (compile the material first)";
            if (!Read(L"MaterialInstance.txt", m_Template))   return "the material has no template yet (compile the material first)";

            for (auto& T : m_Template.m_lTextureDefaults)
                if (std::ranges::find(m_Manifest.m_Decls, T.m_GUID, &xmaterial::param_decl::m_ID) == m_Manifest.m_Decls.end())
                {
                    auto& D = m_Manifest.m_Decls.emplace_back();
                    D.m_ID = T.m_GUID; D.m_Name = T.m_Name; D.m_Type = xmaterial::param_type::TEXTURE;
                }
            return {};
        }

        // The slot of a TEXTURE parameter in the material's textures (m_Template.m_lFinalTextures), -1 when the material has none for it
        int TextureIndex(xmaterial::param_id ID) const noexcept
        {
            int Index = -1;
            for (auto& E : m_Manifest.m_Entries) if (E.m_ID == ID && E.m_Type == xmaterial::param_type::TEXTURE) Index = E.m_Sampler - 1;
            for (auto& T : m_Template.m_lTextureDefaults) if (Index < 0 && T.m_GUID == ID) Index = T.m_Index;
            return Index >= 0 && Index < static_cast<int>(m_Template.m_lFinalTextures.size()) ? Index : -1;
        }
    };

    // An instance resolved against its material (xmaterial_instance_resolve.h): the instance, its ancestors read from the descriptors folder (an instance of another
    // library is not found) and the material's layout. The compiler packs the result, the editor lists it.
    struct resolution
    {
        material_layout                                 m_Layout;
        resolved                                        m_Resolved;
        std::map<std::uint64_t, const descriptor*>      m_Descriptors;      // the instances of the chain by key, the one given first
        std::vector<std::unique_ptr<descriptor>>        m_Ancestors;        // the ones read

        static std::wstring DescriptorPath(std::wstring_view Descriptors, std::uint64_t Key) noexcept
        {
            return std::format(L"{}/MaterialInstance/{:02X}/{:02X}/{:X}.desc/Descriptor.txt", Descriptors, Key & 0xff, (Key >> 8) & 0xff, Key);
        }

        // Empty, or why it could not be done. The resolver's own errors (E_CHAIN_*) are in m_Resolved.m_Diags.
        std::string Run(std::wstring_view Project, std::wstring_view Descriptors, std::uint64_t Self, const descriptor& D) noexcept
        {
            if (auto Err = m_Layout.Load(Project, D.m_MaterialRef); !Err.empty()) return Err;

            std::map<std::uint64_t, resolve_node> Nodes;
            auto Add = [&](std::uint64_t Key, const descriptor& X) -> const resolve_node*
            {
                auto& N    = Nodes[Key];
                N.m_Key    = Key;
                N.m_Parent = X.m_ParentRef.m_Instance.m_Value;
                N.m_Root   = X.m_MaterialRef.m_Instance.m_Value;
                for (const auto& O : X.m_Overrides) N.m_Overrides.push_back({ O.m_ID, O.m_Type, O.m_Value });
                std::ranges::sort(N.m_Overrides, {}, &override_value::m_ID);
                m_Descriptors[Key] = &X;
                return &N;
            };
            Add(Self, D);

            std::string Missing;
            m_Resolved = Resolve(m_Layout.m_Manifest, Self, [&](std::uint64_t Key) -> const resolve_node*
            {
                if (auto It = Nodes.find(Key); It != Nodes.end()) return &It->second;
                auto pAncestor = std::make_unique<descriptor>();
                xproperty::settings::context C;
                if (pAncestor->Serialize(true, DescriptorPath(Descriptors, Key), C))
                {
                    Missing = std::format("the parent material instance {:016X} could not be read", Key);
                    return nullptr;
                }
                m_Ancestors.push_back(std::move(pAncestor));
                return Add(Key, *m_Ancestors.back());
            });
            return Missing;
        }
    };

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