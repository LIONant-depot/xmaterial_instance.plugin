#ifndef XMATERIAL_INSTANCE_DATA_FILE_H
#define XMATERIAL_INSTANCE_DATA_FILE_H

#include "dependencies/xserializer/source/xserializer.h"
#include "plugins/xmaterial.plugin/source/xmaterial_xgpu_rsc_loader.h"
#include "plugins/xtexture.plugin/source/xtexture_xgpu_rsc_loader.h"

namespace xmaterial_instance
{
    struct data_file
    {
        // v2: the instance is fully resolved (documentation/material_contract.md section 5): its parameter block in the material's layout + the layout hash the renderer
        //     checks against the material's. A v1 file (older size and version) is refused by the serializer and loads as null, like an uncompiled resource; the pipeline
        //     recompiles it because the compiler executable is newer.
        // v3: the instance's Double Sided (m_DoubleSided), resolved through its chain
        inline static constexpr auto xserializer_version_v = 3;

        //-------------------------------------------------------------------------

        struct texture
        {
            xrsc::texture_ref   m_TexureRef;
        };

        //-------------------------------------------------------------------------

        inline          data_file       (void)                          noexcept = default;
        inline          data_file       (xserializer::stream& Steaming) noexcept;

        inline std::span<texture>         getTextures(void) { return { m_pTextureList, (std::size_t)m_nTexturesList }; }

        xrsc::material_ref  m_MaterialRef;
        texture*            m_pTextureList;
        std::uint8_t        m_nTexturesList;
        std::uint16_t       m_BlockBytes;               // the resolved parameter block, a multiple of 16 (0: the material reads no parameters)
        float*              m_pParams;                  // m_BlockBytes/4 floats in the material's layout
        std::uint64_t       m_LayoutHash;               // the manifest's: must equal the material's (xmaterial::data_file::m_LayoutHash)
        std::int8_t         m_DoubleSided;              // -1: the material's render setting; 0 off, 1 on: the instance's (or a parent's) override
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
    template<> inline
    xerr SerializeIO<xmaterial_instance::data_file>(xserializer::stream& Stream, const xmaterial_instance::data_file& MaterialI) noexcept
    {
        xerr Err;
        false
        || (Err = Stream.Serialize(MaterialI.m_MaterialRef.m_Instance.m_Value))
        || (Err = Stream.Serialize(MaterialI.m_pTextureList, MaterialI.m_nTexturesList))
        || (Err = Stream.Serialize(MaterialI.m_nTexturesList))
        || (Err = Stream.Serialize(MaterialI.m_pParams, MaterialI.m_BlockBytes / 4u))
        || (Err = Stream.Serialize(MaterialI.m_BlockBytes))
        || (Err = Stream.Serialize(MaterialI.m_LayoutHash))
        || (Err = Stream.Serialize(MaterialI.m_DoubleSided))
        ;

        return Err;
    }

    //-------------------------------------------------------------------------
    template<> inline
    xerr SerializeIO<xmaterial_instance::data_file::texture>(xserializer::stream& Stream, const xmaterial_instance::data_file::texture& Texture) noexcept
    {
        xerr Err;
        false
        || (Err = Stream.Serialize(Texture.m_TexureRef.m_Instance.m_Value))
        ;

        return Err;
    }
}

#endif
