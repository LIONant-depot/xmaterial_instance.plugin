#ifndef XMATERIAL_INSTANCE_EDITOR_H
#define XMATERIAL_INSTANCE_EDITOR_H
#pragma once

// The Material Instance editor: opens a material instance from the asset browser in its own window. What the instance says (which material, which parent, which
// parameters it sets) is edited in an inspector, a parameters panel and through commands, all undoable; the compiled instance is previewed on a mesh. Hosts include this
// header and open editors through xeditor::open_resource_editors.
#include "source/Tools/Editor/xeditor_descriptor_editor.h"
#include "source/Tools/Editor/xeditor_mesh_preview.h"
#include "dependencies/xresource_pipeline_v2/source/editor/xresource_editor_command_guids.h"
#include "dependencies/xresource_pipeline_v2/source/editor/xresource_editor_resources.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_intance_descriptor.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_carry.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_xgpu_rsc_loader.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_runtime.h"
#include "plugins/xmaterial.plugin/source/xmaterial_runtime.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_xgpu_rsc_loader.cpp"      // the resource loader: compiled once, in the host's translation unit
#include "plugins/xmaterial_instance.plugin/source/Editor/xmaterial_instance_thumbnail.h"

#include <array>
#include <charconv>
#include <cmath>
#include <sstream>

namespace xmaterial_instance_editor
{
    using xeditor::cmd_util::GetArg;
    using xmaterial::param_type;
    using descriptor = xmaterial_instance::descriptor;

    //--------------------------------------------------------------------------------------------
    // Text <-> values
    //--------------------------------------------------------------------------------------------
    inline const char* TypeName(param_type T) noexcept { return std::array{ "FLOAT", "FLOAT2", "FLOAT3", "FLOAT4", "COLOR", "TEXTURE" }[int(T)]; }
    inline int Components(param_type T) noexcept { return T == param_type::FLOAT ? 1 : T == param_type::FLOAT2 ? 2 : T == param_type::FLOAT3 ? 3 : T == param_type::TEXTURE ? 0 : 4; }

    // A guid in hex: 16 digits, fewer, or 32 (with its type, only the first 16 are read); a ", type" after it is ignored
    inline bool ParseHex(const std::string& Text, std::uint64_t& Out) noexcept
    {
        std::string_view Part = Text;
        Part = Part.substr(0, Part.find_first_of(", "));
        if (Part.size() == 32) Part = Part.substr(0, 16);
        const auto Result = std::from_chars(Part.data(), Part.data() + Part.size(), Out, 16);
        return !Part.empty() && Part.size() <= 16 && Result.ec == std::errc() && Result.ptr == Part.data() + Part.size();
    }

    // The value of a parameter as the commands write it: a number, numbers separated by commas, or the texture's guid
    inline std::string FormatValue(param_type T, const xmaterial::float4v& V, const xrsc::texture_ref& Texture)
    {
        if (T == param_type::TEXTURE) return std::format("{:016X}", Texture.m_Instance.m_Value);
        const float C[4] = { V.m_X, V.m_Y, V.m_Z, V.m_W };
        std::string Out;
        for (int i = 0; i < Components(T); ++i) Out += std::format("{}{}", i ? ", " : "", C[i]);
        return Out;
    }

    // Text -> value. A color may leave its alpha out (1). False when the text is not that.
    inline bool ParseValue(param_type T, const std::string& Text, xmaterial::float4v& V, xrsc::texture_ref& Texture) noexcept
    {
        if (T == param_type::TEXTURE)
        {
            std::uint64_t Guid = 0;
            if (!ParseHex(Text, Guid) || Guid == 0) return false;
            Texture.m_Instance.m_Value = Guid;
            return true;
        }

        std::string Spaced = Text;
        std::ranges::replace(Spaced, ',', ' ');
        std::istringstream In(Spaced);
        float C[4] = {};
        int   n    = 0;
        while (n < 4 && (In >> C[n])) ++n;
        std::string Rest;
        if (In >> Rest) return false;                                   // something that is not a number, or a fifth one
        if (n != Components(T) && !(T == param_type::COLOR && n == 3)) return false;
        if (T == param_type::COLOR && n == 3) C[3] = 1.0f;
        V = { C[0], C[1], C[2], C[3] };
        return true;
    }

    //--------------------------------------------------------------------------------------------
    // Where things are: the project the instance is in, the folder of its descriptor (its parents are read from there), what it resolves to
    //--------------------------------------------------------------------------------------------
    inline std::wstring Project() noexcept { return xresource_editor::g_LibMgr.m_ProjectPath; }

    inline std::wstring DescriptorsRoot(const xeditor::descriptor_document& Doc) noexcept
    {
        const auto At = Doc.m_DescriptorPath.rfind(L"MaterialInstance");
        return At == std::wstring::npos || At == 0 ? std::wstring{} : Doc.m_DescriptorPath.substr(0, At - 1);
    }

    inline descriptor& Desc(xeditor::descriptor_document& Doc) noexcept { return static_cast<descriptor&>(*Doc.m_pDescriptor); }

    // One parameter of the material as the instance sees it, or an override that matches none (an orphan: kept in the file, not applied)
    struct row
    {
        enum class source : std::uint8_t { DEFAULT, PARENT, HERE, ORPHAN_ID, ORPHAN_TYPE };

        xmaterial::param_id             m_ID        = 0;
        std::string                     m_Name;
        param_type                      m_Type      = param_type::FLOAT;
        xmaterial::float4v              m_Value;                        // linear, as resolved (an orphan: as stored)
        xrsc::texture_ref               m_Texture;
        source                          m_Source    = source::DEFAULT;
        std::string                     m_From;                         // PARENT: the instance that sets it
        const xmaterial::param_decl*    m_pDecl     = nullptr;          // null for an orphan with no declaration

        std::string Where() const
        {
            switch (m_Source)
            {
            case source::DEFAULT:       return "default";
            case source::PARENT:        return "parent " + m_From;
            case source::HERE:          return "here";
            case source::ORPHAN_ID:     return "orphan: the material has no such parameter";
            default:                    return "orphan: the parameter has another type now";
            }
        }
    };

    inline std::vector<row> Rows(const xmaterial_instance::resolution& R, const descriptor& D)
    {
        std::vector<row> Out;
        for (const auto& P : R.m_Resolved.m_Params)
        {
            row Row;
            Row.m_ID = P.m_ID; Row.m_Type = P.m_Type; Row.m_Value = P.m_Value;
            Row.m_pDecl = &*std::ranges::find(R.m_Layout.m_Manifest.m_Decls, P.m_ID, &xmaterial::param_decl::m_ID);
            Row.m_Name  = Row.m_pDecl->m_Name;
            if (P.m_Source >= 0)
            {
                const auto Key = R.m_Resolved.m_Chain[P.m_Source];
                Row.m_Source = P.m_Source == 0 ? row::source::HERE : row::source::PARENT;
                if (P.m_Source > 0) Row.m_From = xeditor::ResolveResourceDisplayName(xresource::full_guid{ xresource::instance_guid{ Key }, xrsc::material_instance_type_guid_v }, nullptr);
                if (P.m_Type == param_type::TEXTURE) Row.m_Texture = R.m_Descriptors.at(Key)->Find(P.m_ID)->m_Texture;
            }
            else if (P.m_Type == param_type::TEXTURE)
            {
                if (const int i = R.m_Layout.TextureIndex(P.m_ID); i >= 0) Row.m_Texture = R.m_Layout.m_Template.m_lFinalTextures[i].m_TextureRef;
            }
            Out.push_back(std::move(Row));
        }

        for (const auto& O : D.m_Overrides)
        {
            auto It = std::ranges::find(R.m_Layout.m_Manifest.m_Decls, O.m_ID, &xmaterial::param_decl::m_ID);
            if (It != R.m_Layout.m_Manifest.m_Decls.end() && It->m_Type == O.m_Type) continue;
            row Row;
            Row.m_ID = O.m_ID; Row.m_Type = O.m_Type; Row.m_Value = O.m_Value; Row.m_Texture = O.m_Texture; Row.m_Name = O.m_NameHint;
            Row.m_Source = It == R.m_Layout.m_Manifest.m_Decls.end() ? row::source::ORPHAN_ID : row::source::ORPHAN_TYPE;
            Out.push_back(std::move(Row));
        }
        return Out;
    }

    // The overrides of D under another material (SetMaterial, SetParent): by id, then by name and type; what matches nothing stays as it is (xmaterial_instance_carry.h)
    inline void CarryTo(descriptor& D, xrsc::material_ref Material, const xmaterial_instance::material_layout& Layout) noexcept
    {
        std::vector<xmaterial_instance::carry_param> Old, New;
        for (const auto& O : D.m_Overrides)             Old.push_back({ O.m_ID, O.m_NameHint, int(O.m_Type) });
        for (const auto& P : Layout.m_Manifest.m_Decls) New.push_back({ P.m_ID, P.m_Name, int(P.m_Type) });
        const auto Match = xmaterial_instance::MatchCarriedOverrides(Old, New);
        for (std::size_t i = 0; i < Match.size(); ++i)
            if (Match[i] && Match[i] != D.m_Overrides[i].m_ID)
            {
                D.m_Overrides[i].m_ID = Match[i];
                D.m_Overrides[i].m_NameHint = std::ranges::find(Layout.m_Manifest.m_Decls, Match[i], &xmaterial::param_decl::m_ID)->m_Name;
            }
        std::ranges::sort(D.m_Overrides, {}, &xmaterial_instance::override_entry::m_ID);
        D.m_MaterialRef = Material;
    }

    //--------------------------------------------------------------------------------------------
    // The commands. Each changes the descriptor and undoes by putting the whole descriptor back (a snapshot taken before it), so a command that carries values to
    // another material, or clears one, is inverted exactly. Values are linear.
    //--------------------------------------------------------------------------------------------
    struct instance_cmd : xundo::command_base
    {
        xeditor::descriptor_document&   m_Doc;
        const char*                     m_pName;

        instance_cmd(xundo::system& System, xeditor::descriptor_document& Doc, const char* pName) noexcept : command_base(System, pName, nullptr), m_Doc(Doc), m_pName(pName) {}

        virtual std::string Apply(descriptor& D) noexcept = 0;          // the change, or why not (the descriptor is untouched then)

        std::string Redo() noexcept override
        {
            if (!m_Doc.isLoaded()) return std::format("{}: nothing loaded", m_pName);
            if (auto Err = Apply(Desc(m_Doc)); !Err.empty()) return std::format("{}: {}", m_pName, Err);
            m_Doc.m_bDirty = true;
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override { xeditor::WriteString(File, m_Doc.Snapshot()); }
        void Undo(xundo::undo_file& File) noexcept override { m_Doc.Restore(xeditor::ReadString(File)); }

        // The material's layout for the instance as it is now
        std::string LoadLayout(xmaterial_instance::material_layout& Layout) const noexcept { return Layout.Load(Project(), Desc(m_Doc).m_MaterialRef); }

        // -Id: a parameter's id in hex, or @Name for a declaration of the material (an orphan can only be named by its id)
        bool ParseId(const std::string& Text, const xmaterial_instance::material_layout* pLayout, xmaterial::param_id& Out) const noexcept
        {
            if (!Text.starts_with('@')) return ParseHex(Text, Out) && Out != 0;
            if (!pLayout) return false;
            auto It = std::ranges::find(pLayout->m_Manifest.m_Decls, Text.substr(1), &xmaterial::param_decl::m_Name);
            if (It == pLayout->m_Manifest.m_Decls.end()) return false;
            Out = It->m_ID;
            return true;
        }
    };

    //--------------------------------------------------------------------------------------------
    // SetMaterial: picks the material the instance is made from, directly (it has no parent after it). What the person set comes along: by id, then by name (CarryTo);
    // the rest is kept as orphans. The material must have been compiled (its layout is in the compiler's log). Undo puts the whole descriptor back.
    //--------------------------------------------------------------------------------------------
    struct set_material_cmd : instance_cmd
    {
        set_material_cmd(xundo::system& System, xeditor::descriptor_document& Doc) noexcept : instance_cmd(System, Doc, "SetMaterial") { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Makes the instance from another material (undoable): what was set carries over by parameter id, then by name; the parent is cleared. The material must have been compiled. Usage: SetMaterial -Material hexguid [-Before hexguid]"; }
        void RegisterArguments() noexcept override
        {
            m_hMaterial = m_Parser.addOption("Material", "The material's guid, 16 hex digits (32 with the type is accepted)", true,  1);
            m_hBefore   = m_Parser.addOption("Before",   "The previous material, when an edit has already been applied",       false, 1);
        }

        std::string Apply(descriptor& D) noexcept override
        {
            std::string Text;
            xrsc::material_ref Material;
            if (!GetArg(m_Parser, m_hMaterial, Text) || !ParseHex(Text, Material.m_Instance.m_Value) || Material.empty()) return "bad arguments";

            xmaterial_instance::material_layout Layout;
            if (auto Err = Layout.Load(Project(), Material); !Err.empty()) return Err;
            D.m_ParentRef = {};
            CarryTo(D, Material, Layout);
            return {};
        }

        // An inspector edit has already put the new material in the descriptor: the state to come back to has the previous one.
        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            std::string Text;
            xrsc::material_ref Before;
            if (!m_Doc.isLoaded()) { xeditor::WriteString(File, {}); return; }
            auto& D = Desc(m_Doc);
            const auto Current = D.m_MaterialRef;
            if (GetArg(m_Parser, m_hBefore, Text) && ParseHex(Text, Before.m_Instance.m_Value)) D.m_MaterialRef = Before;
            xeditor::WriteString(File, m_Doc.Snapshot());
            D.m_MaterialRef = Current;
        }

        xcmdline::parser::handle m_hMaterial, m_hBefore;
    };

    //--------------------------------------------------------------------------------------------
    // SetParent: inherits from another instance (-Instance 0: from the material alone). The parent's material becomes the instance's: when it differs, what was set
    // carries over like SetMaterial. A parent that would make a cycle is refused.
    //--------------------------------------------------------------------------------------------
    struct set_parent_cmd : instance_cmd
    {
        set_parent_cmd(xundo::system& System, xeditor::descriptor_document& Doc) noexcept : instance_cmd(System, Doc, "SetParent") { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Makes the instance inherit from another material instance, or from its material alone with 0 (undoable). Its material becomes the parent's. Usage: SetParent -Instance hexguid|0 [-Before hexguid]"; }
        void RegisterArguments() noexcept override
        {
            m_hInstance = m_Parser.addOption("Instance", "The parent's guid, 16 hex digits; 0 for none",                        true,  1);
            m_hBefore   = m_Parser.addOption("Before",   "The previous parent, when an edit has already been applied",          false, 1);
        }

        std::string Apply(descriptor& D) noexcept override
        {
            std::string Text;
            std::uint64_t Parent = 0;
            if (!GetArg(m_Parser, m_hInstance, Text) || !ParseHex(Text, Parent)) return "bad arguments";
            if (Parent == 0) { D.m_ParentRef = {}; return {}; }

            // The parent, and its parents: none of them is this instance
            const std::uint64_t Self = m_Doc.m_Guid.m_Instance.m_Value;
            const auto Root = DescriptorsRoot(m_Doc);
            descriptor First;
            for (std::uint64_t Key = Parent, n = 0; Key; ++n)
            {
                if (Key == Self)                  return "that instance inherits from this one (a cycle)";
                if (n > std::uint64_t(xmaterial_instance::max_chain_depth)) break;       // too deep: the compiler reports it
                descriptor                   Up;
                xproperty::settings::context C;
                if (Up.Serialize(true, xmaterial_instance::resolution::DescriptorPath(Root, Key), C)) return std::format("the instance {:016X} could not be read", Key);
                if (n == 0) First = Up;
                Key = Up.m_ParentRef.m_Instance.m_Value;
            }
            if (First.m_MaterialRef.empty()) return "the parent has no material";

            if (First.m_MaterialRef != D.m_MaterialRef)
            {
                xmaterial_instance::material_layout Layout;
                if (auto Err = Layout.Load(Project(), First.m_MaterialRef); !Err.empty()) return Err;
                CarryTo(D, First.m_MaterialRef, Layout);
            }
            D.m_ParentRef.m_Instance.m_Value = Parent;
            return {};
        }

        // An inspector edit has already put the new parent in the descriptor
        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            std::string Text;
            std::uint64_t Before = 0;
            if (!m_Doc.isLoaded()) { xeditor::WriteString(File, {}); return; }
            auto& D = Desc(m_Doc);
            const auto Current = D.m_ParentRef;
            if (GetArg(m_Parser, m_hBefore, Text) && ParseHex(Text, Before)) D.m_ParentRef.m_Instance.m_Value = Before;
            xeditor::WriteString(File, m_Doc.Snapshot());
            D.m_ParentRef = Current;
        }

        xcmdline::parser::handle m_hInstance, m_hBefore;
    };

    //--------------------------------------------------------------------------------------------
    // SetOverride / ClearOverride: the instance sets a parameter (also to its default: pinned), or goes back to inheriting it
    //--------------------------------------------------------------------------------------------
    struct set_override_cmd : instance_cmd
    {
        set_override_cmd(xundo::system& System, xeditor::descriptor_document& Doc) noexcept : instance_cmd(System, Doc, "SetOverride") { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Sets a parameter of the material on this instance (undoable); a value equal to the default is kept (pinned). Values are linear: a number, numbers separated by commas (a color may leave its alpha out), or a texture's guid. Usage: SetOverride -Id hexid|@Name -Value text"; }
        void RegisterArguments() noexcept override
        {
            m_hId    = m_Parser.addOption("Id",    "The parameter: its id in hex, or @Name",  true, 1);
            m_hValue = m_Parser.addOption("Value", "The value, linear",                       true, 1);
        }

        std::string Apply(descriptor& D) noexcept override
        {
            std::string IdText, Value;
            if (!GetArg(m_Parser, m_hId, IdText) || !GetArg(m_Parser, m_hValue, Value)) return "bad arguments";

            xmaterial_instance::material_layout Layout;
            if (auto Err = LoadLayout(Layout); !Err.empty()) return Err;

            xmaterial::param_id ID = 0;
            if (!ParseId(IdText, &Layout, ID)) return std::format("'{}' is not a parameter id or @Name", IdText);
            auto It = std::ranges::find(Layout.m_Manifest.m_Decls, ID, &xmaterial::param_decl::m_ID);
            if (It == Layout.m_Manifest.m_Decls.end()) return std::format("the material has no parameter {:016X}", ID);

            xmaterial::float4v V; xrsc::texture_ref Texture;
            if (!ParseValue(It->m_Type, Value, V, Texture)) return std::format("'{}' is not a value for {} ({})", Value, It->m_Name, It->m_Type == param_type::TEXTURE ? "a texture guid" : std::format("{} numbers", Components(It->m_Type)));

            auto& O = D.Set(ID);
            O.m_Type = It->m_Type; O.m_Value = V; O.m_Texture = Texture; O.m_NameHint = It->m_Name;
            return {};
        }

        xcmdline::parser::handle m_hId, m_hValue;
    };

    struct clear_override_cmd : instance_cmd
    {
        clear_override_cmd(xundo::system& System, xeditor::descriptor_document& Doc) noexcept : instance_cmd(System, Doc, "ClearOverride") { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Removes what this instance sets for a parameter: it inherits it again, also an orphan (undoable). Usage: ClearOverride -Id hexid|@Name"; }
        void RegisterArguments() noexcept override { m_hId = m_Parser.addOption("Id", "The parameter: its id in hex, or @Name", true, 1); }

        std::string Apply(descriptor& D) noexcept override
        {
            std::string IdText;
            if (!GetArg(m_Parser, m_hId, IdText)) return "bad arguments";

            xmaterial_instance::material_layout Layout;
            const bool bLayout = LoadLayout(Layout).empty();
            xmaterial::param_id ID = 0;
            if (!ParseId(IdText, bLayout ? &Layout : nullptr, ID)) return std::format("'{}' is not a parameter id{}", IdText, bLayout ? " or @Name" : " (the material has no layout yet, only ids work)");
            if (!D.Clear(ID)) return std::format("the instance sets no parameter {:016X}", ID);
            return {};
        }

        xcmdline::parser::handle m_hId;
    };

    // Every parameter of the material with its value here and where it comes from (default, a parent, here), then the overrides that match nothing
    struct list_overrides_cmd : xundo::query_command_base
    {
        xeditor::descriptor_document& m_Doc;
        list_overrides_cmd(xundo::system& System, xeditor::descriptor_document& Doc) noexcept : query_command_base(System, "ListOverrides", nullptr), m_Doc(Doc) {}
        const char* getCommandHelp() const noexcept override { return "Lists the parameters of the instance's material: id, name, type, value and where it comes from (default, parent <name>, here), then the orphans. Usage: ListOverrides"; }
        void RegisterArguments() noexcept override {}
        std::string Query() noexcept override
        {
            if (!m_Doc.isLoaded()) return "ListOverrides: nothing loaded";
            xmaterial_instance::resolution R;
            if (auto Err = R.Run(Project(), DescriptorsRoot(m_Doc), m_Doc.m_Guid.m_Instance.m_Value, Desc(m_Doc)); !Err.empty()) return "ListOverrides: " + Err;

            std::string Out;
            for (const auto& Row : Rows(R, Desc(m_Doc)))
                Out += std::format("{:016X}  {}  {}  {}  {}\n", Row.m_ID, Row.m_Name.empty() ? "?" : Row.m_Name, TypeName(Row.m_Type), FormatValue(Row.m_Type, Row.m_Value, Row.m_Texture), Row.Where());
            for (const auto& D : R.m_Resolved.m_Diags) if (D.m_Code >= xmaterial_instance::diag_code::E_CHAIN_CYCLE) Out += "error: the chain is not valid (cycle, too deep, or another material)\n";
            return Out.empty() ? "(the material has no parameters)" : Out;
        }
    };

    //--------------------------------------------------------------------------------------------
    // The editor
    //--------------------------------------------------------------------------------------------
    struct session : xeditor::descriptor_editor
    {
        set_material_cmd                m_SetMaterial;
        set_parent_cmd                  m_SetParent;
        set_override_cmd                m_SetOverride;
        clear_override_cmd              m_ClearOverride;
        list_overrides_cmd              m_ListOverrides;
        xeditor::mesh_preview           m_Preview;
        xeditor::mesh_preview_cmds      m_PreviewCmds;
        xrsc::material_instance_ref     m_InstanceRef;

        std::unique_ptr<xmaterial_instance::resolution> m_Resolution;       // what the parameters panel shows, read again every second and when the descriptor is replaced
        double                                          m_ResolutionAt  = 0;
        std::string                                     m_ResolutionErr;
        xmaterial::param_id                             m_EditID        = 0;        // the parameter whose widget is being dragged: the value shown is the edit's until it is let go
        float                                           m_Edit[4]       = {};

        session(xresource::full_guid Guid, xresource_editor::library::guid LibraryGuid, xgpu::device* pDevice) noexcept
            : descriptor_editor("Material Instance", Guid, LibraryGuid, pDevice), m_SetMaterial(m_Undo, m_Document), m_SetParent(m_Undo, m_Document), m_SetOverride(m_Undo, m_Document)
            , m_ClearOverride(m_Undo, m_Document), m_ListOverrides(m_Undo, m_Document), m_PreviewCmds(m_Undo, m_Preview)
        {
            xresource_editor::WireResourcePickerCallbacks(m_DescriptorInspector.m_Inspector);

            AddPanel("Material Instance Preview", dock::center, [this] { m_Preview.Render(); });
            AddPanel("Description",               dock::right,  [this] { m_DescriptorInspector.Show(); });
            AddPanel("Parameters",                dock::right,  [this] { ShowParameters(); });

            if (pDevice && m_Preview.Init(*pDevice)) ReloadPreview();
        }

        ~session() noexcept override { xresource::g_Mgr.ReleaseRef(m_InstanceRef); }

        void OnCompiled() noexcept override { m_Resolution.reset(); ReloadPreview(); }
        void OnDescriptorReplaced() noexcept override { m_Resolution.reset(); m_EditID = 0; }

        // Picking the material or the parent is one command, not a property edit: what was set has to follow.
        bool OnCustomChange(const xproperty::ui::undo::cmd& Cmd) noexcept override
        {
            const bool bMaterial = Cmd.m_Name.ends_with("/MaterialRef"), bParent = Cmd.m_Name.ends_with("/ParentRef");
            if ((!bMaterial && !bParent) || !Cmd.m_NewValue.is<xresource::full_guid>()) return false;
            const auto Before = Cmd.m_Original.is<xresource::full_guid>() ? Cmd.m_Original.get<xresource::full_guid>().m_Instance.m_Value : 0;
            xeditor::Run(m_Undo, std::format("{} -{} {:016X} -Before {:016X}", bMaterial ? "SetMaterial" : "SetParent", bMaterial ? "Material" : "Instance", Cmd.m_NewValue.get<xresource::full_guid>().m_Instance.m_Value, Before));
            m_Resolution.reset();
            return true;
        }

        // The parameters of the material: each with where its value comes from (dim: the material's default or a parent's; marked: set here, the button goes back to
        // inheriting it) and an editor for its value. A color is shown as sRGB and stored linear. Every edit is a SetOverride when it is let go.
        void ShowParameters() noexcept
        {
            if (!m_Document.isLoaded()) return;
            auto&      D    = Desc(m_Document);
            const auto Self = m_Document.m_Guid.m_Instance.m_Value;
            const auto Now  = ImGui::GetTime();
            if (!m_Resolution || Now - m_ResolutionAt > 1.0)
            {
                m_Resolution = std::make_unique<xmaterial_instance::resolution>();
                m_ResolutionErr = m_Resolution->Run(Project(), DescriptorsRoot(m_Document), Self, D);
                m_ResolutionAt  = Now;
            }
            if (!m_ResolutionErr.empty()) { ImGui::TextWrapped("%s", m_ResolutionErr.c_str()); return; }
            if (m_Resolution->m_Resolved.Failed()) { ImGui::TextWrapped("The chain of parents is not valid (a cycle, more than 8 deep, or a parent of another material)."); return; }

            std::string Pending;
            const auto Edit = [&](const std::string& Command) { Pending = Command; };        // run after the rows: they point into the resolution an edit drops
            const auto ToSRGB   = [](float x) { return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f; };
            const auto ToLinear = [](float x) { return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f); };

            const auto Rows_ = Rows(*m_Resolution, D);
            for (const auto& R : Rows_)
            {
                ImGui::PushID(static_cast<int>(R.m_ID ^ (R.m_ID >> 32)));
                const bool bOrphan = R.m_Source == row::source::ORPHAN_ID || R.m_Source == row::source::ORPHAN_TYPE;
                const bool bHere   = R.m_Source == row::source::HERE;

                // The button that goes back to inheriting (or drops an orphan); a row that sets nothing keeps the room
                if (bHere || bOrphan)
                {
                    if (ImGui::SmallButton(bOrphan ? "x" : ">")) Edit(std::format("ClearOverride -Id {:016X}", R.m_ID));
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", bOrphan ? "Remove this value: nothing reads it" : "Inherit it again");
                }
                else ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Text, bHere ? IM_COL32(170, 170, 255, 255) : bOrphan ? IM_COL32(200, 120, 120, 255) : ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::TextUnformatted(R.m_Name.empty() ? std::format("{:016X}", R.m_ID).c_str() : R.m_Name.c_str());
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%016llX: %s", static_cast<unsigned long long>(R.m_ID), R.Where().c_str());
                ImGui::SameLine(150.0f);

                if (R.m_Type == param_type::TEXTURE)
                {
                    const xresource::full_guid Current{ xresource::instance_guid{ R.m_Texture.m_Instance.m_Value }, xrsc::texture_type_guid_v };
                    bool bOpen = false, bClear = false;
                    xresource_editor::RenderResourceReferenceRow(&R, Current, true, {}, bOpen, bClear);
                    xresource::full_guid Picked = Current;
                    const xresource::type_guid Filter = xrsc::texture_type_guid_v;
                    xresource_editor::ResourceBrowserPopup(&R, bOpen, Picked, std::span<const xresource::type_guid>(&Filter, 1));
                    if (!bOrphan && Picked.m_Instance.m_Value != Current.m_Instance.m_Value && !Picked.empty()) Edit(std::format("SetOverride -Id {:016X} -Value {:016X}", R.m_ID, Picked.m_Instance.m_Value));
                    if (bClear && bHere) Edit(std::format("ClearOverride -Id {:016X}", R.m_ID));
                }
                else if (bOrphan) ImGui::TextDisabled("%s  (%s)", FormatValue(R.m_Type, R.m_Value, R.m_Texture).c_str(), R.Where().c_str());
                else
                {
                    float V[4] = { R.m_Value.m_X, R.m_Value.m_Y, R.m_Value.m_Z, R.m_Value.m_W };
                    if (m_EditID == R.m_ID) std::copy(m_Edit, m_Edit + 4, V);
                    const auto& Decl = *R.m_pDecl;
                    bool bChanged = false;
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    switch (R.m_Type)
                    {
                    case param_type::FLOAT:  bChanged = Decl.m_UiMax.m_X > Decl.m_UiMin.m_X ? ImGui::SliderFloat("##v", V, Decl.m_UiMin.m_X, Decl.m_UiMax.m_X) : ImGui::DragFloat("##v", V, 0.01f); break;
                    case param_type::FLOAT2: bChanged = ImGui::DragFloat2("##v", V, 0.01f); break;
                    case param_type::FLOAT3: bChanged = ImGui::DragFloat3("##v", V, 0.01f); break;
                    case param_type::FLOAT4: bChanged = ImGui::DragFloat4("##v", V, 0.01f); break;
                    default:
                    {
                        float S[4] = { ToSRGB(V[0]), ToSRGB(V[1]), ToSRGB(V[2]), V[3] };
                        const ImGuiColorEditFlags Flags = (Decl.m_bHDR ? ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float : ImGuiColorEditFlags_Float) | (Decl.m_bAlpha ? 0 : ImGuiColorEditFlags_NoAlpha);
                        bChanged = ImGui::ColorEdit4("##v", S, Flags);
                        if (bChanged) { V[0] = ToLinear(S[0]); V[1] = ToLinear(S[1]); V[2] = ToLinear(S[2]); V[3] = S[3]; }
                    }
                    }
                    if (bChanged) { m_EditID = R.m_ID; std::copy(V, V + 4, m_Edit); }
                    if (ImGui::IsItemDeactivatedAfterEdit() && m_EditID == R.m_ID)
                    {
                        m_EditID = 0;
                        Edit(std::format("SetOverride -Id {:016X} -Value {}", R.m_ID, xeditor::Quote(FormatValue(R.m_Type, { V[0], V[1], V[2], V[3] }, {}))));
                    }
                }
                ImGui::PopID();
            }
            if (!Pending.empty()) { xeditor::Run(m_Undo, Pending); m_Resolution.reset(); }
        }

        // (Re)builds what the preview draws: the instance's material with the instance's textures. After every compile.
        void ReloadPreview() noexcept
        {
            xresource::g_Mgr.ReleaseRef(m_InstanceRef);
            m_InstanceRef.m_Instance = m_Document.m_Guid.m_Instance;
            auto* pInstance = xresource::g_Mgr.getResource(m_InstanceRef);
            if (!pInstance) { m_Preview.ClearMaterial(); return; }
            auto* pMaterial = xresource::g_Mgr.getResource(pInstance->m_MaterialRef);
            if (!pMaterial) { m_Preview.ClearMaterial(); return; }

            std::vector<xgpu::pipeline_instance::sampler_binding> Bindings;
            Bindings.reserve(pInstance->m_nTexturesList);
            for (auto& Entry : pInstance->getTextures())
            {
                if (auto* pTexture = xresource::g_Mgr.getResource(Entry.m_TexureRef)) Bindings.emplace_back(*pTexture);
                else                                                                   Bindings.emplace_back(m_Preview.DefaultTexture());
            }
            m_Preview.SetMaterial(pMaterial->getShader(), Bindings, PreviewBlock(*pInstance, *pMaterial));
        }
    };

    inline const xeditor::auto_register_resource_editor g_Registration
    { xrsc::material_instance_type_guid_v
    , [](xresource::full_guid Guid, xresource_editor::library::guid LibraryGuid, xgpu::device* pDevice) -> std::unique_ptr<xeditor::resource_editor>
      { return std::make_unique<session>(Guid, LibraryGuid, pDevice); }
    };

    inline const xeditor::auto_register_thumbnail_renderer g_ThumbReg{ xrsc::material_instance_type_guid_v, []{ return std::make_unique<thumbnail_renderer>(); } };
}

#endif // XMATERIAL_INSTANCE_EDITOR_H
