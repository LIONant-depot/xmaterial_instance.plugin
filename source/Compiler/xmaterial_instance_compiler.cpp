#include "dependencies/xstrtool/source/xstrtool.h"
#include "source/xmaterial_instance_data_file.h"
#include "source/xmaterial_intance_descriptor.h"
#include "source/Compiler/xmaterial_instance_compiler.h"

#include "dependencies/xproperty/source/xcore/my_properties.cpp"

namespace xmaterial_instance_compiler
{
    struct implementation final : xmaterial_instance_compiler::instance
    {
        xerr onCompile(void) override
        {
            //
            // Load the descriptor
            //
            auto                            DescriptorFileName  = std::format( L"{}/{}/Descriptor.txt", m_ProjectPaths.m_Project, m_InputSrcDescriptorPath);
            auto                            BaseDescriptor      = xresource_pipeline::factory_base::Find(std::string_view{ "Material Instance" })->CreateDescriptor();
            xproperty::settings::context    Context             = {};
            if (auto Err = BaseDescriptor->Serialize(true, DescriptorFileName, Context); Err)
                return Err;

            //
            // Validate material instance
            //
            {
                std::vector<std::string> ErrorList;
                BaseDescriptor->Validate(ErrorList);
                if (not ErrorList.empty())
                {
                    // Print the error messages
                    for (auto& E : ErrorList)
                        LogMessage(xresource_pipeline::msg_type::ERROR, E);

                    return xerr::create_f<state, "Material Instance failed validation">();
                }
            }

            // Convert to something more friendly
            auto& Descriptor = *reinterpret_cast<xmaterial_instance::descriptor*>(BaseDescriptor.get());

            //
            // Resolve: this instance, its parents, the material (section 5 of the contract). Every ancestor's descriptor is a dependency, so editing one compiles its
            // descendants again.
            //
            xmaterial_instance::resolution Resolution;
            const auto Missing  = Resolution.Run(m_ProjectPaths.m_Project, m_ProjectPaths.m_Descriptors, m_ResourceGuid.m_Value, Descriptor);
            auto&      Resolved = Resolution.m_Resolved;
            const auto& Layout  = Resolution.m_Layout;
            const auto& Manifest = Layout.m_Manifest;

            static constexpr const char* Codes[] = { "W_ORPHAN_ID", "W_ORPHAN_TYPE", "W_CLAMPED", "E_CHAIN_CYCLE", "E_CHAIN_DEPTH", "E_PARENT_ROOT" };
            for (auto& D : Resolved.m_Diags)
                LogMessage( D.m_Code >= xmaterial_instance::diag_code::E_CHAIN_CYCLE ? xresource_pipeline::msg_type::ERROR : xresource_pipeline::msg_type::WARNING
                          , D.m_ID ? std::format("{}: parameter {:016X}", Codes[int(D.m_Code)], D.m_ID) : std::string(Codes[int(D.m_Code)]));

            if (not Missing.empty())
            {
                LogMessage(xresource_pipeline::msg_type::ERROR, Missing);
                return xerr::create_f<state, "The material instance could not be resolved (see the log)">();
            }
            if (Resolved.Failed())
                return xerr::create_f<state, "The material instance chain is not valid (see the log for the E_ codes)">();

            for (auto& A : Resolved.m_Chain)
                if (A != m_ResourceGuid.m_Value)
                {
                    xrsc::material_instance_ref Ref;
                    Ref.m_Instance.m_Value = A;
                    m_Dependencies.m_Resources.push_back(Ref);
                    m_Dependencies.m_Assets.push_back(Resolution.DescriptorPath(m_ProjectPaths.m_Descriptors, A).substr(m_ProjectPaths.m_Project.length() + 1));
                }

            //
            // The parameter block, in the material's layout
            //
            displayProgressBar("Compiling Packing Parameters", 0.0f);

            std::vector<float> Block(Manifest.m_BlockBytes / 4, 0.0f);
            for (const auto& E : Manifest.m_Entries)
            {
                if (E.m_Type == xmaterial::param_type::TEXTURE) continue;
                auto It = std::ranges::find(Resolved.m_Params, E.m_ID, &xmaterial_instance::resolved_param::m_ID);
                if (It == Resolved.m_Params.end()) continue;
                const float V[4] = { It->m_Value.m_X, It->m_Value.m_Y, It->m_Value.m_Z, It->m_Value.m_W };
                const int   n    = E.m_Type == xmaterial::param_type::FLOAT ? 1 : E.m_Type == xmaterial::param_type::FLOAT2 ? 2 : E.m_Type == xmaterial::param_type::FLOAT3 ? 3 : 4;
                for (int i = 0; i < n; ++i) Block[E.m_ByteOffset / 4 + i] = V[i];
            }

            //
            // The textures: the material's, with the ones an instance of the chain set
            //
            std::vector<xrsc::texture_ref> Final;
            for (auto& E : Layout.m_Template.m_lFinalTextures) Final.push_back(E.m_TextureRef);

            for (const auto& P : Resolved.m_Params)
            {
                if (P.m_Type != xmaterial::param_type::TEXTURE || P.m_Source < 0) continue;
                const int  Index = Layout.TextureIndex(P.m_ID);
                const auto* pSet = Resolution.m_Descriptors[Resolved.m_Chain[P.m_Source]]->Find(P.m_ID);
                if (Index >= 0 && pSet && not pSet->m_Texture.empty()) Final[Index] = pSet->m_Texture;
            }

            auto Textures = std::make_unique<xmaterial_instance::data_file::texture[]>(Final.size());
            for (auto& E : Final) Textures[&E - Final.data()].m_TexureRef = E;

            displayProgressBar("Compiling Packing Parameters", 1.0f);

            {
                std::string Text = std::format("Resolved against the layout {:016X}: block of {} bytes:", Manifest.m_LayoutHash, Manifest.m_BlockBytes);
                for (const float F : Block) Text += std::format(" {}", F);
                LogMessage(xresource_pipeline::msg_type::INFO, Text);
                Text = "Textures:";
                for (const auto& E : Final) Text += std::format(" {:016X}", E.m_Instance.m_Value);
                LogMessage(xresource_pipeline::msg_type::INFO, Text);
            }

            //
            // Construct the data_file
            //
            displayProgressBar("Compiling Constructing data file", 0.0f);
            xmaterial_instance::data_file DataFile;
            DataFile.m_MaterialRef   = Descriptor.m_MaterialRef;
            DataFile.m_pTextureList  = Textures.get();
            DataFile.m_nTexturesList = static_cast<std::uint8_t>(Final.size());
            DataFile.m_BlockBytes    = Manifest.m_BlockBytes;
            DataFile.m_pParams       = Block.empty() ? nullptr : Block.data();
            DataFile.m_LayoutHash    = Manifest.m_LayoutHash;
            displayProgressBar("Compiling Constructing data file", 1.0f);

            //
            // Add the dependencies
            //
            m_Dependencies.m_Resources.push_back(Descriptor.m_MaterialRef);
            for (auto& E : Final)
            {
                if (not E.empty()) m_Dependencies.m_Resources.push_back(E);
            }

            {
                std::wstring MaterialRefDescriptorPath;
                if (auto Err = Descriptor.getTemplatePathFromDescriptorPath(MaterialRefDescriptorPath, DescriptorFileName); Err)
                    return Err;

                auto Rel = MaterialRefDescriptorPath.substr(m_ProjectPaths.m_Project.length()+1);
                m_Dependencies.m_Assets.push_back(Rel);
                m_Dependencies.m_Assets.push_back(Rel.replace(Rel.rfind(L'/') + 1, std::wstring::npos, L"MaterialLayout.txt"));
            }

            //
            // Serialize Final xMaterial
            //
            int Count = 0;
            for (auto& T : m_Target)
            {
                displayProgressBar("Serializing", Count++ / static_cast<float>(m_Target.size()));

                if (T.m_bValid)
                {
                    xserializer::stream Stream;
                    if ( auto Err = Stream.Save(T.m_DataPath, DataFile, xserializer::compression_level::HIGH); Err )
                        return Err;
                }
            }
            displayProgressBar("Serializing", 1);

            return {};
        }
    };
}

//
// The creation function
//
std::unique_ptr<xmaterial_instance_compiler::instance> xmaterial_instance_compiler::instance::Create(void)
{
    return std::make_unique<implementation>();
}
