#ifndef XMATERIAL_INSTANCE_INFO_H
#define XMATERIAL_INSTANCE_INFO_H

#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "Plugins/xtexture.plugin/source/xtexture_xgpu_rsc_loader.h"
#include "Plugins/xmaterial.plugin/source/xmaterial_xgpu_rsc_loader.h"

namespace xmaterial_instance
{
    inline static constexpr auto resource_type_guid_v = xresource::type_guid(xresource::guid_generator::Instance64FromString("MaterialInstance"));

    struct default_texture_entry
    {
        std::string         m_Name                  = {};
        int                 m_Index                 = {};
        std::uint64_t       m_GUID                  = {};

        XPROPERTY_DEF
        ( "default_texture_entry", default_texture_entry
        , obj_member<"Name",                &default_texture_entry::m_Name>
        , obj_member<"Index",               &default_texture_entry::m_Index>
        , obj_member<"GUID",                &default_texture_entry::m_GUID>
        )
    };
    XPROPERTY_REG(default_texture_entry)

    struct final_texture_entry
    {
        xrsc::texture_ref   m_TextureRef        = {};

        XPROPERTY_DEF
        ( "final_texture_entry", final_texture_entry
        , obj_member<"TextureRef",          &final_texture_entry::m_TextureRef>
        )
    };
    XPROPERTY_REG(final_texture_entry)

    struct descriptor : xresource_pipeline::descriptor::base
    {
        XPROPERTY_VDEF
        ( "material_instance_descriptor",   descriptor
        , obj_member<"MaterialRef",         &descriptor::m_MaterialRef >
        , obj_member<"Textures",            &descriptor::m_lTextures, member_ui_open<true>>
        , obj_member<"TextureDefaults",     &descriptor::m_lTextureDefaults, member_flags<flags::DONT_SHOW>>
        , obj_member<"FinalTextures",       &descriptor::m_lFinalTextures, member_flags<flags::DONT_SHOW>>
        )

        void SetupFromSource(std::string_view FileName)
        {
            
        }

        void Validate(std::vector<std::string>& Errors) const noexcept override
        {
            if (m_MaterialRef.empty())
            {
                Errors.push_back(std::format("The material reference is NULL"));
                return;
            }
                
            if (m_lTextureDefaults.size() != m_lTextures.size())
            {
                Errors.push_back(std::format("Default and Set Textures count (Default == {}, Textures == {}", m_lTextureDefaults.size(), m_lTextures.size()));
                return;
            }
                
            for (auto& E : m_lTextureDefaults)
            {
                const auto Index = static_cast<int>(&E - m_lTextureDefaults.data());
                if (m_lTextures[Index].empty())
                    Errors.push_back(std::format("Forgot to assign a texture {} at Index {}", E.m_Name, Index));
            }
        }

        void setupDefaults()
        {
            m_lTextures.resize(m_lTextureDefaults.size());
            for (auto& E : m_lTextureDefaults)
            {
                const auto Index = static_cast<int>(&E - m_lTextureDefaults.data());
                m_lTextures[Index] = m_lFinalTextures[E.m_Index].m_TextureRef;
            }
        }

        xerr getTemplatePathFromDescriptorPath(std::wstring& MaterialTemplatePath, std::wstring_view FileName) const noexcept
        {
            auto Pos = FileName.rfind(L".lionprj");
            if (Pos != std::wstring::npos) Pos += sizeof(".lionprj");
            else if (Pos = FileName.rfind(L".lionlib"); Pos != std::wstring::npos) Pos += sizeof(".lionlib");
            else xerr::create_f<xresource_pipeline::state, "Unable to locate the project path, fail to load the material instance descriptor">();

            const auto ProjectPath = FileName.substr(0, Pos - 1);
            MaterialTemplatePath = std::format(L"{}/Cache/Resources/Logs/Material/{:02X}/{:02X}/{:X}.log/MaterialInstance.txt"
                , ProjectPath
                , m_MaterialRef.m_Instance.m_Value & 0xff
                , (m_MaterialRef.m_Instance.m_Value & 0xff00) >> 8
                , m_MaterialRef.m_Instance.m_Value
            );

            return {};
        }

        xerr Serialize(bool isReading, std::wstring_view FileName, xproperty::settings::context& Context) noexcept override
        {
            // Load the actual descriptor
            if ( auto Err = xresource_pipeline::descriptor::base::Serialize(isReading, FileName, Context); Err)
                return Err;

            // Done if we are writing 
            if ( not isReading) return {};

            // Otherwise may have other work to do...
            // If we have not material assign then there is nothing to do again...
            if (m_MaterialRef.empty()) return {};

            // Otherwise we must load the material-instance template and merge it with ours
            // because things may have changed...
            xmaterial_instance::descriptor MaterialDescriptorTemplate;
            {
                //
                // Load the template
                //
                std::wstring                    MaterialRefDescriptorPath;
                xtextfile::stream               File;
                xproperty::settings::context    C;

                if (auto Err = getTemplatePathFromDescriptorPath(MaterialRefDescriptorPath, FileName); Err)
                    return Err;

                if (auto Err = File.Open(true, MaterialRefDescriptorPath, xtextfile::file_type::TEXT); Err)
                    return xerr::create_f<xresource_pipeline::state, "Error opening the material reference descriptor">(Err);

                if (auto Err = xproperty::sprop::serializer::Stream(File, MaterialDescriptorTemplate, C); Err)
                    return xerr::create_f<xresource_pipeline::state, "Error reading the material reference descriptor">(Err);
            }

            //
            // Set the user textures for the template with all the defaults
            //
            MaterialDescriptorTemplate.setupDefaults();

            //
            // Now Set the user textures based on our descriptor
            //
            for ( auto& E : m_lTextureDefaults)
            {
                const auto Index = static_cast<int>(&E - m_lTextureDefaults.data());

                // Did the user override this texture?
                if (m_lTextures[Index] != m_lFinalTextures[E.m_Index].m_TextureRef)
                {
                    bool bFound = false;

                    // Search if our template had this texture
                    for (auto& DT : MaterialDescriptorTemplate.m_lTextureDefaults)
                    {
                        if (E.m_GUID == DT.m_GUID)
                        {
                            bFound = true;
                            const auto iUserTempl = static_cast<int>(&DT - MaterialDescriptorTemplate.m_lTextureDefaults.data());
                            MaterialDescriptorTemplate.m_lTextures[iUserTempl] = m_lTextures[Index];
                            break;
                        }
                    }

                    if (bFound == false)
                    {
                        printf("WARNING: Unable to find in the material instance for texture entry %s (We will ignore it and leave the default)\n", E.m_Name.c_str());
                    }
                }
            }

            //
            // OK We can set the new data now
            //
            m_lTextureDefaults  = std::move(MaterialDescriptorTemplate.m_lTextureDefaults);
            m_lTextures         = std::move(MaterialDescriptorTemplate.m_lTextures);
            m_lFinalTextures    = std::move(MaterialDescriptorTemplate.m_lFinalTextures);

            return {};
        }

        xrsc::material_ref                  m_MaterialRef           = {};
        std::vector<default_texture_entry>  m_lTextureDefaults      = {};
        std::vector<xrsc::texture_ref>      m_lTextures             = {};
        std::vector<final_texture_entry>    m_lFinalTextures        = {};

    };
    XPROPERTY_VREG(descriptor)


    //--------------------------------------------------------------------------------------

    struct factory final : xresource_pipeline::factory_base
    {
        using xresource_pipeline::factory_base::factory_base;

        std::unique_ptr<xresource_pipeline::descriptor::base> CreateDescriptor(void) const noexcept override
        {
            return std::make_unique<descriptor>();
        };

        xresource::type_guid ResourceTypeGUID(void) const noexcept override
        {
            return resource_type_guid_v;
        }

        const char* ResourceTypeName(void) const noexcept override
        {
            return "Material Instance";
        }

        const xproperty::type::object& ResourceXPropertyObject(void) const noexcept override
        {
            return *xproperty::getObjectByType<descriptor>();
        }
    };

    inline static factory g_Factory{};
}

#endif