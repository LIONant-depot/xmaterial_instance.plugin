#ifndef XMATERIAL_INSTANCE_IMPORT_COMPILER_H
#define XMATERIAL_INSTANCE_IMPORT_COMPILER_H
#pragma once

// The compiler half of xmaterial_instance_import.h: a geometry compiler hands the materials of its model to emit() and gets one virtual
// Material Instance per material (made from the PBR material of the settings) with virtual Textures in its slots:
//      SamplerAlbedo   the color map (its alpha, or the opacity map, cuts the surface out: then the instance uses the alpha test material),
//                      or the diffuse color
//      SamplerNormal   the normal map (an OBJ "bump" map named like a normal map counts), or a flat normal
//      SamplerORME     occlusion, roughness, metalness and emission packed from the PBR maps of the model, or constants: roughness from the
//                      shininess, emission from the emissive color
// Every resource is written in Cache/Descriptors only when its text changed, so recompiling the geometry does not recompile its textures.
// The includer defines STB_IMAGE_IMPLEMENTATION once (the alpha and size checks read the images).
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_import.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_intance_descriptor.h"
#include "plugins/xtexture.plugin/source/xtexture_rsc_descriptor.h"
#include "dependencies/xraw3d/source/xraw3d.h"
#include "dependencies/stb/stb_image.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

namespace xmaterial_instance::import
{
    struct emitter
    {
        std::wstring                        m_ProjectPath;          // the project's root
        std::wstring                        m_CachedDescriptors;    // where virtual resources live (xresource_pipeline project_paths::m_CachedDescriptors)
        xresource::full_guid                m_Owner;                // the geometry
        std::wstring                        m_ModelPath;            // the model, relative to the project (its textures are relative to it)
        const settings&                     m_Settings;
        xresource_pipeline::dependencies&   m_Dependencies;
        std::function<void(std::string)>    m_Warning;

        // One instance per material, in the order of the list
        void Emit(const std::vector<xraw3d::geom::material_instance>& Materials) noexcept
        {
            for (auto& M : Materials) EmitMaterial(M);
        }

        //------------------------------------------------------------------------------------
        // (public only so that the emitter stays an aggregate the compilers fill by name)

        struct image_info { int m_Width = 0, m_Height = 0, m_Channels = 0; };

        std::map<std::string, xrsc::texture_ref>    m_Textures;     // by what they are made of: a texture several materials share is made once

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

        // A texture path of the model, as the project sees it ("Assets\...", empty when it is not there). Exporters write paths of the artist's
        // machine: when the path does not exist the file is looked for by name under the model's folder.
        std::wstring Resolve(const std::string& Raw) noexcept
        {
            if (Raw.empty()) return {};
            if (Raw[0] == '*') { m_Warning(std::format("Import Materials: the texture {} is embedded in the model, it is not imported", Raw)); return {}; }

            namespace fs = std::filesystem;
            std::error_code Ec;
            const fs::path  Project  = fs::path(m_ProjectPath);
            const fs::path  ModelDir = (Project / m_ModelPath).parent_path();
            fs::path        Path     = fs::path(xstrtool::To(Raw)).make_preferred();

            fs::path Found;
            if      (Path.is_absolute() && fs::exists(Path, Ec)) Found = Path;
            else if (fs::exists(ModelDir / Path, Ec))           Found = ModelDir / Path;
            else
            {
                const auto Name = xstrtool::ToLowerCopy(Path.filename().wstring());
                for (auto It = fs::recursive_directory_iterator(ModelDir, Ec); !Ec && It != fs::recursive_directory_iterator(); It.increment(Ec))
                    if (It->is_regular_file(Ec) && xstrtool::ToLowerCopy(It->path().filename().wstring()) == Name) { Found = It->path(); break; }
            }

            if (Found.empty()) { m_Warning(std::format("Import Materials: texture {} not found (looked under {})", Raw, xstrtool::To(ModelDir.wstring()))); return {}; }

            const auto Relative = fs::relative(Found, Project, Ec).lexically_normal().make_preferred().wstring();
            if (Ec || Relative.starts_with(L"..")) { m_Warning(std::format("Import Materials: texture {} is outside the project", xstrtool::To(Found.wstring()))); return {}; }
            return Relative;
        }

        image_info Info(const std::wstring& Path) const noexcept
        {
            image_info I;
            if (!stbi_info(xstrtool::To(Full(Path)).c_str(), &I.m_Width, &I.m_Height, &I.m_Channels)) I = {};
            return I;
        }

        // Whether a color map with an alpha channel uses it (an exporter gives the color map as the opacity map whenever it has one)
        bool HasCutOut(const std::wstring& Path) const noexcept
        {
            if (Info(Path).m_Channels != 4) return false;
            int W, H, C;
            auto* pData = stbi_load(xstrtool::To(Full(Path)).c_str(), &W, &H, &C, 4);
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
            return std::format(L"{}\\{}\\{:02X}\\{:02X}\\{:X}.desc", m_CachedDescriptors, xstrtool::To(Type), Value & 0xFF, (Value >> 8) & 0xFF, Value);
        }

        // Writes a file through a temporary one (outside the project: its watcher sees every file) and keeps the old one when nothing changed:
        // its time is what decides a recompile
        template<typename T_WRITE>
        static void WriteIfChanged(const std::wstring& Path, T_WRITE&& Write) noexcept
        {
            namespace fs = std::filesystem;
            std::error_code Ec;
            const auto Temp = (fs::temp_directory_path(Ec) / std::format(L"xmaterial_import_{:016X}.txt", std::hash<std::wstring>{}(Path))).wstring();
            Write(Temp);

            auto Read = [](const std::wstring& P) { std::ifstream F(fs::path(P), std::ios::binary); return std::string(std::istreambuf_iterator<char>(F), {}); };
            if (!fs::exists(Path, Ec) || Read(Path) != Read(Temp)) fs::copy_file(Temp, Path, fs::copy_options::overwrite_existing, Ec);
            fs::remove(Temp, Ec);
        }

        void WriteInfo(const std::wstring& Folder, xresource::full_guid Guid, const std::string& Name, xresource::full_guid Parent) noexcept
        {
            xresource_pipeline::info Info{ Guid };
            Info.m_Name = Name;
            Info.m_RscLinks.push_back(Parent);          // shows as the parent's child in the asset browser
            WriteIfChanged(Folder + L"\\Info.txt", [&](const std::wstring& P) { xproperty::settings::context C; Info.Serialize(false, P, C); });
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
            WriteIfChanged(Folder + L"\\Descriptor.txt", [&](const std::wstring& P) { xproperty::settings::context C; Desc.Serialize(false, P, C); });
            WriteInfo(Folder, Ref, Name, Parent);

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

        static xtexture_rsc::mix Channel(const std::wstring& File, xtexture_rsc::compositing_from From, xtexture_rsc::compositing_to To) noexcept
        {
            xtexture_rsc::mix Mix;
            Mix.m_CopyFrom = From;
            Mix.m_CopyTo   = To;
            Mix.m_FileName = File;
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

        static std::string Stem(const std::wstring& Path) { return xstrtool::To(std::filesystem::path(Path).stem().wstring()); }

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
            const auto Diffuse   = Resolve(Param(M, "DiffuseMap"));
            const auto RawOpacity = Param(M, "OpacityMap");
            const auto Opacity   = RawOpacity.empty() || RawOpacity == Param(M, "DiffuseMap") ? std::wstring{} : Resolve(RawOpacity);
            const auto RawHeight = Param(M, "HeightMap");
            const auto Normal    = !Param(M, "NormalMap").empty() ? Resolve(Param(M, "NormalMap")) : LooksLikeNormalMap(RawHeight) ? Resolve(RawHeight) : std::wstring{};
            const auto Rough     = Resolve(Param(M, "RoughnessMap"));
            const auto Metal     = Resolve(Param(M, "MetalnessMap"));
            const auto AO        = Resolve(Param(M, "AmbientOcclusionMap"));
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
                    m_Warning(std::format("Import Materials: material {}: the opacity map {} is not the size of the color map, it is not used", M.m_Name, xstrtool::To(Opacity)));
                }
                else if (!Opacity.empty())
                {
                    bAlphaTest = true;
                    Albedo = Texture(xstrtool::To(Diffuse + L"|" + Opacity), Stem(Diffuse) + " (color, cut out)", InstanceGuid, [&](xtexture_rsc::descriptor& D)
                    {
                        D.m_UsageType            = xtexture_rsc::usage_type::COLOR_AND_ALPHA;
                        D.m_Compression          = xtexture_rsc::compression_format::RGBA_BC7;
                        D.m_bFillAveColorByAlpha = true;
                        D.m_InputVariant         = Mixed({ Channel(Diffuse, from::RGB, to::RGB), Channel(Opacity, from::R, to::A) });
                    });
                }

                if (!bAlphaTest && HasCutOut(Diffuse))
                {
                    bAlphaTest = true;
                    Albedo = Texture(xstrtool::To(Diffuse + L"|alpha"), Stem(Diffuse) + " (color and alpha)", InstanceGuid, [&](xtexture_rsc::descriptor& D)
                    {
                        D.m_UsageType            = xtexture_rsc::usage_type::COLOR_AND_ALPHA;
                        D.m_Compression          = xtexture_rsc::compression_format::RGBA_BC7;
                        D.m_bFillAveColorByAlpha = true;
                        D.m_InputVariant         = Single(Diffuse);
                    });
                }
                else if (!bAlphaTest)
                {
                    Albedo = Texture(xstrtool::To(Diffuse), Stem(Diffuse) + " (color)", InstanceGuid, [&](xtexture_rsc::descriptor& D)
                    {
                        D.m_UsageType    = xtexture_rsc::usage_type::COLOR;
                        D.m_Compression  = xtexture_rsc::compression_format::RGB_BC1;
                        D.m_InputVariant = Single(Diffuse);
                    });
                }
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
                ? Texture(xstrtool::To(Normal), Stem(Normal) + " (normal)", InstanceGuid, [&](xtexture_rsc::descriptor& D)
                {
                    D.m_UsageType    = xtexture_rsc::usage_type::TANGENT_NORMAL;
                    D.m_Compression  = xtexture_rsc::compression_format::RG_BC5;
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
                m_Warning(std::format("Import Materials: material {}: the {} map {} is not the size of {}, it is not used", M.m_Name, pWhat, xstrtool::To(Path), xstrtool::To(Size)));
                return false;
            };

            std::vector<xtexture_rsc::mix> Mixes;
            std::string Key = "orme";
            const bool bSharedRM = !Rough.empty() && Rough == Metal;       // glTF metallic-roughness: G and B
            if (Usable(AO, "occlusion"))    { Mixes.push_back(Channel(AO,    from::R, to::R)); Key += "|ao:" + xstrtool::To(AO); }
            else                              Mixes.push_back(Constant(1, to::R));
            if (Usable(Rough, "roughness")) { Mixes.push_back(Channel(Rough, bSharedRM ? from::G : from::R, to::G)); Key += "|r:" + xstrtool::To(Rough); }
            else                            { Mixes.push_back(Constant(Roughness, to::G)); Key += std::format("|r{:.3f}", Roughness); }
            if (Usable(Metal, "metalness")) { Mixes.push_back(Channel(Metal, bSharedRM ? from::B : from::R, to::B)); Key += "|m:" + xstrtool::To(Metal); }
            else                              Mixes.push_back(Constant(0, to::B));
            Mixes.push_back(Constant(Emission, to::A));
            Key += std::format("|e{:.0f}", Emission);

            const bool bConstantORME = Size.empty();
            const auto ORME = Texture(Key, (bConstantORME ? M.m_Name : Stem(Size)) + " (ORME)", InstanceGuid, [&](xtexture_rsc::descriptor& D)
            {
                D.m_UsageType     = xtexture_rsc::usage_type::COLOR_AND_ALPHA;
                D.m_Compression   = bConstantORME ? xtexture_rsc::compression_format::RGBA_UNCOMPRESSED : xtexture_rsc::compression_format::RGBA_BC7;
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
            WriteIfChanged(Folder + L"\\Descriptor.txt", [&](const std::wstring& P) { xproperty::settings::context C; Instance.Serialize(false, P, C); });
            WriteInfo(Folder, InstanceGuid, M.m_Name, m_Owner);
            m_Dependencies.m_VirtualResources.push_back(InstanceGuid);
        }
    };
}

#endif
