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
            //  make sure that we have something valid
            //
            if ( Descriptor.m_MaterialRef.empty() )
                return xerr::create_f<state, "Material Instance without a material reference">();

            //
            // Set all the final textures
            //
            if (Descriptor.m_lTextureDefaults.size() != Descriptor.m_lTextures.size())
                return xerr::create_f<state, "Default and Set Textures size do not match">();

            for (auto& E : Descriptor.m_lTextureDefaults)
            {
                const auto Index = static_cast<int>(&E - Descriptor.m_lTextureDefaults.data());
                if (Descriptor.m_lTextures[Index].empty())
                {
                    xerr::LogMessage<state::FAILURE>(std::format("Forgot to assign a texture {} With Index {} Entry Number {}", E.m_Name, E.m_Index, Index));
                    return xerr::create_f< state, "You forgot to assign a texture">();
                }

                Descriptor.m_lFinalTextures[E.m_Index].m_TextureRef = Descriptor.m_lTextures[Index];
            }

            //
            // Assign all the textures
            //
            displayProgressBar("Compiling Assigning Textures", 0.0f);

            auto Textures = std::make_unique<xmaterial_instance::data_file::texture[]>(Descriptor.m_lFinalTextures.size());

            for ( auto& E : Descriptor.m_lFinalTextures)
            {
                const auto Index = static_cast<int>(&E - Descriptor.m_lFinalTextures.data());

                //
                // Assign all the required data
                //
                auto& T = Textures[Index];
                T.m_TexureRef = E.m_TextureRef;
            }

            displayProgressBar("Compiling Assigning Textures", 1.0f);

            //
            // Construct the data_file
            //
            displayProgressBar("Compiling Constructing data file", 0.0f);
            xmaterial_instance::data_file DataFile;
            DataFile.m_MaterialRef   = Descriptor.m_MaterialRef;
            DataFile.m_pTextureList  = Textures.get();
            DataFile.m_nTexturesList = static_cast<std::uint8_t>(Descriptor.m_lFinalTextures.size());
            displayProgressBar("Compiling Constructing data file", 1.0f);

            //
            // Add the dependencies
            //
            m_Dependencies.m_Resources.push_back(Descriptor.m_MaterialRef);
            for (auto& E : Descriptor.m_lFinalTextures)
            {
                if (not E.m_TextureRef.empty()) m_Dependencies.m_Resources.push_back(E.m_TextureRef);
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
