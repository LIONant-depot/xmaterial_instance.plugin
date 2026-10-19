#ifndef XMATERIAL_INSTANCE_IMPORT_COMPILER_H
#define XMATERIAL_INSTANCE_IMPORT_COMPILER_H
#pragma once

// The compiler half of xmaterial_instance_import.h: a geometry compiler hands the materials of its model to emit() and gets one virtual
// Material Instance per material (made from the PBR material of the settings) with virtual Textures in its slots:
//      SamplerAlbedo   the color map (its alpha, or the opacity map, cuts the surface out: then the instance uses the alpha test material),
//                      or the diffuse color when the model has none, or magenta when the one it names is not found (each a warning, and their sum)
//      SamplerNormal   the normal map (an OBJ "bump" map named like a normal map counts), or a flat normal
//      SamplerORME     occlusion, roughness, metalness and emission packed from the PBR maps of the model, or constants: roughness from the
//                      shininess, emission from the emissive color
// Every resource is written in Descriptors only when its text changed, so recompiling the geometry does not recompile its textures. What exists already
// is the user's (xresource_pipeline_virtual.h): the import makes what is missing and never updates what exists, a Material Instance (its base material, slots
// and params) or a Texture (its guid is made from its input: the same texture has the same input, so what it holds is the user's). The exceptions: a slot
// that still holds the magenta placeholder of a missing texture, or only constants (no map was there), takes the texture once it is found. Reapplying the import is explicit (the editor's buttons).
// The includer defines STB_IMAGE_IMPLEMENTATION once (the alpha and size checks read the images).
#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline_virtual.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_import.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_intance_descriptor.h"
#include "plugins/xtexture.plugin/source/xtexture_rsc_descriptor.h"
#include "dependencies/xraw3d/source/xraw3d.h"
#include "dependencies/stb/stb_image.h"

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <vector>

namespace xmaterial_instance::import
{
    struct emitter
    {
        std::wstring                        m_ProjectPath;          // the project's root
        std::wstring                        m_Descriptors;          // where the descriptors of virtual resources live: the project's Descriptors (xresource_pipeline project_paths::m_Descriptors)
        std::wstring                        m_OwnerFolder;          // the descriptor folder of the geometry: where the list of what it made is kept (Virtual.txt)
        xresource::full_guid                m_Owner;                // the geometry
        std::wstring                        m_ModelPath;            // the model, relative to the project (its textures are relative to it)
        const settings&                     m_Settings;
        xresource_pipeline::dependencies&   m_Dependencies;
        std::function<void(std::string)>    m_Warning;
        std::function<void(std::string)>    m_Info;                 // optional: where a texture the model names was found, when it was looked for

        // One instance per material, in the order of the list
        void Emit(const std::vector<xraw3d::geom::material_instance>& Materials) noexcept
        {
            m_bHadList = m_Previous.Load(m_OwnerFolder);
            for (auto& M : Materials) EmitMaterial(M);

            // The textures the model names that are not there: each was warned of where it was looked for, and the sum of them (a problem of the compile)
            if (!m_Missing.empty())
            {
                std::string First;
                for (std::size_t i = 0; i < m_Missing.size() && i < 5; ++i) First += (i ? ", " : "") + m_Missing[i];
                m_Warning(std::format("Import Materials: {} textures missing (their materials show magenta); first few: {}{}", m_Missing.size(), First, m_Missing.size() > 5 ? ", ..." : ""));
            }

            // What this compile made. What an earlier compile made and this one does not stays on disk, unused (the editor does not compile it)
            xresource_pipeline::virtual_rsc::owned_list Made;
            Made.m_Resources = m_Dependencies.m_VirtualResources;
            Made.Save(m_OwnerFolder);
        }

        //------------------------------------------------------------------------------------
        // (public only so that the emitter stays an aggregate the compilers fill by name)

        struct image_info { int m_Width = 0, m_Height = 0, m_Channels = 0; };

        std::map<std::string, xrsc::texture_ref>    m_Textures;     // by what they are made of: a texture several materials share is made once
        std::vector<std::string>                    m_Missing;      // the textures the model names that Resolve did not find (once each)

        xresource_pipeline::virtual_rsc::owned_list m_Previous;     // what the earlier compile of the geometry made (Virtual.txt)
        bool                                        m_bHadList = false;

        static std::string Param(const xraw3d::geom::material_instance& M, std::string_view Name) noexcept
        {
            for (auto& P : M.m_Params) if (P.m_Name == Name) return P.m_Value;
            return {};
        }

        static std::array<float, 4> Floats(const std::string& Text, std::array<float, 4> Default) noexcept
        {
            std::array<float, 4> Out = Default;
            std::stringstream    Stream(Text);
            std::string          Item;
            for (int i = 0; i < 4 && std::getline(Stream, Item, ','); ++i) Out[i] = std::strtof(Item.c_str(), nullptr);
            return Out;
        }

        std::wstring Full(const std::wstring& ProjectRelative) const noexcept { return m_ProjectPath + L"\\" + ProjectRelative; }

        // A file name in lower case (what towlower knows), with .jpeg read as .jpg and .tiff as .tif: two names with the same key are the same texture
        static std::wstring Fold(std::wstring Text) noexcept
        {
            for (auto& c : Text) c = static_cast<wchar_t>(std::towlower(c));
            return Text;
        }
        static std::wstring NameKey(const std::filesystem::path& Name) noexcept
        {
            auto Ext = Fold(Name.extension().wstring());
            if      (Ext == L".jpeg") Ext = L".jpg";
            else if (Ext == L".tiff") Ext = L".tif";
            return Fold(Name.stem().wstring()) + Ext;
        }
        static bool IsImage(const std::filesystem::path& Name) noexcept
        {
            const auto Ext = Fold(Name.extension().wstring());
            for (const wchar_t* pExt : { L".png", L".jpg", L".jpeg", L".tga", L".bmp", L".tif", L".tiff", L".dds", L".hdr", L".exr", L".psd", L".gif", L".webp" })
                if (Ext == pExt) return true;
            return false;
        }
        // NameKey without what a file manager or a download adds to a copy's name: "x - Copy", "x - Copy (2)", "x (1)", "x_copy", "x-copy". The last resort of Resolve:
        // the texture the model names is next to it under such a name
        static std::wstring CopyKey(const std::filesystem::path& Name) noexcept
        {
            std::wstring Stem = Fold(Name.stem().wstring());
            const auto   Strip = [&]
            {
                if (Stem.ends_with(L")"))
                    if (const auto Open = Stem.rfind(L" ("); Open != std::wstring::npos && Open + 3 < Stem.size()
                        && std::all_of(Stem.begin() + Open + 2, Stem.end() - 1, [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
                    { Stem.erase(Open); return true; }
                for (const wchar_t* pSuffix : { L" - copy", L" copy", L"_copy", L"-copy" })
                    if (const std::wstring_view Suffix = pSuffix; Stem.ends_with(Suffix)) { Stem.erase(Stem.size() - Suffix.size()); return true; }
                return false;
            };
            while (Strip()) {}
            auto Ext = Fold(Name.extension().wstring());
            if      (Ext == L".jpeg") Ext = L".jpg";
            else if (Ext == L".tiff") Ext = L".tif";
            return Stem + Ext;
        }
        // A map the model does not name, found by the way artists name them: next to the color map, "<base>_<role>.<ext>" for a color map "<base>_<Diffuse|Albedo|BaseColor...>.<ext>"
        // ("Ch03_1001_Diffuse.png" and "Ch03_1001_Glossiness.png"; a copy's name counts, CopyKey). Tokens: the role's names, the first that has a file wins. Empty when there is none
        std::wstring Sibling(const std::wstring& ColorMap, std::initializer_list<const wchar_t*> Tokens) const noexcept
        {
            namespace fs = std::filesystem;
            if (ColorMap.empty()) return {};
            const fs::path Color = fs::path(m_ProjectPath) / ColorMap;
            std::wstring   Base  = fs::path(CopyKey(Color.filename())).stem().wstring();
            for (const wchar_t* pRole : { L"basecolor", L"base_color", L"albedo", L"diffuse", L"colour", L"color", L"diff", L"col" })
                if (const std::wstring_view Role = pRole; Base.ends_with(Role) && Base.size() > Role.size())
                {
                    Base.erase(Base.size() - Role.size());
                    while (!Base.empty() && (Base.back() == L'_' || Base.back() == L'-' || Base.back() == L' ' || Base.back() == L'.')) Base.pop_back();
                    break;
                }
            if (Base.empty()) return {};
            std::error_code Ec;
            for (const wchar_t* pToken : Tokens)
                for (auto It = fs::directory_iterator(Color.parent_path(), Ec); !Ec && It != fs::directory_iterator(); It.increment(Ec))
                {
                    std::error_code F;
                    if (!It->is_regular_file(F)) continue;
                    const std::wstring Stem = fs::path(CopyKey(It->path().filename())).stem().wstring();
                    for (const wchar_t* pSeparator : { L"_", L"-", L" ", L"" })
                        if (Stem == Base + pSeparator + pToken)
                        {
                            const auto Relative = fs::relative(It->path(), fs::path(m_ProjectPath), F).lexically_normal().make_preferred().wstring();
                            if (!F && !Relative.starts_with(L"..")) return Relative;
                        }
                }
            return {};
        }
        static std::string UTF8(const std::filesystem::path& Path) noexcept { return xstrtool::UTF16ToUTF8(Path.wstring()); }

        // A texture path of the model, as the project sees it ("Assets\...", empty when it is not found or not in the project). The model's paths are
        // UTF-8, with \ or /, and exporters write paths of the artist's machine: absolute, or relative to a folder that is not there ("Model.fbm\x.png"
        // with the textures copied to a "textures" folder next to the model's). The first that exists is taken:
        //  1. the path as given: absolute, or relative to the model's folder
        //  2. its file name in the model's folder
        //  3. its file name in the usual texture folders of the model's folder and of its parent: textures, texture, tex, images, image, maps (any case),
        //     <model>.fbm and the folder the path names (an FBX exporter's .fbm)
        //  4. its file name anywhere under the asset's folder (the parent of the model's folder when that is "source", "sources" or "src", else the
        //     model's folder; and when the path climbs out of it, "..\BuildingTextures\x.png", the folder it climbs to: an OBJ's .mtl names its
        //     textures relative to itself; never above the project), the one closest to the model's folder first
        //  5. failing all that, the same search for the same name in another image format ("x.png" for "x.tga": a texture converted after the export)
        //  6. failing that, the same search for a file whose name is a copy's of it ("x - Copy.png" for "x.png", either way: CopyKey)
        // A file name matches whatever its case, and .jpg/.jpeg (.tif/.tiff) match each other (NameKey). What a search found is logged (m_Info).
        std::wstring Resolve(const std::string& Raw) noexcept
        {
            if (Raw.empty()) return {};
            const auto Missing = [&] { if (std::ranges::find(m_Missing, Raw) == m_Missing.end()) m_Missing.push_back(Raw); return std::wstring{}; };
            if (Raw[0] == '*') { m_Warning(std::format("Import Materials: the texture {} is embedded in the model, it is not imported", Raw)); return Missing(); }

            namespace fs = std::filesystem;
            std::error_code Ec;
            const fs::path  Project  = fs::path(m_ProjectPath).lexically_normal();
            const fs::path  ModelDir = (Project / m_ModelPath).lexically_normal().parent_path();

            // UTF-8 to wide: a narrow std::filesystem::path would read the bytes as the code page (on Windows), and lose any name that is not ASCII
            std::wstring Wide = xstrtool::UTF8ToUTF16(Raw);
            for (auto& c : Wide) if (c == L'\\' || c == L'/') c = static_cast<wchar_t>(fs::path::preferred_separator);
            const fs::path Path = fs::path(Wide).lexically_normal();
            const auto     Key  = NameKey(Path.filename());

            const auto IsFile = [](const fs::path& P) { std::error_code E; return fs::is_regular_file(P, E); };

            // The file of that name in Dir (any case), each folder looked in once
            std::vector<fs::path> Searched;
            const auto InFolder = [&](const fs::path& Dir) -> fs::path
            {
                std::error_code E;
                if (Dir.empty() || std::ranges::find(Searched, Dir) != Searched.end() || !fs::is_directory(Dir, E)) return {};
                Searched.push_back(Dir);
                for (auto It = fs::directory_iterator(Dir, E); !E && It != fs::directory_iterator(); It.increment(E))
                {
                    std::error_code F;
                    if (It->is_regular_file(F) && NameKey(It->path().filename()) == Key) return It->path();
                }
                return {};
            };

            fs::path Found;
            if      (Path.is_absolute()) { if (IsFile(Path)) Found = Path; }
            else if (IsFile(ModelDir / Path)) Found = ModelDir / Path;
            const bool bAsGiven = !Found.empty();

            // 2.
            if (Found.empty()) Found = InFolder(ModelDir);

            // 3.
            if (Found.empty())
            {
                const std::wstring Names[] = { L"textures", L"texture", L"tex", L"images", L"image", L"maps"
                                             , Fold(fs::path(m_ModelPath).stem().wstring() + L".fbm"), Fold(Path.parent_path().filename().wstring()) };
                for (const fs::path& Base : { ModelDir, ModelDir.parent_path() })
                {
                    std::error_code E;
                    for (auto It = fs::directory_iterator(Base, E); Found.empty() && !E && It != fs::directory_iterator(); It.increment(E))
                    {
                        std::error_code F;
                        if (!It->is_directory(F)) continue;
                        const auto Name = Fold(It->path().filename().wstring());
                        if (!Name.empty() && std::ranges::find(Names, Name) != std::end(Names)) Found = InFolder(It->path());
                    }
                    if (!Found.empty()) break;
                }
            }

            // 4.
            fs::path Root = ModelDir;
            if (const auto Name = Fold(ModelDir.filename().wstring()); Name == L"source" || Name == L"sources" || Name == L"src")
                if (const auto Up = ModelDir.parent_path(); !Up.lexically_relative(Project).empty() && !Up.lexically_relative(Project).wstring().starts_with(L".."))
                    Root = Up;
            // A relative path that climbs out of Root (its folder may not be there: the textures were moved to a sibling folder) widens the search to
            // where it climbs: the folder both Root and the path's folder are under
            if (!Path.is_absolute())
            {
                const fs::path Named = (ModelDir / Path).lexically_normal().parent_path();
                fs::path       Common;
                for (auto A = Root.begin(), B = Named.begin(); A != Root.end() && B != Named.end() && Fold(A->wstring()) == Fold(B->wstring()); ++A, ++B) Common /= *A;
                if (const auto Rel = Common.lexically_relative(Project); !Common.empty() && !Rel.empty() && !Rel.wstring().starts_with(L".."))
                    Root = Common;
            }
            const auto Closest = [&](auto&& KeyOf, const std::wstring& Want)
            {
                fs::path    Best;
                std::size_t BestDistance = SIZE_MAX;
                for (auto It = fs::recursive_directory_iterator(Root, fs::directory_options::skip_permission_denied, Ec); !Ec && It != fs::recursive_directory_iterator(); It.increment(Ec))
                {
                    std::error_code F;
                    if (!It->is_regular_file(F) || KeyOf(It->path().filename()) != Want) continue;
                    std::size_t Distance = 0;
                    for (const auto& Part : It->path().parent_path().lexically_relative(ModelDir)) { (void)Part; ++Distance; }
                    if (Distance < BestDistance || (Distance == BestDistance && It->path() < Best)) { BestDistance = Distance; Best = It->path(); }
                }
                return Best;
            };
            if (Found.empty()) Found = Closest([](const fs::path& N) { return NameKey(N); }, Key);
            // 5. the same name in another image format (the model names "x.tga", the textures were converted to "x.png")
            if (Found.empty())
                Found = Closest([](const fs::path& N) { return IsImage(N) ? Fold(N.stem().wstring()) : std::wstring(); }, Fold(Path.stem().wstring()));
            // 6.
            if (Found.empty()) Found = Closest([](const fs::path& N) { return CopyKey(N); }, CopyKey(Path.filename()));

            if (Found.empty())
            {
                std::string Where;
                for (const auto& Dir : Searched) Where += (Where.empty() ? "" : ", ") + UTF8(Dir);
                m_Warning(std::format("Import Materials: texture {} not found (looked in {}{}and in every folder under {})", Raw, Where, Where.empty() ? "" : ", ", UTF8(Root)));
                return Missing();
            }
            if (!bAsGiven && m_Info) m_Info(std::format("Import Materials: texture {} found as {}", Raw, UTF8(Found)));

            const auto Relative = fs::relative(Found, Project, Ec).lexically_normal().make_preferred().wstring();
            if (Ec || Relative.starts_with(L"..")) { m_Warning(std::format("Import Materials: texture {} is outside the project", UTF8(Found))); return Missing(); }
            return Relative;
        }

        // stb opens a file by a narrow name, which on Windows is the code page (not UTF-8): the file is opened here by its wide name
        static FILE* OpenImage(const std::wstring& Path) noexcept
        {
        #if defined(_WIN32)
            FILE* pFile = nullptr;
            return _wfopen_s(&pFile, Path.c_str(), L"rb") == 0 ? pFile : nullptr;
        #else
            return std::fopen(xstrtool::UTF16ToUTF8(Path).c_str(), "rb");
        #endif
        }

        image_info Info(const std::wstring& Path) const noexcept
        {
            image_info I;
            FILE* pFile = OpenImage(Full(Path));
            if (!pFile) return I;
            if (!stbi_info_from_file(pFile, &I.m_Width, &I.m_Height, &I.m_Channels)) I = {};
            std::fclose(pFile);
            return I;
        }

        // Whether a color map with an alpha channel uses it (an exporter gives the color map as the opacity map whenever it has one)
        bool HasCutOut(const std::wstring& Path) const noexcept
        {
            if (Info(Path).m_Channels != 4) return false;
            FILE* pFile = OpenImage(Full(Path));
            if (!pFile) return false;
            int W, H, C;
            auto* pData = stbi_load_from_file(pFile, &W, &H, &C, 4);
            std::fclose(pFile);
            if (!pData) return false;
            bool bCut = false;
            for (int i = 0, n = W * H; i < n && !bCut; ++i) bCut = pData[i * 4 + 3] < 128;
            stbi_image_free(pData);
            return bCut;
        }

        static std::string GuidHex(std::uint64_t Value) { return std::format("{:016X}", Value); }

        // Named like every resource folder: the guid without leading zeros (xresource_mgr loads by {:X})
        std::wstring DescriptorFolder(std::string_view Type, std::uint64_t Value) const noexcept
        {
            return std::format(L"{}\\{}\\{:02X}\\{:02X}\\{:X}.desc", m_Descriptors, xstrtool::To(Type), Value & 0xFF, (Value >> 8) & 0xFF, Value);
        }

        template<typename T_WRITE>
        static void WriteIfChanged(const std::wstring& Path, T_WRITE&& Write) noexcept { xresource_pipeline::virtual_rsc::WriteIfChanged(Path, std::forward<T_WRITE>(Write)); }

        // Whether the texture of a slot is gone: its descriptor does not exist (deleted as unused, or never made)
        bool IsGoneTexture(const xrsc::texture_ref Texture) const noexcept
        {
            std::error_code Ec;
            return !std::filesystem::exists(DescriptorFolder("Texture", Texture.m_Instance.m_Value) + L"\\Descriptor.txt", Ec);
        }

        // Whether a texture is only constants (no file in any of its mixes): what the import makes for a map the model does not have (a flat normal, the
        // constant ORME, the diffuse color)
        bool IsConstantTexture(const xrsc::texture_ref Texture) const noexcept
        {
            xtexture_rsc::descriptor     Descriptor;
            xproperty::settings::context C;
            if (Descriptor.Serialize(true, DescriptorFolder("Texture", Texture.m_Instance.m_Value) + L"\\Descriptor.txt", C)) return false;
            const auto* pMix = std::get_if<xtexture_rsc::mix_source>(&Descriptor.m_InputVariant);
            return pMix && std::ranges::all_of(pMix->m_Inputs, [](const xtexture_rsc::mix& E) { return E.m_CopyFrom == xtexture_rsc::compositing_from::CUSTOM_CONSTANT; });
        }

        // Whether the generator made this texture: it is on the list of the earlier compile; with no list yet (a project from before the lists) a texture
        // whose Info links a Material Instance that links the geometry
        bool IsGenerated(const xrsc::texture_ref Texture) noexcept
        {
            const xresource::full_guid Guid{ Texture.m_Instance, xtexture_rsc::resource_type_guid_v };
            if (m_Previous.Contains(Guid)) return true;
            if (m_bHadList) return false;

            auto Parents = [&](std::string_view Type, xresource::full_guid G)
            {
                xresource_pipeline::info Info;
                xproperty::settings::context C;
                if (Info.Serialize(true, DescriptorFolder(Type, G.m_Instance.m_Value) + L"\\Info.txt", C)) return std::vector<xresource::full_guid>{};
                return Info.m_RscLinks;
            };
            for (auto& Instance : Parents("Texture", Guid))
                for (auto& Owner : Parents("MaterialInstance", Instance))
                    if (Owner == m_Owner) return true;
            return false;
        }

        // A virtual texture, made once per key
        xrsc::texture_ref Texture(const std::string& Key, const std::string& Name, xresource::full_guid Parent, std::function<void(xtexture_rsc::descriptor&)> Setup) noexcept
        {
            if (auto It = m_Textures.find(Key); It != m_Textures.end()) return It->second;

            xrsc::texture_ref Ref{ .m_Instance = xresource::instance_guid::GenerateGUIDCopy(std::format("{}_texture_{}", GuidHex(m_Owner.m_Instance.m_Value), Key).c_str()) };
            const auto Folder = DescriptorFolder("Texture", Ref.m_Instance.m_Value);
            std::error_code Ec;
            std::filesystem::create_directories(Folder, Ec);

            xtexture_rsc::descriptor Desc;
            Desc.m_UWrap = Desc.m_VWrap = xtexture_rsc::wrap_type::WRAP;
            Setup(Desc);
            if (!xresource_pipeline::virtual_rsc::EmitDescriptor(Folder, Desc, [](xtexture_rsc::descriptor&, const xtexture_rsc::descriptor&) {}))        // a texture has its guid from what it is made of: the same guid has the same input, so what it holds now is the user's
                m_Warning(std::format("Import Materials: the descriptor of the texture {} could not be read: it is left as it is", Name));
            xresource_pipeline::virtual_rsc::EmitInfo(Folder, Ref, Name, Parent);

            m_Dependencies.m_VirtualResources.push_back(Ref);
            return m_Textures[Key] = Ref;
        }

        static xtexture_rsc::mix Constant(float Value, xtexture_rsc::compositing_to To) noexcept
        {
            xtexture_rsc::mix Mix;
            Mix.m_CopyFrom = xtexture_rsc::compositing_from::CUSTOM_CONSTANT;
            Mix.m_CopyTo   = To;
            Mix.m_Constant = std::clamp(Value, 0.0f, 1.0f);
            return Mix;
        }

        static xtexture_rsc::mix Channel(const std::wstring& File, xtexture_rsc::compositing_from From, xtexture_rsc::compositing_to To, bool bInvert = false) noexcept
        {
            xtexture_rsc::mix Mix;
            Mix.m_CopyFrom = From;
            Mix.m_CopyTo   = To;
            Mix.m_FileName = File;
            Mix.m_bInvert  = bInvert;
            return Mix;
        }

        static xtexture_rsc::mix_source Mixed(std::vector<xtexture_rsc::mix> Inputs) noexcept
        {
            xtexture_rsc::mix_source Source;
            Source.m_Inputs = std::move(Inputs);
            return Source;
        }

        static xtexture_rsc::single_input Single(const std::wstring& File) noexcept
        {
            xtexture_rsc::single_input Input;
            Input.m_FileName = File;
            return Input;
        }

        static std::string Stem(const std::wstring& Path) { return xstrtool::UTF16ToUTF8(std::filesystem::path(Path).stem().wstring()); }

        static bool LooksLikeNormalMap(const std::string& Path) noexcept
        {
            const auto Lower = xstrtool::ToLowerCopy(std::filesystem::path(Path).stem().string());
            for (auto* p : { "ddn", "normal", "nrm", "norm" }) if (Lower.find(p) != std::string::npos) return true;
            return Lower.ends_with("_n");
        }

        // The material's template of slots (the material compiler leaves it in its log)
        bool LoadTemplate(xmaterial_instance::descriptor& D, xrsc::material_ref Material) noexcept
        {
            const auto Path = std::format(L"{}\\Cache\\Resources\\Logs\\Material\\{:02X}\\{:02X}\\{:X}.log\\MaterialInstance.txt", m_ProjectPath
                , Material.m_Instance.m_Value & 0xff, (Material.m_Instance.m_Value >> 8) & 0xff, Material.m_Instance.m_Value);
            xtextfile::stream File;
            if (auto Err = File.Open(true, Path, xtextfile::file_type::TEXT); Err) return false;
            xproperty::settings::context Context;
            if (auto Err = xproperty::sprop::serializer::Stream(File, D, Context); Err) return false;
            D.m_MaterialRef = Material;
            D.setupDefaults();
            return true;
        }

        void EmitMaterial(const xraw3d::geom::material_instance& M) noexcept
        {
            using to   = xtexture_rsc::compositing_to;
            using from = xtexture_rsc::compositing_from;

            const xresource::full_guid InstanceGuid = InstanceRef(m_Owner.m_Instance, M.m_Name);

            //
            // What the model gives
            //
            const auto RawDiffuse = Param(M, "DiffuseMap");
            const auto Diffuse   = Resolve(RawDiffuse);
            const auto RawOpacity = Param(M, "OpacityMap");
            const auto Opacity   = RawOpacity.empty() || RawOpacity == Param(M, "DiffuseMap") ? std::wstring{} : Resolve(RawOpacity);
            const auto RawHeight = Param(M, "HeightMap");
            const auto Normal    = !Param(M, "NormalMap").empty() ? Resolve(Param(M, "NormalMap")) : LooksLikeNormalMap(RawHeight) ? Resolve(RawHeight) : std::wstring{};
            auto       Rough     = Resolve(Param(M, "RoughnessMap"));
            auto       Gloss     = Rough.empty() ? Resolve(Param(M, "ShininessMap")) : std::wstring{};     // FBX's glossiness: the roughness upside down (R = 1 - Glossiness)
            auto       Metal     = Resolve(Param(M, "MetalnessMap"));
            auto       AO        = Resolve(Param(M, "AmbientOcclusionMap"));

            // What the model does not name is looked for next to its color (or normal) map by name: Ch03_1001_Diffuse.png, Ch03_1001_Glossiness.png
            {
                const auto& Reference = !Diffuse.empty() ? Diffuse : Normal;
                const auto  Guess = [&](std::wstring& Map, const char* pWhat, std::initializer_list<const wchar_t*> Tokens)
                {
                    if (!Map.empty()) return;
                    Map = Sibling(Reference, Tokens);
                    if (!Map.empty() && m_Info) m_Info(std::format("Import Materials: material {}: the {} map is not named by the model, {} next to its color map is taken", M.m_Name, pWhat, UTF8(Map)));
                };
                Guess(AO, "occlusion", { L"ambientocclusion", L"occlusion", L"ao" });
                if (Rough.empty() && Gloss.empty())
                {
                    Guess(Rough, "roughness", { L"roughness", L"rough" });
                    if (Rough.empty()) Guess(Gloss, "glossiness", { L"glossiness", L"gloss", L"smoothness" });
                }
                Guess(Metal, "metalness", { L"metallic", L"metalness", L"metal" });
            }
            const auto Color     = Floats(Param(M, "DiffuseColor"),  { 1, 1, 1, 1 });
            const auto Emissive  = Floats(Param(M, "EmissiveColor"), { 0, 0, 0, 1 });
            const float Shininess = std::strtof(Param(M, "ShininessFactor").c_str(), nullptr);

            // Emission is ORME.a times the albedo: an emitter's albedo is its emissive color (brightest channel 1)
            const float EmissiveMax = std::max({ Emissive[0], Emissive[1], Emissive[2] });

            //
            // Albedo
            //
            bool bAlphaTest = false;
            xrsc::texture_ref Albedo;
            if (!Diffuse.empty())
            {
                const auto DI = Info(Diffuse);
                const auto OI = Opacity.empty() ? image_info{} : Info(Opacity);
                if (!Opacity.empty() && (OI.m_Width != DI.m_Width || OI.m_Height != DI.m_Height))
                {
                    m_Warning(std::format("Import Materials: material {}: the opacity map {} is not the size of the color map, it is not used", M.m_Name, xstrtool::UTF16ToUTF8(Opacity)));
                }
                else if (!Opacity.empty())
                {
                    bAlphaTest = true;
                    Albedo = Texture(xstrtool::UTF16ToUTF8(Diffuse + L"|" + Opacity), Stem(Diffuse) + " (color, cut out)", InstanceGuid, [&](xtexture_rsc::descriptor& D)
                    {
                        D.m_UsageType            = xtexture_rsc::usage_type::COLOR_AND_ALPHA;
                        D.m_Compression          = m_Settings.AlbedoAlpha();
                        D.m_bFillAveColorByAlpha = true;
                        D.m_InputVariant         = Mixed({ Channel(Diffuse, from::RGB, to::RGB), Channel(Opacity, from::R, to::A) });
                    });
                }

                if (!bAlphaTest && HasCutOut(Diffuse))
                {
                    bAlphaTest = true;
                    Albedo = Texture(xstrtool::UTF16ToUTF8(Diffuse + L"|alpha"), Stem(Diffuse) + " (color and alpha)", InstanceGuid, [&](xtexture_rsc::descriptor& D)
                    {
                        D.m_UsageType            = xtexture_rsc::usage_type::COLOR_AND_ALPHA;
                        D.m_Compression          = m_Settings.AlbedoAlpha();
                        D.m_bFillAveColorByAlpha = true;
                        D.m_InputVariant         = Single(Diffuse);
                    });
                }
                else if (!bAlphaTest)
                {
                    Albedo = Texture(xstrtool::UTF16ToUTF8(Diffuse), Stem(Diffuse) + " (color)", InstanceGuid, [&](xtexture_rsc::descriptor& D)
                    {
                        D.m_UsageType    = xtexture_rsc::usage_type::COLOR;
                        D.m_Compression  = m_Settings.Albedo();
                        D.m_InputVariant = Single(Diffuse);
                    });
                }
            }
            else if (!RawDiffuse.empty())
            {
                // The model names a color map that is not there: magenta, so it shows (the summary of the compile lists it)
                Albedo = Texture("missing_texture", "Missing texture", InstanceGuid, [&](xtexture_rsc::descriptor& D)
                {
                    D.m_UsageType     = xtexture_rsc::usage_type::COLOR;
                    D.m_Compression   = xtexture_rsc::compression_format::RGBA_UNCOMPRESSED;
                    D.m_bSRGB         = false;
                    D.m_bGenerateMips = false;
                    D.m_InputVariant  = Mixed({ Constant(1, to::R), Constant(0, to::G), Constant(1, to::B), Constant(1, to::A) });
                });
            }
            else
            {
                const auto C = EmissiveMax > 0 ? std::array{ Emissive[0] / EmissiveMax, Emissive[1] / EmissiveMax, Emissive[2] / EmissiveMax } : std::array{ Color[0], Color[1], Color[2] };
                Albedo = Texture(std::format("color_{:.3f}_{:.3f}_{:.3f}", C[0], C[1], C[2]), M.m_Name + " (color)", InstanceGuid, [&](xtexture_rsc::descriptor& D)
                {
                    D.m_UsageType    = xtexture_rsc::usage_type::COLOR;
                    D.m_Compression  = xtexture_rsc::compression_format::RGBA_UNCOMPRESSED;
                    D.m_bSRGB        = false;               // the model's colors are linear
                    D.m_bGenerateMips = false;
                    D.m_InputVariant = Mixed({ Constant(C[0], to::R), Constant(C[1], to::G), Constant(C[2], to::B), Constant(1, to::A) });
                });
            }

            //
            // Normal
            //
            const auto NormalRef = !Normal.empty()
                ? Texture(xstrtool::UTF16ToUTF8(Normal), Stem(Normal) + " (normal)", InstanceGuid, [&](xtexture_rsc::descriptor& D)
                {
                    D.m_UsageType    = xtexture_rsc::usage_type::TANGENT_NORMAL;
                    D.m_Compression  = m_Settings.Normal();
                    D.m_bSRGB        = false;
                    D.m_InputVariant = Single(Normal);
                })
                : Texture("flat_normal", "Flat normal", InstanceGuid, [&](xtexture_rsc::descriptor& D)
                {
                    D.m_UsageType     = xtexture_rsc::usage_type::TANGENT_NORMAL;
                    D.m_Compression   = xtexture_rsc::compression_format::RGBA_UNCOMPRESSED;
                    D.m_bSRGB         = false;
                    D.m_bGenerateMips = false;
                    D.m_InputVariant  = Mixed({ Constant(0.5f, to::R), Constant(0.5f, to::G), Constant(1, to::B), Constant(1, to::A) });
                });

            //
            // ORME: maps where the model has them (all one size, the texture compiler mixes them), constants for the rest
            //
            const float Roughness = Shininess > 0 ? std::sqrt(2.0f / (Shininess + 2.0f)) : 0.8f;   // Blinn-Phong exponent to roughness
            const float Emission  = EmissiveMax > 0 ? 1.0f : 0.0f;

            std::wstring Size;  // the first map's size every other map must have
            image_info   SizeInfo;
            auto Usable = [&](const std::wstring& Path, const char* pWhat) -> bool
            {
                if (Path.empty()) return false;
                const auto I = Info(Path);
                if (Size.empty()) { Size = Path; SizeInfo = I; return true; }
                if (I.m_Width == SizeInfo.m_Width && I.m_Height == SizeInfo.m_Height) return true;
                m_Warning(std::format("Import Materials: material {}: the {} map {} is not the size of {}, it is not used", M.m_Name, pWhat, xstrtool::UTF16ToUTF8(Path), xstrtool::UTF16ToUTF8(Size)));
                return false;
            };

            std::vector<xtexture_rsc::mix> Mixes;
            std::string Key = "orme";
            const bool bSharedRM = !Rough.empty() && Rough == Metal;       // glTF metallic-roughness: G and B
            if (Usable(AO, "occlusion"))    { Mixes.push_back(Channel(AO,    from::R, to::R)); Key += "|ao:" + xstrtool::UTF16ToUTF8(AO); }
            else                              Mixes.push_back(Constant(1, to::R));
            if (Usable(Rough, "roughness")) { Mixes.push_back(Channel(Rough, bSharedRM ? from::G : from::R, to::G)); Key += "|r:" + xstrtool::UTF16ToUTF8(Rough); }
            else if (Usable(Gloss, "glossiness")) { Mixes.push_back(Channel(Gloss, from::R, to::G, true)); Key += "|g:" + xstrtool::UTF16ToUTF8(Gloss); }
            else                            { Mixes.push_back(Constant(Roughness, to::G)); Key += std::format("|r{:.3f}", Roughness); }
            if (Usable(Metal, "metalness")) { Mixes.push_back(Channel(Metal, bSharedRM ? from::B : from::R, to::B)); Key += "|m:" + xstrtool::UTF16ToUTF8(Metal); }
            else                              Mixes.push_back(Constant(0, to::B));
            Mixes.push_back(Constant(Emission, to::A));
            Key += std::format("|e{:.0f}", Emission);

            const bool bConstantORME = Size.empty();
            const auto ORME = Texture(Key, (bConstantORME ? M.m_Name : Stem(Size)) + " (ORME)", InstanceGuid, [&](xtexture_rsc::descriptor& D)
            {
                D.m_UsageType     = xtexture_rsc::usage_type::INTENSITY;         // data (occlusion, roughness, metalness, emission), not a color: no alpha coverage, no average-color fill
                D.m_Compression   = bConstantORME ? xtexture_rsc::compression_format::RGBA_UNCOMPRESSED : m_Settings.ORME();
                D.m_bSRGB         = false;
                D.m_bGenerateMips = !bConstantORME;
                D.m_InputVariant  = Mixed(std::move(Mixes));
            });

            //
            // The instance
            //
            xmaterial_instance::descriptor Instance;
            const auto Material = bAlphaTest ? m_Settings.m_AlphaTest : m_Settings.m_Material;
            if (!LoadTemplate(Instance, Material))
            {
                m_Warning(std::format("Import Materials: material {}: the material {:016X} has no template of its slots yet (compile it first): its instance is not made", M.m_Name, Material.m_Instance.m_Value));
                return;
            }

            for (std::size_t i = 0; i < Instance.m_lTextureDefaults.size(); ++i)
            {
                const auto& Slot = Instance.m_lTextureDefaults[i].m_Name;
                if      (Slot == "SamplerAlbedo") Instance.m_lTextures[i] = Albedo;
                else if (Slot == "SamplerNormal") Instance.m_lTextures[i] = NormalRef;
                else if (Slot == "SamplerORME")   Instance.m_lTextures[i] = ORME;
            }

            const auto Folder = DescriptorFolder("MaterialInstance", InstanceGuid.m_Instance.m_Value);
            std::error_code Ec;
            std::filesystem::create_directories(Folder, Ec);
            if (!xresource_pipeline::virtual_rsc::EmitDescriptor(Folder, Instance, [&](xmaterial_instance::descriptor& Existing, const xmaterial_instance::descriptor& Fresh)
            {
                // An instance that exists is the user's, as a texture is: its base material, slots and params stay as they are. The one thing given to
                // it is a texture that was missing: a slot that still holds the magenta placeholder takes the texture the model names, now that it is found.
                // Likewise a slot that holds only constants (the flat normal, the constant ORME, the color of a model without a color map) takes the map
                // that the model, or the files next to its color map, give now: the constants were what the import made for want of a map.
                const xrsc::texture_ref Placeholder{ .m_Instance = xresource::instance_guid::GenerateGUIDCopy(std::format("{}_texture_missing_texture", GuidHex(m_Owner.m_Instance.m_Value)).c_str()) };
                for (std::size_t j = 0; j < Existing.m_lTextureDefaults.size() && j < Existing.m_lTextures.size(); ++j)
                {
                    const bool bPlaceholder = Existing.m_lTextures[j] == Placeholder || IsGoneTexture(Existing.m_lTextures[j]);      // a texture that is gone is a missing one
                    if (!bPlaceholder && !IsConstantTexture(Existing.m_lTextures[j])) continue;
                    for (std::size_t i = 0; i < Fresh.m_lTextureDefaults.size() && i < Fresh.m_lTextures.size(); ++i)
                    {
                        if (Fresh.m_lTextureDefaults[i].m_Name != Existing.m_lTextureDefaults[j].m_Name || Fresh.m_lTextures[i] == Placeholder || Fresh.m_lTextures[i] == Existing.m_lTextures[j]) continue;
                        if (!bPlaceholder && IsConstantTexture(Fresh.m_lTextures[i])) continue;       // constants for constants: the user's stay
                        Existing.m_lTextures[j] = Fresh.m_lTextures[i];
                        if (m_Info) m_Info(std::format("Import Materials: material {}: its {} {} is found, the slot takes it", M.m_Name, bPlaceholder ? "missing" : "constant", Fresh.m_lTextureDefaults[i].m_Name));
                    }
                }
            }))
                m_Warning(std::format("Import Materials: the descriptor of the instance {} could not be read: it is left as it is", M.m_Name));
            xresource_pipeline::virtual_rsc::EmitInfo(Folder, InstanceGuid, M.m_Name, m_Owner);
            m_Dependencies.m_VirtualResources.push_back(InstanceGuid);
        }
    };
}

#endif
