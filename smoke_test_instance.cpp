// Material instances v2 (documentation/material_contract.md sections 5, 6, 9): the descriptor (an old file read as overrides, a save/load round trip) and the fixtures the
// compiler test compiles. Standalone: built and run by source/Editors/LevelEditor/smoke/test_material_instance_params.py. Exit 0 when every check holds.
//   smoke_test_instance fixture <dir> [new-param]   a project <dir>/t.lionprj: the material's layout and template in its compile log, the instances in its Descriptors
//   smoke_test_instance descriptor <dir>            the checks on the descriptor, in <dir>
#include "dependencies/xproperty/source/xcore/my_properties.cpp"
#include "dependencies/xtextfile/source/xtextfile.h"
#include "dependencies/xproperty/source/sprop/property_sprop_xtextfile_serializer.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_intance_descriptor.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    namespace fs = std::filesystem;
    using namespace xmaterial_instance;
    using xmaterial::param_type;

    int g_Failed = 0;

    void Check(bool bOk, const char* pWhat)
    {
        if (bOk) return;
        ++g_Failed;
        std::printf("FAIL: %s\n", pWhat);
    }

    // The guids of the fixture
    constexpr std::uint64_t MAT = 0xC001, MAT2 = 0xC003, TINT2 = 0x11, TINT = 0x10, ROUGH = 0x20, METAL = 0x30, ALBEDO = 0x6E;
    constexpr std::uint64_t RED = 0x1001, REDROUGH = 0x1003, OLD = 0x1005, ORPHAN = 0x1007, CYCA = 0x1009, CYCB = 0x100B, TEX = 0x100D, TEXPARENT = 0x100F, DSOFF = 0x1011;
    constexpr std::uint64_t T_HARD = 0x7001, T_DEFAULT = 0x7003, T_USER = 0x7005, T_PARENT = 0x7007;

    xrsc::texture_ref Tex(std::uint64_t V) { xrsc::texture_ref T; T.m_Instance.m_Value = V; return T; }

    std::wstring DescriptorFile(const fs::path& Project, std::uint64_t K)
    {
        return (Project / std::format(L"Descriptors/MaterialInstance/{:02X}/{:02X}/{:X}.desc/Descriptor.txt", K & 0xff, (K >> 8) & 0xff, K)).wstring();
    }

    template<typename T>
    bool Write(const std::wstring& Path, T& Object)
    {
        std::error_code Ec;
        fs::create_directories(fs::path(Path).parent_path(), Ec);
        xtextfile::stream File;
        xproperty::settings::context C;
        return !File.Open(false, Path, xtextfile::file_type::TEXT) && !xproperty::sprop::serializer::Stream(File, Object, C);
    }

    // DoubleSided: -1 not set (inherited), 0 or 1 set by this instance (an override of the system parameter, like any other)
    void WriteInstance(const fs::path& Project, std::uint64_t Key, std::uint64_t Parent, std::vector<override_entry> Overrides, int DoubleSided = -1)
    {
        descriptor D;
        if (DoubleSided >= 0) { override_entry O; O.m_ID = xmaterial::double_sided_param_id_v; O.m_Type = param_type::FLOAT; O.m_Value = { float(DoubleSided) }; O.m_NameHint = "Double Sided"; Overrides.push_back(O); }
        D.m_MaterialRef.m_Instance.m_Value = MAT;
        D.m_ParentRef.m_Instance.m_Value   = Parent;
        D.m_Overrides                      = std::move(Overrides);
        std::error_code Ec;
        fs::create_directories(fs::path(DescriptorFile(Project, Key)).parent_path(), Ec);
        xproperty::settings::context C;
        Check(!D.Serialize(false, DescriptorFile(Project, Key), C), "writing an instance");
    }

    override_entry Float(std::uint64_t ID, float V, const char* pName) { override_entry O; O.m_ID = ID; O.m_Type = param_type::FLOAT; O.m_Value = { V }; O.m_NameHint = pName; return O; }
    override_entry Texture(std::uint64_t ID, std::uint64_t T) { override_entry O; O.m_ID = ID; O.m_Type = param_type::TEXTURE; O.m_Texture = Tex(T); O.m_NameHint = "SamplerAlbedo"; return O; }

    // An instance file as v1 wrote it: the template of the material it was made from, and the textures of its slots; the albedo slot (pin guid 0x6E) is one of its own
    const char* pOldFile =
        "[ DescriptorVersion ]\n{ Major:d  Minor:d }\n//-------  -------\n     1        0   \n\n"
        "// New Types\n< s32:d u32:g s16:C u16:H s8:c u8:h f32:f f64:F string:s wstring:S u64:G s64:D bool:c full_guid:GG enum:s >\n\n"
        "[ xProperties : 10 ]\n{ Name:s                                                        Value:?                                        }\n"
        "  \"material_instance_descriptor/MaterialRef\"                    ;full_guid #C001 #8B3C028882EA813D\n"
        "  \"material_instance_descriptor/Textures[]\"                     ;s64       1\n"
        "  \"material_instance_descriptor/Textures[G:0]\"                  ;full_guid #7005 #6701D481518BD833\n"
        "  \"material_instance_descriptor/TextureDefaults[]\"              ;s64       1\n"
        "  \"material_instance_descriptor/TextureDefaults[G:0]/Name\"      ;string    \"SamplerAlbedo\"\n"
        "  \"material_instance_descriptor/TextureDefaults[G:0]/Index\"     ;s32       1\n"
        "  \"material_instance_descriptor/TextureDefaults[G:0]/GUID\"      ;u64       #6E\n"
        "  \"material_instance_descriptor/FinalTextures[]\"                ;s64       2\n"
        "  \"material_instance_descriptor/FinalTextures[G:0]/TextureRef\"  ;full_guid #7001 #6701D481518BD833\n"
        "  \"material_instance_descriptor/FinalTextures[G:1]/TextureRef\"  ;full_guid #7003 #6701D481518BD833\n";

    int Fixture(const fs::path& Dir, bool bNewParam)
    {
        const fs::path Project = Dir / "t.lionprj";
        const auto Logs = (Project / std::format(L"Cache/Resources/Logs/Material/{:02X}/{:02X}/{:X}.log", MAT & 0xff, (MAT >> 8) & 0xff, MAT)).wstring();

        // The material: Tint (a color without alpha, entity field TINT) and Roughness (valid 0..1) in its manifest, an exposed texture the old way (pin guid 0x6E) in its template
        xmaterial::param_decl Tint;  Tint.m_ID = TINT;  Tint.m_Name = "Tint";      Tint.m_Type = param_type::COLOR; Tint.m_bAlpha = false; Tint.m_EntityField = xmaterial::entity_field::TINT; Tint.m_Default = { 1, 1, 1, 1 };
        xmaterial::param_decl Rough; Rough.m_ID = ROUGH; Rough.m_Name = "Roughness"; Rough.m_Type = param_type::FLOAT; Rough.m_Default = { 0.5f }; Rough.m_ValidMin = { 0 }; Rough.m_ValidMax = { 1 };
        xmaterial::param_decl Metal; Metal.m_ID = METAL; Metal.m_Name = "Metal";     Metal.m_Type = param_type::FLOAT;
        // Double Sided: the system parameter every material compiler declares (STATIC: no storage, the layout hash is the same)
        xmaterial::param_decl DS;    DS.m_ID = xmaterial::double_sided_param_id_v; DS.m_Name = "Double Sided"; DS.m_Type = param_type::FLOAT; DS.m_Kind = xmaterial::param_kind::STATIC; DS.m_ValidMin = { 0 }; DS.m_ValidMax = { 1 };
        auto Manifest = bNewParam ? xmaterial::Pack({ Tint, Rough, Metal, DS }, { TINT, ROUGH, METAL }) : xmaterial::Pack({ Tint, Rough, DS }, { TINT, ROUGH });
        Check(Write(Logs + L"/MaterialLayout.txt", Manifest), "writing the layout");

        // A second material (SetMaterial in the editor test): its Tint was declared again under another id, it has no texture slot of its own
        {
            const auto Logs2 = (Project / std::format(L"Cache/Resources/Logs/Material/{:02X}/{:02X}/{:X}.log", MAT2 & 0xff, (MAT2 >> 8) & 0xff, MAT2)).wstring();
            auto Tint2 = Tint; Tint2.m_ID = TINT2;
            auto Manifest2 = xmaterial::Pack({ Tint2, Rough }, { TINT2, ROUGH });
            Check(Write(Logs2 + L"/MaterialLayout.txt", Manifest2), "writing the second layout");
            descriptor Template2;
            Template2.m_MaterialRef.m_Instance.m_Value = MAT2;
            Template2.m_lFinalTextures.resize(1);
            Template2.m_lFinalTextures[0].m_TextureRef = Tex(T_HARD);
            Check(Write(Logs2 + L"/MaterialInstance.txt", Template2), "writing the second template");
        }

        descriptor Template;
        Template.m_MaterialRef.m_Instance.m_Value = MAT;
        Template.m_lFinalTextures.resize(2);
        Template.m_lFinalTextures[0].m_TextureRef = Tex(T_HARD);
        Template.m_lFinalTextures[1].m_TextureRef = Tex(T_DEFAULT);
        Template.m_lTextureDefaults.push_back({ "SamplerAlbedo", 1, ALBEDO });
        Check(Write(Logs + L"/MaterialInstance.txt", Template), "writing the template");

        // The instances
        override_entry Red; Red.m_ID = TINT; Red.m_Type = param_type::COLOR; Red.m_Value = { 1, 0, 0, 1 }; Red.m_NameHint = "Tint";
        WriteInstance(Project, RED, 0, { Red }, 1);           // Double Sided on: its children inherit it, unless they set it themselves
        WriteInstance(Project, DSOFF, RED, {}, 0);
        WriteInstance(Project, REDROUGH, RED, { Float(ROUGH, 0.9f, "Roughness") });
        WriteInstance(Project, ORPHAN, 0, { Float(ROUGH, 7.f, "Roughness"), Float(0x999, 3.f, "Gone") });
        WriteInstance(Project, CYCA, CYCB, {});
        WriteInstance(Project, CYCB, CYCA, {});
        WriteInstance(Project, TEXPARENT, RED, { Texture(ALBEDO, T_PARENT) });
        WriteInstance(Project, TEX, TEXPARENT, {});
        {
            std::error_code Ec;
            fs::create_directories(fs::path(DescriptorFile(Project, OLD)).parent_path(), Ec);
            std::ofstream(fs::path(DescriptorFile(Project, OLD)), std::ios::binary) << pOldFile;
        }
        fs::create_directories(Project / "Cache/Resources/Platforms/WINDOWS");
        std::printf("fixture: %s hash %016llX\n", bNewParam ? "new param" : "first", static_cast<unsigned long long>(Manifest.m_LayoutHash));
        return g_Failed;
    }

    template<typename T>
    bool Read(const std::wstring& Path, T& D) { xproperty::settings::context C; return !D.Serialize(true, Path, C); }

    int Descriptor(const fs::path& Dir)
    {
        const fs::path Project = Dir / "t.lionprj";

        // Test 8: a v1 file with a changed texture loads as one TEXTURE override with the id of the slot's pin
        descriptor Old;
        Check(Read(DescriptorFile(Project, OLD), Old), "reading the v1 file");
        Check(Old.m_Overrides.size() == 1 && Old.m_Overrides[0].m_ID == ALBEDO && Old.m_Overrides[0].m_Type == param_type::TEXTURE && Old.m_Overrides[0].m_Texture == Tex(T_USER)
              && Old.m_Overrides[0].m_NameHint == "SamplerAlbedo", "the changed slot is an override keyed by the pin guid");
        Check(Old.m_lTextures.empty() && Old.m_lTextureDefaults.empty() && Old.m_lFinalTextures.empty() && Old.m_MaterialRef.m_Instance.m_Value == MAT && Old.m_ParentRef.empty(), "v1's lists are gone, the material stays");

        // ... and is written back as v2 (version row, overrides, the same on the next read)
        const auto Back = (Dir / "old_v2.txt").wstring();
        {
            xproperty::settings::context C;
            Check(!Old.Serialize(false, Back, C), "writing the v1 file as v2");
        }
        {
            std::ifstream In(fs::path(Back), std::ios::binary);
            std::string Text((std::istreambuf_iterator<char>(In)), {});
            Check(Text.find("material_instance_descriptor/Version") != std::string::npos && Text.find("/Overrides[G:") != std::string::npos && Text.find("Overrides[G:0]/") == std::string::npos
                 , "the file has Version and Overrides rows, by parameter id (not by place)");
        }
        descriptor Again;
        Check(Read(Back, Again) && Again.m_DescVersion == 2 && Again.m_Overrides.size() == 1 && Again.m_Overrides[0].m_ID == ALBEDO && Again.m_Overrides[0].m_Texture == Tex(T_USER), "v2 reads back the same");

        // A slot left at the template's texture is no override
        descriptor Same;
        Same.m_MaterialRef.m_Instance.m_Value = MAT;
        Same.m_lFinalTextures.resize(2); Same.m_lFinalTextures[1].m_TextureRef = Tex(T_DEFAULT);
        Same.m_lTextureDefaults.push_back({ "SamplerAlbedo", 1, ALBEDO });
        Same.setupDefaults();
        Same.FoldLegacy();
        Check(Same.m_Overrides.empty(), "a texture equal to the template's is not set");

        // Test 3 (the descriptor's half): an override of an unknown id survives a save and a load; unset is not set-to-default; the file is sorted by id
        descriptor Orphan;
        Check(Read(DescriptorFile(Project, ORPHAN), Orphan) && Orphan.m_Overrides.size() == 2, "reading the instance with an unknown id");
        Orphan.Set(ROUGH).m_Value = { 0.5f };                           // pinned to the default
        Orphan.Set(0x5) .m_Type   = param_type::FLOAT;                  // out of order on purpose (Set keeps it sorted, the file must be too)
        const auto Round = (Dir / "orphan_round.txt").wstring();
        {
            xproperty::settings::context C;
            Check(!Orphan.Serialize(false, Round, C), "writing the orphans");
        }
        descriptor Loaded;
        Check(Read(Round, Loaded) && Loaded.m_Overrides.size() == 3, "reading them back");
        Check(Loaded.Find(0x999) && Loaded.Find(0x999)->m_Value.m_X == 3.f && Loaded.Find(0x999)->m_NameHint == "Gone", "the unknown id survives, its value and hint with it");
        Check(Loaded.Find(ROUGH) && Loaded.Find(ROUGH)->m_Value.m_X == 0.5f, "a value equal to the default stays set (pinned)");
        Check(Loaded.m_Overrides[0].m_ID < Loaded.m_Overrides[1].m_ID && Loaded.m_Overrides[1].m_ID < Loaded.m_Overrides[2].m_ID, "sorted by id");
        Check(Loaded.Clear(ROUGH) && !Loaded.Find(ROUGH) && !Loaded.Clear(ROUGH), "Clear removes the entry, once");

        // Validation: a texture override with no texture is the old 'forgot to assign a texture'
        descriptor Empty;
        Empty.m_MaterialRef.m_Instance.m_Value = MAT;
        Empty.Set(ALBEDO).m_Type = param_type::TEXTURE;
        std::vector<std::string> Errors;
        Empty.Validate(Errors);
        Check(Errors.size() == 1, "an empty texture override is an error");

        if (g_Failed == 0) std::printf("all checks passed\n");
        return g_Failed;
    }
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::printf("usage: fixture|descriptor <dir> [new-param]\n"); return 2; }
    const std::string Mode = argv[1];
    const fs::path    Dir  = argv[2];
    const int Result = Mode == "fixture" ? Fixture(Dir, argc > 3) : Descriptor(Dir);
    return Result == 0 ? 0 : 1;
}
