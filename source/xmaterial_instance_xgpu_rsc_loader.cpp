
#include "xmaterial_Instance_xgpu_rsc_loader.h"
#include "xmaterial_instance_runtime.h"
#include "dependencies/xserializer/source/xserializer.h"
#include "dependencies/xresource_guid/source/bridges/xresource_xproperty_bridge.h"

//
// We will register the loader
//
inline static auto s_MaterialRegistrations = xresource::common_registrations<xrsc::material_instance_type_guid_v>{};

//------------------------------------------------------------------

xresource::loader< xrsc::material_instance_type_guid_v >::data_type* xresource::loader< xrsc::material_instance_type_guid_v >::Load( xresource::mgr& Mgr, const full_guid& GUID )
{
    auto&                   UserData            = Mgr.getUserData<resource_mgr_user_data>();
    std::wstring            Path                = Mgr.getResourcePath(GUID, type_name_v);
    xmaterial_instance::rt* pMaterialInstance   = nullptr;

    //static_assert( sizeof(xmaterial_instance::rt) == sizeof(xmaterial::data_file));

    // Load the xbitmap
    xserializer::stream Stream;
    if (auto Err = Stream.Load(Path, pMaterialInstance); Err)
    {
        assert(false);
    }

    // Get the material
    auto& Material = *Mgr.getResource( pMaterialInstance->m_MaterialRef );

    // Force load all the dependencies 
    for (int i=0; i< pMaterialInstance->m_nTexturesList; ++i )
    {
        // This must be a system texture... (Meaning the system need to provide this texture)
        if (pMaterialInstance->m_pTextureList[i].m_TexureRef.empty()) continue;

        assert(not pMaterialInstance->m_pTextureList[i].m_TexureRef.empty());
        if ( auto p = Mgr.getResource(pMaterialInstance->m_pTextureList[i].m_TexureRef ); p == nullptr)
        {
            // Set default texture here as well?
            assert(false);
        }
    }

    // Return the texture
    return pMaterialInstance;
}

//------------------------------------------------------------------

void xresource::loader< xrsc::material_instance_type_guid_v >::Destroy(xresource::mgr& Mgr, data_type&& Data, const full_guid& GUID)
{
    auto& UserData = Mgr.getUserData<resource_mgr_user_data>();

    // Release reference to the material
    Mgr.ReleaseRef(Data.m_MaterialRef);

    //
    // Free up dependencies
    //
    for (int i = 0; i < Data.m_nTexturesList; ++i)
    {
        if (Data.m_pTextureList[i].m_TexureRef.empty() == false)
        {
            Mgr.ReleaseRef( Data.m_pTextureList[i].m_TexureRef );
        }
    }

    //
    // Finally Free the user data
    //
    xserializer::default_memory_handler_v.Free(xserializer::mem_type{ .m_bUnique = true }, &Data);
}

