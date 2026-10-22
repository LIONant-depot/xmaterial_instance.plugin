#ifndef XMATERIAL_INSTANCE_MIGRATE_H
#define XMATERIAL_INSTANCE_MIGRATE_H
#pragma once

// What the material's migration ops (SetParamDefault -Policy PreserveLook, RetypeParam; documentation/material_contract.md section 6) do to the instance files: read every
// descriptor made from the material through the descriptor API, change the overrides, write the ones that changed and queue their compile. Run by the commands of the material
// editor, which also snapshot the files (Snapshot / Restore) so an undo puts them back.
// ponytail: reads every descriptor of the project's MaterialInstance folder and rewrites the files on disk; an Instance editor that has the file open keeps its own copy until reopened.
#include "plugins/xmaterial_instance.plugin/source/xmaterial_intance_descriptor.h"
#include "dependencies/xresource_pipeline_v2/source/editor/xresource_editor_resources.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace xmaterial_instance
{
    inline int Components(xmaterial::param_type T) noexcept
    {
        using xmaterial::param_type;
        return T == param_type::FLOAT ? 1 : T == param_type::FLOAT2 ? 2 : T == param_type::FLOAT3 ? 3 : 4;
    }

    // RetypeParam -Convert: the leading components stay, the rest is zero; Splat fills the new ones with the first (a FLOAT becomes (x,x,x)). TEXTURE has no conversion.
    inline xmaterial::float4v Convert(const xmaterial::float4v& V, xmaterial::param_type From, xmaterial::param_type To, bool bSplat) noexcept
    {
        float In[4] = { V.m_X, V.m_Y, V.m_Z, V.m_W }, Out[4] = {};
        const int nFrom = Components(From), nTo = Components(To);
        for (int i = 0; i < nTo; ++i) Out[i] = i < nFrom ? In[i] : bSplat ? In[0] : 0.f;
        return { Out[0], Out[1], Out[2], Out[3] };
    }

    struct instance_file
    {
        std::uint64_t   m_Key = 0;
        descriptor      m_Desc;
        bool            m_bChanged = false;
    };

    // The folder of the descriptors ("<project>/Descriptors") from any descriptor path of the project
    inline std::wstring DescriptorsOf(const std::wstring& AnyDescriptorPath) noexcept
    {
        const auto At = AnyDescriptorPath.rfind(L"Material");
        return At == std::wstring::npos || At == 0 ? std::wstring{} : AnyDescriptorPath.substr(0, At - 1);
    }

    inline std::wstring DescriptorPath(const std::wstring& Descriptors, std::uint64_t Key) noexcept { return resolution::DescriptorPath(Descriptors, Key); }

    // Every instance made from Material (the root material is also declared by the instances that have a parent), read
    inline std::vector<std::unique_ptr<instance_file>> ReadInstances(const std::wstring& Descriptors, std::uint64_t Material) noexcept
    {
        std::vector<std::unique_ptr<instance_file>> Out;
        std::error_code Ec;
        const std::filesystem::path Folder = std::filesystem::path(Descriptors) / L"MaterialInstance";
        for (std::filesystem::recursive_directory_iterator It(Folder, Ec), End; !Ec && It != End; It.increment(Ec))
        {
            if (It->path().filename() != L"Descriptor.txt") continue;
            auto pFile = std::make_unique<instance_file>();
            pFile->m_Key = std::wcstoull(It->path().parent_path().stem().c_str(), nullptr, 16);
            xproperty::settings::context C;
            if (!pFile->m_Key || pFile->m_Desc.Serialize(true, DescriptorPath(Descriptors, pFile->m_Key), C) || pFile->m_Desc.m_MaterialRef.m_Instance.m_Value != Material) continue;
            Out.push_back(std::move(pFile));
        }
        return Out;
    }

    // Writes the files that changed and queues their compile
    inline int WriteChanged(const std::wstring& Descriptors, xresource_editor::library::guid Library, std::vector<std::unique_ptr<instance_file>>& Files) noexcept
    {
        int n = 0;
        for (auto& pFile : Files)
        {
            if (!pFile->m_bChanged) continue;
            xproperty::settings::context C;
            if (pFile->m_Desc.Serialize(false, DescriptorPath(Descriptors, pFile->m_Key), C)) continue;
            xresource_editor::g_LibMgr.MakeDescriptorDirty({ Library.m_Instance }, xresource::full_guid{ xresource::instance_guid{ pFile->m_Key }, xrsc::material_instance_type_guid_v });
            ++n;
        }
        return n;
    }

    // The instance files of a material as text, for the undo: "<key hex> <size>\n<bytes>" per file
    inline std::string Snapshot(const std::wstring& Descriptors, std::uint64_t Material) noexcept
    {
        std::string Out;
        for (auto& pFile : ReadInstances(Descriptors, Material))
        {
            std::ifstream In(std::filesystem::path(DescriptorPath(Descriptors, pFile->m_Key)), std::ios::binary);
            std::stringstream Text; Text << In.rdbuf();
            Out += std::format("{:X} {}\n", pFile->m_Key, Text.str().size()) + Text.str();
        }
        return Out;
    }

    inline void Restore(const std::wstring& Descriptors, xresource_editor::library::guid Library, const std::string& Text) noexcept
    {
        for (std::size_t At = 0; At < Text.size(); )
        {
            const auto Eol = Text.find('\n', At);
            std::uint64_t Key = 0; std::size_t Size = 0;
            if (Eol == std::string::npos || std::sscanf(Text.c_str() + At, "%llx %zu", reinterpret_cast<unsigned long long*>(&Key), &Size) != 2) return;
            {
                std::ofstream Out(std::filesystem::path(DescriptorPath(Descriptors, Key)), std::ios::binary | std::ios::trunc);
                Out.write(Text.data() + Eol + 1, static_cast<std::streamsize>(Size));
            }
            xresource_editor::g_LibMgr.MakeDescriptorDirty({ Library.m_Instance }, xresource::full_guid{ xresource::instance_guid{ Key }, xrsc::material_instance_type_guid_v });
            At = Eol + 1 + Size;
        }
    }

    // SetParamDefault -Policy PreserveLook: the resolver's helper decides who gets the old default (xmaterial_instance_resolve.h), the files get the entries. Returns the files changed.
    inline int PreserveLookFiles(std::vector<std::unique_ptr<instance_file>>& Files, std::uint64_t Material, const xmaterial::param_decl& Decl, const xmaterial::float4v& OldDefault) noexcept
    {
        std::vector<resolve_node> Nodes;
        for (auto& pFile : Files)
        {
            auto& N = Nodes.emplace_back();
            N.m_Key = pFile->m_Key; N.m_Parent = pFile->m_Desc.m_ParentRef.m_Instance.m_Value; N.m_Root = pFile->m_Desc.m_MaterialRef.m_Instance.m_Value;
            for (const auto& O : pFile->m_Desc.m_Overrides) N.m_Overrides.push_back({ O.m_ID, O.m_Type, O.m_Value });
            std::ranges::sort(N.m_Overrides, {}, &override_value::m_ID);
        }
        int n = 0;
        for (const auto Key : PreserveLook(Nodes, Material, Decl, OldDefault))
            for (auto& pFile : Files)
                if (pFile->m_Key == Key)
                {
                    auto& O = pFile->m_Desc.Set(Decl.m_ID);
                    O.m_Type = Decl.m_Type; O.m_Value = OldDefault; O.m_NameHint = Decl.m_Name;
                    pFile->m_bChanged = true; ++n;
                }
        return n;
    }

    // RetypeParam -Convert: the overrides of the old type become the new type with their value converted. Returns the overrides changed.
    inline int RetypeFiles(std::vector<std::unique_ptr<instance_file>>& Files, xmaterial::param_id ID, xmaterial::param_type From, xmaterial::param_type To, bool bSplat) noexcept
    {
        int n = 0;
        for (auto& pFile : Files)
            if (auto* pO = const_cast<override_entry*>(pFile->m_Desc.Find(ID)); pO && pO->m_Type == From)
            {
                pO->m_Value = Convert(pO->m_Value, From, To, bSplat); pO->m_Type = To;
                pFile->m_bChanged = true; ++n;
            }
        return n;
    }
}

#endif // XMATERIAL_INSTANCE_MIGRATE_H
