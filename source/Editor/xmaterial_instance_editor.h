#ifndef XMATERIAL_INSTANCE_EDITOR_H
#define XMATERIAL_INSTANCE_EDITOR_H
#pragma once

// The Material Instance editor: opens a material instance from the asset browser in its own window. The descriptor (which material, which
// textures) is edited in an inspector and through commands, all undoable; the compiled instance is previewed on a mesh. Hosts include this header
// and open editors through xeditor::open_resource_editors.
#include "source/Tools/Editor/xeditor_descriptor_editor.h"
#include "source/Tools/Editor/xeditor_mesh_preview.h"
#include "source/Tools/Editor/xeditor_texture_thumbnails.h"
#include "dependencies/xresource_pipeline_v2/source/editor/E10_CommandGuids.h"
#include "dependencies/xresource_pipeline_v2/source/editor/E10_Resources.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_intance_descriptor.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_xgpu_rsc_loader.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_runtime.h"
#include "plugins/xmaterial.plugin/source/xmaterial_runtime.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_xgpu_rsc_loader.cpp"      // the resource loader: compiled once, in the host's translation unit

#include <charconv>

namespace xmaterial_instance_editor
{
    //--------------------------------------------------------------------------------------------
    // SetMaterial: picks the material the instance is made from. The material's own textures become the instance's defaults, so this is more than
    // setting one property: the compiler leaves a template of the material's textures in its log, which is read into the descriptor. Undo puts the
    // whole descriptor back.
    //--------------------------------------------------------------------------------------------
    struct set_material_cmd : xundo::command_base
    {
        xeditor::descriptor_document& m_Doc;

        set_material_cmd(xundo::system& System, xeditor::descriptor_document& Doc) noexcept : command_base(System, "SetMaterial", nullptr), m_Doc(Doc) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Makes the instance from another material (undoable): its textures become the defaults. The material must have been compiled. Usage: SetMaterial -Material hexguid [-Before hexguid]"; }
        void RegisterArguments() noexcept override
        {
            m_hMaterial = m_Parser.addOption("Material", "The material's guid, 16 hex digits (32 with the type is accepted)", true,  1);
            m_hBefore   = m_Parser.addOption("Before",   "The previous material, when an edit has already been applied",       false, 1);
        }

        static bool ParseGuid(const std::string& Text, xrsc::material_ref& Out) noexcept
        {
            if (Text.size() != 16 && Text.size() != 32) return false;
            std::uint64_t Value = 0;
            const auto Result = std::from_chars(Text.data(), Text.data() + 16, Value, 16);
            if (Result.ec != std::errc() || Result.ptr != Text.data() + 16) return false;
            Out.m_Instance.m_Value = Value;
            return true;
        }

        // The material's template of textures, in the compiler's log: streamed into the descriptor, then the instance's textures start from the defaults.
        static std::string LoadTemplate(xmaterial_instance::descriptor& D, xrsc::material_ref Material) noexcept
        {
            const auto Path = std::format(L"{}/Cache/Resources/Logs/Material/{:02X}/{:02X}/{:X}.log/MaterialInstance.txt", e10::g_LibMgr.m_ProjectPath
                , Material.m_Instance.m_Value & 0xff, (Material.m_Instance.m_Value & 0xff00) >> 8, Material.m_Instance.m_Value);

            xtextfile::stream File;
            if (auto Err = File.Open(true, Path, xtextfile::file_type::TEXT); Err) return "the material has no template yet (compile the material first)";
            xproperty::settings::context Context;
            if (auto Err = xproperty::sprop::serializer::Stream(File, D, Context); Err) return "the material's template could not be read";
            D.m_MaterialRef = Material;
            D.setupDefaults();
            return {};
        }

        std::string Redo() noexcept override
        {
            std::string Text;
            xrsc::material_ref Material;
            if (!xeditor::cmd_util::GetArg(m_Parser, m_hMaterial, Text) || !ParseGuid(Text, Material)) return "SetMaterial: bad arguments";
            if (!m_Doc.isLoaded()) return "SetMaterial: nothing loaded";

            auto& D = static_cast<xmaterial_instance::descriptor&>(*m_Doc.m_pDescriptor);
            if (auto Err = LoadTemplate(D, Material); !Err.empty()) return "SetMaterial: " + Err;
            m_Doc.m_bDirty = true;
            return {};
        }

        // An inspector edit has already put the new material in the descriptor: the state to come back to has the previous one.
        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            std::string Text;
            xrsc::material_ref Before;
            if (!m_Doc.isLoaded()) { xeditor::WriteString(File, {}); return; }
            auto& D = static_cast<xmaterial_instance::descriptor&>(*m_Doc.m_pDescriptor);
            const auto Current = D.m_MaterialRef;
            if (xeditor::cmd_util::GetArg(m_Parser, m_hBefore, Text) && ParseGuid(Text, Before)) D.m_MaterialRef = Before;
            xeditor::WriteString(File, m_Doc.Snapshot());
            D.m_MaterialRef = Current;
        }

        void Undo(xundo::undo_file& File) noexcept override { m_Doc.Restore(xeditor::ReadString(File)); }

        xcmdline::parser::handle m_hMaterial, m_hBefore;
    };

    //--------------------------------------------------------------------------------------------
    // The editor
    //--------------------------------------------------------------------------------------------
    struct session : xeditor::descriptor_editor
    {
        set_material_cmd                m_SetMaterial;
        xeditor::mesh_preview           m_Preview;
        xeditor::mesh_preview_cmds      m_PreviewCmds;
        xeditor::texture_thumbnails     m_Thumbnails;
        xrsc::material_instance_ref     m_InstanceRef;
        std::function<void(xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, const xproperty::any&, ImGuiTreeNodeFlags, const char*, bool&)> m_TextureRowLabel;

        session(xresource::full_guid Guid, e10::library::guid LibraryGuid, xgpu::device* pDevice) noexcept
            : descriptor_editor("Material Instance", Guid, LibraryGuid, pDevice), m_SetMaterial(m_Undo, m_Document), m_PreviewCmds(m_Undo, m_Preview)
        {
            m_Thumbnails.Wire(m_DescriptorInspector.m_Inspector);
            WireTextureRowLabels();

            AddPanel("Material Instance Preview", dock::center, [this] { m_Preview.Render(); });
            AddPanel("Description",               dock::right,  [this] { m_DescriptorInspector.Show(); });

            if (pDevice && m_Preview.Init(*pDevice)) ReloadPreview();
        }

        ~session() noexcept override { xresource::g_Mgr.ReleaseRef(m_InstanceRef); }

        void OnCompiled() noexcept override { ReloadPreview(); }

        // Picking the material is one command, not a property edit: the defaults have to follow.
        bool OnCustomChange(const xproperty::ui::undo::cmd& Cmd) noexcept override
        {
            if (!Cmd.m_Name.ends_with("/MaterialRef") || !Cmd.m_NewValue.is<xresource::full_guid>()) return false;
            const auto Before = Cmd.m_Original.is<xresource::full_guid>() ? Cmd.m_Original.get<xresource::full_guid>().m_Instance.m_Value : 0;
            xeditor::Run(m_Undo, std::format("SetMaterial -Material {:016X} -Before {:016X}", Cmd.m_NewValue.get<xresource::full_guid>().m_Instance.m_Value, Before));
            return true;
        }

        // A texture slot is labelled with what it is for; one that is not the material's default is marked, and the mark resets it.
        void WireTextureRowLabels() noexcept
        {
            m_TextureRowLabel = [this](xproperty::inspector&, const xproperty::type::object&, void* pInstance, std::string_view Path, const xproperty::any&, ImGuiTreeNodeFlags Flags, const char* pName, bool& Open)
            {
                // The slot's row is as tall as the texture button beside it
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 18.0f));
                std::string NewName;
                bool bOverride = false;

                int Index = 0;
                const auto Bracket = Path.rfind('[');
                const auto Colon   = Bracket == std::string_view::npos ? std::string_view::npos : Path.find(':', Bracket);
                const auto Close   = Bracket == std::string_view::npos ? std::string_view::npos : Path.find(']', Bracket);
                const bool bElement = Path.find("/Textures[") != std::string_view::npos && Colon != std::string_view::npos && Close != std::string_view::npos && Colon < Close
                                   && std::from_chars(Path.data() + Colon + 1, Path.data() + Close, Index).ec == std::errc();

                if (bElement)
                {
                    auto* pDesc = static_cast<xmaterial_instance::descriptor*>(pInstance);
                    if (Index >= 0 && Index < static_cast<int>(pDesc->m_lTextureDefaults.size()) && Index < static_cast<int>(pDesc->m_lTextures.size()))
                    {
                        auto& Default = pDesc->m_lTextureDefaults[Index];
                        NewName = std::format("{} {}", pName, Default.m_Name);
                        pName   = NewName.c_str();
                        bOverride = Default.m_Index >= 0 && Default.m_Index < static_cast<int>(pDesc->m_lFinalTextures.size())
                                 && pDesc->m_lFinalTextures[Default.m_Index].m_TextureRef != pDesc->m_lTextures[Index];

                        if (bOverride)
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(170, 170, 255, 255));
                            if (ImGui::Button(">"))
                            {
                                const auto Default = pDesc->m_lFinalTextures[pDesc->m_lTextureDefaults[Index].m_Index].m_TextureRef;
                                xeditor::Run(m_Undo, std::format("SetProperty -Path {} -Value {}", xeditor::Base64Encode(std::string(Path))
                                    , xeditor::Base64Encode(std::format("{:X}, {:X}", Default.m_Instance.m_Value, xrsc::texture_type_guid_v.m_Value))));
                            }
                            ImGui::SameLine();
                        }
                    }
                }

                const void* pID = reinterpret_cast<const void*>(std::hash<std::string_view>{}(Path));
                Open = ImGui::TreeNodeEx(pID, ImGuiTreeNodeFlags_Framed | Flags, bOverride ? " %s" : "  %s", pName);
                if (bOverride) ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            };
            m_DescriptorInspector.m_Inspector.m_OnResourceLeftSize.m_Delegates.clear();
            m_DescriptorInspector.m_Inspector.m_OnResourceLeftSize.Register(m_TextureRowLabel);
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
            m_Preview.SetMaterial(pMaterial->getShader(), Bindings);
        }
    };

    inline const xeditor::auto_register_resource_editor g_Registration
    { xrsc::material_instance_type_guid_v
    , [](xresource::full_guid Guid, e10::library::guid LibraryGuid, xgpu::device* pDevice) -> std::unique_ptr<xeditor::resource_editor>
      { return std::make_unique<session>(Guid, LibraryGuid, pDevice); }
    };
}

#endif // XMATERIAL_INSTANCE_EDITOR_H
