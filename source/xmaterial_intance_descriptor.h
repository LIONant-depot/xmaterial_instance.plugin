#ifndef XMATERIAL_INSTANCE_INFO_H
#define XMATERIAL_INSTANCE_INFO_H

#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "plugins/xtexture.plugin/source/xtexture_xgpu_rsc_loader.h"
#include "plugins/xmaterial.plugin/source/xmaterial_xgpu_rsc_loader.h"
#include "plugins/xmaterial.plugin/source/xmaterial_params.h"
#include "plugins/xmaterial.plugin/source/xmaterial_render_settings.h"     // the system parameters (Double Sided, Alpha Cutoff)
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

    // One parameter of the material as the instance's inspector shows it (descriptor::m_Params, in the material's order): its name, then its value through a view of
    // its type (sprop_union_group: one row). Filled by the editor from the resolved instance (the value here, a parent's or the default), never saved; an edit becomes
    // a SetOverride. Values are linear.
    struct param_row
    {
        std::string             m_Name;
        xmaterial::param_id     m_ID        = 0;                // not shown: what the commands name it by
        xmaterial::param_type   m_Type      = xmaterial::param_type::FLOAT;
        bool                    m_bAlpha    = true;             // COLOR: its alpha means something (else an RGB row)
        bool                    m_bToggle   = false;            // FLOAT 0 or 1 shown as a checkbox (Double Sided)
        xmaterial::float4v      m_V;
        xrsc::texture_ref       m_Texture;

        static std::pair<const xproperty::type::object*, void*> ValueView(param_row& O) noexcept;

        XPROPERTY_DEF
        ( "param_row", param_row, xproperty::settings::sprop_union_group
        , obj_member<"Name",  &param_row::m_Name, member_flags<flags::SHOW_READONLY>>
        , obj_member<"Value", +[](param_row& O) -> std::pair<const xproperty::type::object*, void*> { return ValueView(O); }>
        )
    };
    XPROPERTY_REG(param_row)

    // The sides of param_row's value: each is the row itself, seen as one type
    struct param_float_view   : param_row { XPROPERTY_DEF("param: float",   param_float_view,   obj_member<"Value", +[](param_float_view& O, bool bRead, float& V) { if (bRead) V = O.m_V.m_X; else O.m_V.m_X = V; }>) };
    struct param_float2_view  : param_row { XPROPERTY_DEF("param: float2",  param_float2_view,  xproperty::settings::vector2_group
        , obj_member<"X", +[](param_float2_view& O, bool bRead, float& V) { if (bRead) V = O.m_V.m_X; else O.m_V.m_X = V; }>
        , obj_member<"Y", +[](param_float2_view& O, bool bRead, float& V) { if (bRead) V = O.m_V.m_Y; else O.m_V.m_Y = V; }>) };
    struct param_float3_view  : param_row { XPROPERTY_DEF("param: float3",  param_float3_view,  xproperty::settings::vector3_group
        , obj_member<"X", +[](param_float3_view& O, bool bRead, float& V) { if (bRead) V = O.m_V.m_X; else O.m_V.m_X = V; }>
        , obj_member<"Y", +[](param_float3_view& O, bool bRead, float& V) { if (bRead) V = O.m_V.m_Y; else O.m_V.m_Y = V; }>
        , obj_member<"Z", +[](param_float3_view& O, bool bRead, float& V) { if (bRead) V = O.m_V.m_Z; else O.m_V.m_Z = V; }>) };
    struct param_float4_view  : param_row { XPROPERTY_DEF("param: float4",  param_float4_view,  xproperty::settings::vector4_group
        , obj_member<"X", +[](param_float4_view& O, bool bRead, float& V) { if (bRead) V = O.m_V.m_X; else O.m_V.m_X = V; }>
        , obj_member<"Y", +[](param_float4_view& O, bool bRead, float& V) { if (bRead) V = O.m_V.m_Y; else O.m_V.m_Y = V; }>
        , obj_member<"Z", +[](param_float4_view& O, bool bRead, float& V) { if (bRead) V = O.m_V.m_Z; else O.m_V.m_Z = V; }>
        , obj_member<"W", +[](param_float4_view& O, bool bRead, float& V) { if (bRead) V = O.m_V.m_W; else O.m_V.m_W = V; }>) };
    struct param_color3_view  : param_row { XPROPERTY_DEF("param: color3",  param_color3_view,  xproperty::settings::color3_group
        , obj_member<"R", +[](param_color3_view& O, bool bRead, float& V) { if (bRead) V = O.m_V.m_X; else O.m_V.m_X = V; }>
        , obj_member<"G", +[](param_color3_view& O, bool bRead, float& V) { if (bRead) V = O.m_V.m_Y; else O.m_V.m_Y = V; }>
        , obj_member<"B", +[](param_color3_view& O, bool bRead, float& V) { if (bRead) V = O.m_V.m_Z; else O.m_V.m_Z = V; }>) };
    struct param_color4_view  : param_row { XPROPERTY_DEF("param: color4",  param_color4_view,  xproperty::settings::color4_group
        , obj_member<"R", +[](param_color4_view& O, bool bRead, float& V) { if (bRead) V = O.m_V.m_X; else O.m_V.m_X = V; }>
        , obj_member<"G", +[](param_color4_view& O, bool bRead, float& V) { if (bRead) V = O.m_V.m_Y; else O.m_V.m_Y = V; }>
        , obj_member<"B", +[](param_color4_view& O, bool bRead, float& V) { if (bRead) V = O.m_V.m_Z; else O.m_V.m_Z = V; }>
        , obj_member<"A", +[](param_color4_view& O, bool bRead, float& V) { if (bRead) V = O.m_V.m_W; else O.m_V.m_W = V; }>) };
    struct param_toggle_view  : param_row { XPROPERTY_DEF("param: toggle",  param_toggle_view,  obj_member<"Value", +[](param_toggle_view& O, bool bRead, bool& V) { if (bRead) V = O.m_V.m_X > 0.5f; else O.m_V.m_X = V ? 1.0f : 0.0f; }>) };
    struct param_texture_view : param_row { XPROPERTY_DEF("param: texture", param_texture_view, obj_member<"Value", &param_row::m_Texture>) };
    XPROPERTY_REG(param_float_view)
    XPROPERTY_REG(param_float2_view)
    XPROPERTY_REG(param_float3_view)
    XPROPERTY_REG(param_float4_view)
    XPROPERTY_REG(param_color3_view)
    XPROPERTY_REG(param_color4_view)
    XPROPERTY_REG(param_texture_view)
    XPROPERTY_REG(param_toggle_view)

    inline std::pair<const xproperty::type::object*, void*> param_row::ValueView(param_row& O) noexcept
    {
        using T = xmaterial::param_type;
        switch (O.m_Type)
        {
        case T::FLOAT:   return O.m_bToggle ? std::pair<const xproperty::type::object*, void*>{ xproperty::getObjectByType<param_toggle_view>(), &O }
                                            : std::pair<const xproperty::type::object*, void*>{ xproperty::getObjectByType<param_float_view>(), &O };
        case T::FLOAT2:  return { xproperty::getObjectByType<param_float2_view>(),  &O };
        case T::FLOAT3:  return { xproperty::getObjectByType<param_float3_view>(),  &O };
        case T::FLOAT4:  return { xproperty::getObjectByType<param_float4_view>(),  &O };
        case T::COLOR:   return O.m_bAlpha ? std::pair<const xproperty::type::object*, void*>{ xproperty::getObjectByType<param_color4_view>(), &O }
                                           : std::pair<const xproperty::type::object*, void*>{ xproperty::getObjectByType<param_color3_view>(), &O };
        case T::TEXTURE: return { xproperty::getObjectByType<param_texture_view>(), &O };
        }
        return { nullptr, nullptr };
    }

    // v2 (m_DescVersion 2) keeps what the person set in m_Overrides. The Textures, TextureDefaults and FinalTextures of v1 are only read: an old file is turned
    // into overrides when it is read (FoldLegacy) and is written back as v2 on save. The material compiler still writes its template (MaterialInstance.txt) with
    // these lists: the textures of the material in sampler order and the slots it exposes, which material_layout reads.
    struct descriptor : xresource_pipeline::descriptor::base
    {
        inline static constexpr auto base_filters_v = std::array{ xrsc::material_type_guid_v, xrsc::material_instance_type_guid_v };

        XPROPERTY_VDEF
        ( "material_instance_descriptor",   descriptor
        , obj_member<"Version",             &descriptor::m_DescVersion,         member_flags<flags::DONT_SHOW>>
        // Base: what the instance is made from, a material or another instance (a variant), in one field (not saved: it is MaterialRef or ParentRef). The editor
        // turns a pick into SetMaterial or SetParent; with an instance as the base, MaterialRef shows, read only (it is the base's)
        , obj_member<"Base", +[](descriptor& O, bool bRead, xresource::full_guid& Guid)
            {
                if (bRead) Guid = O.m_ParentRef.empty() ? xresource::full_guid{ O.m_MaterialRef.m_Instance, xrsc::material_type_guid_v } : xresource::full_guid{ O.m_ParentRef.m_Instance, xrsc::material_instance_type_guid_v };
                // written: nothing here, the editor's SetMaterial / SetParent makes the change (what was set carries over, undo has the whole state before)
            }
            , member_ui<xresource::full_guid>::type_filters<base_filters_v>, member_flags<flags::DONT_SAVE>
            , member_help<"A material, or another material instance to make a variant of (it inherits what that one sets)">>
        , obj_member<"MaterialRef",         &descriptor::m_MaterialRef,         member_dynamic_flags<+[](const descriptor& O) { xproperty::flags::type F{}; F.m_bDontShow = O.m_ParentRef.empty(); F.m_bShowReadOnly = true; return F; }>>
        , obj_member<"ParentRef",           &descriptor::m_ParentRef,           member_flags<flags::DONT_SHOW>>
        // The render settings an instance may set (system parameters, not the graph's), overrides like the parameters
        , obj_scope<"Render Settings"
            , obj_member<"Double Sided",    &descriptor::m_DoubleSidedRow,      member_flags<flags::DONT_SAVE>, member_help<"Back faces are drawn too, lit by their own side: a pipeline of its own, not a material of its own">>
            , obj_member<"Alpha Cutoff",    &descriptor::m_AlphaCutoffRow,      member_flags<flags::DONT_SAVE>
                , member_dynamic_flags<+[](const descriptor& O) { xproperty::flags::type F{}; F.m_bDontShow = O.m_AlphaCutoffRow.m_Name.empty(); F.m_bDontSave = true; return F; }>
                , member_help<"Masked: alpha below this is cut out">>
            , member_ui_open<true>>
        , obj_member<"Parameters",          &descriptor::m_Params,              member_flags<flags::DONT_SAVE>, member_array_size_readonly<>, member_ui_open<true>
            , member_help<"The material's parameters, in its order: the value this instance sets (marked, > goes back to inheriting it), a parent's or the material's default">>
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

        param_row                           m_DoubleSidedRow        = {};       // the editor's view of the render settings (never saved; empty name: the material has none)
        param_row                           m_AlphaCutoffRow        = {};
        std::vector<param_row>              m_Params                = {};       // the editor's view of the material's parameters, in its order (never saved, see param_row)
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