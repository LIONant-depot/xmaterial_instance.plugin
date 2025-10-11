#ifndef XMATERIAL_INSTANCE_DATA_FILE_H
#define XMATERIAL_INSTANCE_DATA_FILE_H

#include "dependencies/xserializer/source/xserializer.h"
#include "Plugins/xmaterial.plugin/source/xmaterial_xgpu_rsc_loader.h"
#include "Plugins/xtexture.plugin/source/xtexture_xgpu_rsc_loader.h"

namespace xmaterial_instance
{
    struct data_file
    {
        inline static constexpr auto xserializer_version_v = 1;

        //-------------------------------------------------------------------------

        struct texture
        {
            xrsc::texture_ref   m_TexureRef;
            int                 m_Index;
        };

        //-------------------------------------------------------------------------

        inline          data_file       (void)                          noexcept = default;
        inline          data_file       (xserializer::stream& Steaming) noexcept;


        xrsc::material_ref  m_MaterialRef;
        texture*            m_pDefaultTextureList;
        std::uint8_t        m_nDefaultTexturesList;
    };

    //-------------------------------------------------------------------------

    data_file::data_file(xserializer::stream& Steaming) noexcept
    {
        assert( Steaming.getResourceVersion() == data_file::xserializer_version_v );
    }
}

//-------------------------------------------------------------------------
// serializer
//-------------------------------------------------------------------------
namespace xserializer::io_functions
{
    //-------------------------------------------------------------------------
    template<>
    xerr SerializeIO<xmaterial_instance::data_file>(xserializer::stream& Stream, const xmaterial_instance::data_file& MaterialI) noexcept
    {
        xerr Err;
        false
        || (Err = Stream.Serialize(MaterialI.m_MaterialRef.m_Instance.m_Value))
        || (Err = Stream.Serialize(MaterialI.m_pDefaultTextureList, MaterialI.m_nDefaultTexturesList))
        || (Err = Stream.Serialize(MaterialI.m_nDefaultTexturesList))
        ;

        return Err;
    }

    //-------------------------------------------------------------------------
    template<>
    xerr SerializeIO<xmaterial_instance::data_file::texture>(xserializer::stream& Stream, const xmaterial_instance::data_file::texture& Texture) noexcept
    {
        xerr Err;
        false
        || (Err = Stream.Serialize(Texture.m_TexureRef.m_Instance.m_Value))
        || (Err = Stream.Serialize(Texture.m_Index))
        ;

        return Err;
    }
}

#endif
