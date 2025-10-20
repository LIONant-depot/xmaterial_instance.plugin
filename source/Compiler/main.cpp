#include "xmaterial_instance_compiler.h"
#include <string>

int main(int argc, const char* argv[])
{
    //for debug purpose
    if constexpr (!false)
    {
        static const char* pDebugArgs[] =
        { "compiler location" 
        , "-PROJECT"
        , "D:\\LIONant\\xGPU\\bin_dependencies\\xresource_pipeline_example.lion_project" 
        , "-OPTIMIZATION"
        , "O1"
        , "-DEBUG"
        , "D0"
        , "-DESCRIPTOR"
        , "Descriptors\\MaterialInstance\\05\\80\\92AE485F322C8005.desc" 
        , "-OUTPUT"
        , "D:\\LIONant\\xGPU\\bin_dependencies\\xresource_pipeline_example.lion_project\\Cache\\Resources\\Platforms\\WINDOWS"
        };

        argv = pDebugArgs;
        argc = static_cast<int>(sizeof(pDebugArgs) / sizeof(pDebugArgs[0]));
    }

    auto shaderCompilerPipeline = xmaterial_instance_compiler::instance::Create();
    

    if (auto Err = shaderCompilerPipeline->Parse(argc, argv); Err)
    {
        Err.ForEachInChain([&](xerr Error)
        {
            xstrtool::print("ERROR: {}\n", Err.getMessage());
            if (auto Hint = Err.getHint(); Hint.empty() == false)
                xstrtool::print("- HINT: {}\n", Hint);
        });
        return 1;
    }

    if (auto Err = shaderCompilerPipeline->Compile(); Err)
    {
        xstrtool::print("{}\nERROR: Fail to compile(2)\n", Err.getMessage());
        if (auto Hint = Err.getHint(); Hint.empty() == false)
            xstrtool::print("- HINT: {}\n", Hint);
        return Err.getStateUID();
    }
    
    return 0;
}