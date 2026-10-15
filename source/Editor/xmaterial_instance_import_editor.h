#ifndef XMATERIAL_INSTANCE_IMPORT_EDITOR_H
#define XMATERIAL_INSTANCE_IMPORT_EDITOR_H
#pragma once

// The editor half of xmaterial_instance_import.h, for the geometry editors (static and skin): the material slots of a descriptor whose
// materials are imported from the model. A slot with an instance of the user's own is an override: its row shows the arrow the
// Material Instance editor shows for a texture, and the arrow gives the slot back its imported instance. The row's context menu
// copies the imported instance into a real resource of the project and makes it the override ("Duplicate as override"), the way to
// edit an imported material: the imported ones are made again by every compile.
//
// Commands (all undoable):
//      SetImportMaterials -On true|false           the import on or off: the slots nobody assigned take, or give back, the imported instances
//      DuplicateImportedMaterial -Slot i -Instance hex
//      RemoveUnusedImportedResources               takes away what the import made and the model no longer needs (xeditor_virtual_resources.h)
//      ApplyImportCompressions                     sets the compression of the generated textures to the ImportMaterials settings (undoable)
#include "source/Tools/Editor/xeditor_descriptor_editor.h"
#include "source/Tools/Editor/xeditor_virtual_resources.h"
#include "dependencies/xresource_pipeline_v2/source/editor/xresource_editor_commands_assets.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_import.h"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <optional>

namespace xmaterial_instance::import::editor
{
    // What a geometry descriptor has of the import
    struct slots
    {
        settings*                                   m_pSettings = nullptr;
        std::vector<std::string>                    m_Names;                // the model's material of each slot
        std::vector<bool>                           m_Unused;               // the slots the current nodes do not use (drawn dimmed)
        std::vector<xrsc::material_instance_ref>*   m_pRefs     = nullptr;
    };
    using get_slots_fn = slots(*)(xresource_pipeline::descriptor::base&);

    // The slot index of an inspector row path ("GeomStatic/MaterialInstance[G:3]"), -1 when the row is not a slot of this list
    inline int SlotIndex(std::string_view Path, std::string_view ListName) noexcept
    {
        const auto Start = Path.find(std::format("/{}[", ListName));
        if (Start == std::string_view::npos) return -1;
        const auto Bracket = Path.rfind('[');
        const auto Colon   = Path.find(':', Bracket);
        const auto Close   = Path.find(']', Bracket);
        if (Colon == std::string_view::npos || Close == std::string_view::npos || Colon > Close) return -1;
        int Index = -1;
        if (std::from_chars(Path.data() + Colon + 1, Path.data() + Close, Index).ec != std::errc()) return -1;
        return Index;
    }

    //--------------------------------------------------------------------------------------------
    // SetImportMaterials
    //--------------------------------------------------------------------------------------------
    struct set_import_cmd : xundo::command_base
    {
        xeditor::descriptor_document&   m_Doc;
        get_slots_fn                    m_GetSlots;

        set_import_cmd(xundo::system& System, xeditor::descriptor_document& Doc, get_slots_fn GetSlots) noexcept
            : command_base(System, "SetImportMaterials", nullptr), m_Doc(Doc), m_GetSlots(GetSlots) { RegisterArguments(); }

        const char* getCommandHelp() const noexcept override { return "Imports the materials and textures of the model or stops (undoable): the material slots nobody assigned take the imported instances, or give them back. Usage: SetImportMaterials -On true|false"; }
        void RegisterArguments() noexcept override { m_hOn = m_Parser.addOption("On", "true or false", true, 1); }

        std::string Redo() noexcept override
        {
            std::string On;
            if (!xeditor::cmd_util::GetArg(m_Parser, m_hOn, On) || (On != "true" && On != "false" && On != "1" && On != "0")) return "SetImportMaterials: bad arguments";
            if (!m_Doc.isLoaded()) return "SetImportMaterials: nothing loaded";

            auto S = m_GetSlots(*m_Doc.m_pDescriptor);
            S.m_pSettings->m_bImport = On == "true" || On == "1";
            SyncSlots(*S.m_pSettings, m_Doc.m_Guid.m_Instance, S.m_Names, *S.m_pRefs);
            m_Doc.m_bDirty = true;
            return {};
        }

        // An inspector edit has already flipped the checkbox: the state to come back to has it the other way
        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            std::string On;
            if (!m_Doc.isLoaded() || !xeditor::cmd_util::GetArg(m_Parser, m_hOn, On)) { xeditor::WriteString(File, {}); return; }
            auto S = m_GetSlots(*m_Doc.m_pDescriptor);
            const bool bNew     = On == "true" || On == "1";
            const bool bCurrent = S.m_pSettings->m_bImport;
            if (bCurrent == bNew) S.m_pSettings->m_bImport = !bNew;
            xeditor::WriteString(File, m_Doc.Snapshot());
            S.m_pSettings->m_bImport = bCurrent;
        }

        void Undo(xundo::undo_file& File) noexcept override { if (auto Text = xeditor::ReadString(File); !Text.empty()) m_Doc.Restore(Text); }

        xcmdline::parser::handle m_hOn;
    };

    //--------------------------------------------------------------------------------------------
    // DuplicateImportedMaterial: the slot's imported instance, copied into a resource of the project (next to the geometry), becomes the slot's
    // override. The instance guid is minted by the caller so that a redo makes the same resource. Undo puts the slot back and the copy in the trash.
    //--------------------------------------------------------------------------------------------
    struct duplicate_cmd : xundo::command_base
    {
        xeditor::descriptor_document&   m_Doc;
        get_slots_fn                    m_GetSlots;

        duplicate_cmd(xundo::system& System, xeditor::descriptor_document& Doc, get_slots_fn GetSlots) noexcept
            : command_base(System, "DuplicateImportedMaterial", nullptr), m_Doc(Doc), m_GetSlots(GetSlots) { RegisterArguments(); }

        const char* getCommandHelp() const noexcept override { return "Copies the imported Material Instance of a slot into a resource of the project and makes it the slot's override (undoable). The geometry must have been compiled with the import on. Usage: DuplicateImportedMaterial -Slot index -Instance hexguid"; }
        void RegisterArguments() noexcept override
        {
            m_hSlot     = m_Parser.addOption("Slot",     "The material slot",                                   true, 1);
            m_hInstance = m_Parser.addOption("Instance", "The new resource's guid, 16 hex digits (odd)",        true, 1);
        }

        static std::wstring DescriptorFolder(const std::wstring& Root, std::uint64_t Value)
        {
            return std::format(L"{}\\MaterialInstance\\{:02X}\\{:02X}\\{:X}.desc", Root, Value & 0xFF, (Value >> 8) & 0xFF, Value);
        }

        std::string Redo() noexcept override
        {
            std::string SlotText, InstanceText;
            int Slot = -1; std::uint64_t Instance = 0;
            if (!xeditor::cmd_util::GetArg(m_Parser, m_hSlot, SlotText) || !xeditor::cmd_util::GetArg(m_Parser, m_hInstance, InstanceText)
             || std::from_chars(SlotText.data(), SlotText.data() + SlotText.size(), Slot).ec != std::errc()
             || std::from_chars(InstanceText.data(), InstanceText.data() + InstanceText.size(), Instance, 16).ec != std::errc() || (Instance & 1) == 0)
                return "DuplicateImportedMaterial: bad arguments";
            if (!m_Doc.isLoaded()) return "DuplicateImportedMaterial: nothing loaded";

            auto S = m_GetSlots(*m_Doc.m_pDescriptor);
            if (Slot < 0 || Slot >= static_cast<int>(S.m_pRefs->size()) || Slot >= static_cast<int>(S.m_Names.size())) return std::format("DuplicateImportedMaterial: no slot {}", Slot);
            if (!S.m_pSettings->m_bImport) return "DuplicateImportedMaterial: the materials are not imported";

            const auto& Project  = xresource_editor::g_LibMgr.m_ProjectPath;
            const auto  Imported = InstanceRef(m_Doc.m_Guid.m_Instance, S.m_Names[Slot]);
            const auto  Source   = DescriptorFolder(Project + L"\\Descriptors", Imported.m_Instance.m_Value) + L"\\Descriptor.txt";
            std::error_code Ec;
            if (!std::filesystem::exists(Source, Ec)) return "DuplicateImportedMaterial: the imported instance is not there yet (compile the geometry)";

            // Next to the geometry
            xresource::full_guid Parent{};
            xresource_editor::g_LibMgr.getInfo(m_Doc.m_LibraryGuid, m_Doc.m_Guid, [&](const xresource_pipeline::info& Info) { if (!Info.m_RscLinks.empty()) Parent = Info.m_RscLinks.front(); });

            const xresource::full_guid New{ xresource::instance_guid{ Instance }, xrsc::material_instance_type_guid_v };
            xresource_editor::commands::CreateOrRestoreAsset(m_Doc.m_LibraryGuid, New, Parent, S.m_Names[Slot]);

            const auto Target = DescriptorFolder(Project + L"\\Descriptors", Instance);
            std::filesystem::create_directories(Target, Ec);
            std::filesystem::copy_file(Source, Target + L"\\Descriptor.txt", std::filesystem::copy_options::overwrite_existing, Ec);
            if (Ec) return "DuplicateImportedMaterial: the imported instance could not be copied";
            xresource_editor::g_LibMgr.MakeDescriptorDirty(m_Doc.m_LibraryGuid, New);       // compile the copy

            (*S.m_pRefs)[Slot] = { .m_Instance = New.m_Instance };
            m_Doc.m_bDirty = true;
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            xeditor::WriteString(File, m_Doc.isLoaded() ? m_Doc.Snapshot() : std::string{});
            std::string InstanceText;
            xeditor::cmd_util::GetArg(m_Parser, m_hInstance, InstanceText);
            xeditor::WriteString(File, InstanceText);
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            if (auto Text = xeditor::ReadString(File); !Text.empty()) m_Doc.Restore(Text);
            std::uint64_t Instance = 0;
            const auto InstanceText = xeditor::ReadString(File);
            if (std::from_chars(InstanceText.data(), InstanceText.data() + InstanceText.size(), Instance, 16).ec == std::errc())
                xresource_editor::g_LibMgr.MoveToTrash(m_Doc.m_LibraryGuid, xresource::full_guid{ xresource::instance_guid{ Instance }, xrsc::material_instance_type_guid_v });
        }

        xcmdline::parser::handle m_hSlot, m_hInstance;
    };

    //--------------------------------------------------------------------------------------------
    // ApplyImportCompressions: the compression of a texture is chosen when it is made (settings::m_AlbedoCompression, ...) and is the user's after that.
    // This sets the textures the geometry made to the compressions of the settings (what the user chose for them is overwritten): by what they hold, a color
    // map, a color map that cuts out, a normal map, the ORME. Textures of a constant are left uncompressed. Undo writes the descriptors back.
    //--------------------------------------------------------------------------------------------
    struct apply_compressions_cmd : xundo::command_base
    {
        xeditor::descriptor_document&   m_Doc;
        get_slots_fn                    m_GetSlots;

        apply_compressions_cmd(xundo::system& System, xeditor::descriptor_document& Doc, get_slots_fn GetSlots) noexcept
            : command_base(System, "ApplyImportCompressions", nullptr), m_Doc(Doc), m_GetSlots(GetSlots) { RegisterArguments(); }

        const char* getCommandHelp() const noexcept override { return "Sets the compression of the generated textures to the ones of the ImportMaterials settings, overwriting what was chosen for them (undoable). Usage: ApplyImportCompressions"; }
        void RegisterArguments() noexcept override {}

        // What the settings say for this texture: none for a texture that is not one of the roles (or made of constants)
        static std::optional<xtexture_rsc::compression_format> Wanted(const settings& S, const xtexture_rsc::descriptor& D) noexcept
        {
            if (const auto* pMix = std::get_if<xtexture_rsc::mix_source>(&D.m_InputVariant))
            {
                bool bConstant = true;
                for (auto& M : pMix->m_Inputs) bConstant = bConstant && M.m_CopyFrom == xtexture_rsc::compositing_from::CUSTOM_CONSTANT;
                if (bConstant) return std::nullopt;
            }
            switch (D.m_UsageType)
            {
            case xtexture_rsc::usage_type::COLOR:           return S.Albedo();
            case xtexture_rsc::usage_type::TANGENT_NORMAL:  return S.Normal();
            case xtexture_rsc::usage_type::COLOR_AND_ALPHA: return D.m_bSRGB ? S.AlbedoAlpha() : S.ORME();      // the ORME is linear
            default:                                        return std::nullopt;
            }
        }

        struct change { std::wstring m_Path; std::string m_Before; };

        // The descriptors that would change, with their text now
        std::vector<change> Changes(std::vector<std::pair<std::wstring, xtexture_rsc::compression_format>>* pTargets = nullptr) const noexcept
        {
            std::vector<change> List;
            if (!m_Doc.isLoaded()) return List;
            const auto S = m_GetSlots(*m_Doc.m_pDescriptor);
            for (auto& Guid : xresource_editor::g_LibMgr.VirtualDescendantsOf(m_Doc.m_LibraryGuid, m_Doc.m_Guid))
            {
                if (Guid.m_Type != xrsc::texture_type_guid_v) continue;
                std::wstring InfoPath;
                xresource_editor::g_LibMgr.getNodeInfo(m_Doc.m_LibraryGuid, Guid, [&](xresource_editor::library_db::info_node& Node) { InfoPath = Node.m_Path; });
                if (InfoPath.empty()) continue;

                const auto Path = (std::filesystem::path(InfoPath).parent_path() / L"Descriptor.txt").wstring();
                xtexture_rsc::descriptor D;
                xproperty::settings::context Context;
                if (D.Serialize(true, Path, Context)) continue;
                const auto Want = Wanted(*S.m_pSettings, D);
                if (!Want || *Want == D.m_Compression) continue;

                std::ifstream In(std::filesystem::path(Path), std::ios::binary);
                List.push_back({ Path, std::string(std::istreambuf_iterator<char>(In), {}) });
                if (pTargets) pTargets->push_back({ Path, *Want });
            }
            return List;
        }

        std::string Redo() noexcept override
        {
            std::vector<std::pair<std::wstring, xtexture_rsc::compression_format>> Targets;
            if (Changes(&Targets).empty()) return "ApplyImportCompressions: the generated textures already have these compressions";
            for (auto& [Path, Format] : Targets)
            {
                xtexture_rsc::descriptor D;
                xproperty::settings::context Context;
                if (D.Serialize(true, Path, Context)) continue;
                D.m_Compression = Format;
                xproperty::settings::context Out;
                D.Serialize(false, Path, Out);                  // the file watcher compiles the texture again
            }
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            const auto List = Changes();
            xeditor::WriteString(File, std::to_string(List.size()));
            for (auto& C : List) { xeditor::WriteString(File, xstrtool::To(C.m_Path)); xeditor::WriteString(File, C.m_Before); }
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            const int Count = std::atoi(xeditor::ReadString(File).c_str());
            for (int i = 0; i < Count; ++i)
            {
                const auto Path = std::filesystem::path(xstrtool::To(xeditor::ReadString(File)));
                const auto Text = xeditor::ReadString(File);
                std::ofstream Out(Path, std::ios::binary | std::ios::trunc);
                Out.write(Text.data(), static_cast<std::streamsize>(Text.size()));
            }
        }
    };

    //--------------------------------------------------------------------------------------------
    // What a geometry editor holds: the commands, and the drawing of a slot row's override arrow and context menu
    //--------------------------------------------------------------------------------------------
    struct session_part
    {
        xeditor::descriptor_document&   m_Doc;
        xundo::system&                  m_Undo;
        get_slots_fn                    m_GetSlots;
        set_import_cmd                  m_SetImport;
        duplicate_cmd                   m_Duplicate;
        xeditor::virtual_resources::remove_unused_cmd   m_RemoveUnused;
        apply_compressions_cmd                          m_ApplyCompressions;
        bool                                            m_bAskApply = false;
        xeditor::virtual_resources::unused_button       m_UnusedButton;

        session_part(xundo::system& Undo, xeditor::descriptor_document& Doc, get_slots_fn GetSlots) noexcept
            : m_Doc(Doc), m_Undo(Undo), m_GetSlots(GetSlots), m_SetImport(Undo, Doc, GetSlots), m_Duplicate(Undo, Doc, GetSlots), m_RemoveUnused(Undo, Doc), m_ApplyCompressions(Undo, Doc, GetSlots) {}

        // Above the Description inspector: the button that removes what the import made and the model no longer needs
        void DrawUnusedButton() noexcept
        {
            if (!m_Doc.isLoaded()) return;
            m_UnusedButton.Draw(m_Undo, m_Doc);

            // The textures made so far keep the compression they were made with: this puts the settings' on them (once in a while, so it is not checked every frame)
            if (m_GetSlots(*m_Doc.m_pDescriptor).m_pSettings->m_bImport && ImGui::Button("Apply the import compressions to the generated textures###ApplyImportCompressions"))
                ImGui::OpenPopup("Apply the compressions?###ApplyImportCompressionsPopup");
            if (ImGui::BeginPopupModal("Apply the compressions?###ApplyImportCompressionsPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("The generated textures take the compressions of the ImportMaterials settings.");
                ImGui::TextUnformatted("What you chose for them is overwritten. This can be undone.");
                if (ImGui::Button("Apply")) { xeditor::Run(m_Undo, "ApplyImportCompressions"); ImGui::CloseCurrentPopup(); }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }

        // The checkbox is one command, not a property edit: the slots follow it
        bool OnCustomChange(const xproperty::ui::undo::cmd& Cmd) noexcept
        {
            if (!Cmd.m_Name.ends_with("/bImport") || !Cmd.m_NewValue.is<bool>()) return false;
            xeditor::Run(m_Undo, std::format("SetImportMaterials -On {}", Cmd.m_NewValue.get<bool>() ? "true" : "false"));
            return true;
        }

        // The inspector rows of the material slots: labelled with the material's name, dimmed when the current nodes no longer use it, with the
        // arrow that dismisses an override and the context menu that duplicates the imported instance. (Matched by path: the delegate is called for
        // every resource row, and the property table object differs between translation units, so its address says nothing.)
        void RegisterRowLabels(xproperty::inspector& Inspector, std::string_view ListName = "MaterialInstance") noexcept
        {
            m_ListName = ListName;
            m_RowLabel = [this](xproperty::inspector& Inspector, const xproperty::type::object&, void*, std::string_view Path, const xproperty::any&, ImGuiTreeNodeFlags Flags, const char* pName, bool& Open)
            {
                std::string NewName;
                const auto  S     = m_Doc.isLoaded() ? m_GetSlots(*m_Doc.m_pDescriptor) : slots{};
                const int   Index = SlotIndex(Path, m_ListName);
                const bool  bSlot = S.m_pRefs && Index >= 0 && Index < static_cast<int>(S.m_Names.size()) && Index < static_cast<int>(S.m_pRefs->size());
                const bool  bDisable  = bSlot && Index < static_cast<int>(S.m_Unused.size()) && S.m_Unused[Index];
                const bool  bOverride = bSlot && isOverride(*S.m_pSettings, m_Doc.m_Guid.m_Instance, S.m_Names[Index], (*S.m_pRefs)[Index]);
                if (bSlot) { NewName = std::format("{} {}", pName, S.m_Names[Index]); pName = NewName.c_str(); }

                if (bDisable) ImGui::BeginDisabled(true);
                // As tall as the reference widget beside the label (whoever draws the label, a reference row has one height): the label and the
                // override arrow both, from the top of the row to its bottom
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, xproperty::inspector::ResourceRowFramePadding(Inspector.m_CurrentProperty.m_Flags.m_bSmallResource));
                if (bOverride)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(170, 170, 255, 255));
                    ImGui::PushID(Index);
                    if (ImGui::Button(">"))
                    {
                        const auto Imported = InstanceRef(m_Doc.m_Guid.m_Instance, S.m_Names[Index]);
                        xeditor::Run(m_Undo, std::format("SetProperty -Path {} -Value {}", xeditor::Quote(std::string(Path))
                            , xeditor::Quote(std::format("{:X}, {:X}", Imported.m_Instance.m_Value, xrsc::material_instance_type_guid_v.m_Value))));
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Overridden: click to use the imported material again");
                    ImGui::PopID();
                    ImGui::SameLine();
                }

                // The "  " (two leading spaces) gives an ordinary resource-ref row a little breathing room against the Framed box's own left edge.
                // A slot row sits in the array-controls cluster (drag/insert/delete icons right to its left): no padding there.
                if (!Path.empty()) Open = ImGui::TreeNodeEx(reinterpret_cast<const void*>(std::hash<std::string_view>{}(Path)), ImGuiTreeNodeFlags_Framed | Flags, bSlot ? "%s" : "  %s", pName);
                else               Open = ImGui::TreeNodeEx(pName, Flags);
                ImGui::PopStyleVar();
                if (bOverride) ImGui::PopStyleColor();

                if (bSlot && S.m_pSettings->m_bImport)
                {
                    ImGui::PushID(Index);
                    if (ImGui::BeginPopupContextItem("ImportedMaterialSlot"))
                    {
                        if (ImGui::MenuItem("Duplicate as override"))
                            xeditor::Run(m_Undo, std::format("DuplicateImportedMaterial -Slot {} -Instance {:016X}", Index, xresource::instance_guid::GenerateGUIDCopy().m_Value));
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                if (bDisable) ImGui::EndDisabled();
            };
            Inspector.m_OnResourceLeftSize.m_Delegates.clear();
            Inspector.m_OnResourceLeftSize.Register(m_RowLabel);
        }

        std::string     m_ListName;
        std::function<void(xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, const xproperty::any&, ImGuiTreeNodeFlags, const char*, bool&)> m_RowLabel;
    };
}

#endif
