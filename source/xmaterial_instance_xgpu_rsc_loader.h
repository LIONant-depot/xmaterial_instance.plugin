#ifndef XMATERIAL_INSTANCE_XGPU_XRSC_GUID_LOADER_H
#define XMATERIAL_INSTANCE_XGPU_XRSC_GUID_LOADER_H
#pragma once

#include "dependencies/xresource_mgr/source/xresource_mgr.h"

namespace xrsc
{
    // While this should be just a type... it also happens to be an instance... the instance of the texture_plugin
    // So while generating the type guid we must treat it as an instance.
    inline static constexpr auto    material_instance_type_guid_v   = xresource::type_guid(xresource::guid_generator::Instance64FromString("MaterialInstance"));
    using                           material_instance_ref           = xresource::def_guid<material_instance_type_guid_v>;
}

// Predefine the runtime structure
namespace xmaterial_instance{ struct rt; }

template<>
struct xresource::loader< xrsc::material_instance_type_guid_v >                                 // Now we specify the loader and we must fill in all the information
{
    //--- Expected static parameters ---
    constexpr static inline auto         type_name_v        = L"MaterialInstance";              // This name is used to construct the path to the resource (if not provided)
    constexpr static inline auto         use_death_march_v  = false;                            // xGPU already has a death march implemented inside itself...
    using                                data_type          = xmaterial_instance::rt;

    static data_type*                    Load          ( xresource::mgr& Mgr,                    const full_guid& GUID );
    static void                          Destroy       ( xresource::mgr& Mgr, data_type&& Data,  const full_guid& GUID );
};

#endif