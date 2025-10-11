#ifndef SHADER_COMPILER_H
#define SHADER_COMPILER_H
#pragma once
#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "dependencies/xresource_guid/source/xresource_guid.h"

namespace xmaterial_instance_compiler
{
    enum state : std::uint8_t
    { OK
    , FAILURE
    };

    constexpr static auto type_guid_v = xresource::type_guid("Material Instance");

    struct instance : xresource_pipeline::compiler::base
    {
        static std::unique_ptr<instance> Create(void);
    };
}

#endif