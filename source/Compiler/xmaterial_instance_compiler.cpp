#include "dependencies/xstrtool/source/xstrtool.h"
#include "source/xmaterial_instance_data_file.h"
#include "source/xmaterial_intance_descriptor.h"
#include "source/Compiler/xmaterial_instance_compiler.h"

namespace xmaterial_instance_compiler
{
    struct implementation final : xmaterial_instance_compiler::instance
    {
        xerr onCompile(void) override
        {
            //
            // Load the descriptor 
            //
            auto DescriptorFileName = std::format( L"{}/{}/Descriptor.txt", m_ProjectPaths.m_Project, m_InputSrcDescriptorPath);

            xmaterial_instance::descriptor Descriptor;
            xtextfile::stream              Stream;
            if ( auto Err = Stream.Open(true, DescriptorFileName, xtextfile::file_type::TEXT ); Err )
                return xerr::create_f<state, "Fail to open the descriptor">(Err);

            xproperty::settings::context Context{};
            if ( auto Err = xproperty::sprop::serializer::Stream( Stream, Descriptor, Context); Err )
                return xerr::create_f<state, "Fail to load the descriptor">(Err);

            //
            // Assign all the textures
            //
            displayProgressBar("Compiling Assigning Textures", 0.0f);

            auto Textures = std::make_unique<xmaterial_instance::data_file::texture[]>(Descriptor.m_lTextures.size());

            for ( auto& E : Descriptor.m_lTextures)
            {
                const auto Index = static_cast<int>(&E - Descriptor.m_lTextures.data());
                if( E.m_TextureRef.empty() && E.m_DefaultTextureRef.empty() )
                {
                    xerr::LogMessage<state::FAILURE>( std::format("Forgot to assign a texture {} With Index {} Entry Number", E.m_Name, E.m_Index, Index));
                    return xerr::create_f< state, "You forgot to assign a texture and there is not a default texture in the material to replace it">();
                }

                //
                // Assign all the required data
                //
                auto& T = Textures[Index];

                if (not E.m_TextureRef.empty() ) T.m_TexureRef = E.m_TextureRef;
                else                             T.m_TexureRef = E.m_DefaultTextureRef;

                T.m_Index = E.m_Index;
            }

            displayProgressBar("Compiling Assigning Textures", 1.0f);

            //
            // Construct the data_file
            //
            displayProgressBar("Compiling Constructing data file", 0.0f);
            xmaterial_instance::data_file DataFile;
            DataFile.m_MaterialRef          = Descriptor.m_MaterialRef;
            DataFile.m_pDefaultTextureList  = Textures.get();
            DataFile.m_nDefaultTexturesList = static_cast<std::uint8_t>(Descriptor.m_lTextures.size());
            displayProgressBar("Compiling Constructing data file", 1.0f);

            //
            // Add the dependencies
            //
            for (auto& E : Descriptor.m_lTextures)
            {
                if (not E.m_TextureRef.empty()) m_Dependencies.m_Resources.push_back(E.m_TextureRef);
                else                            m_Dependencies.m_Resources.push_back(E.m_DefaultTextureRef);
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
