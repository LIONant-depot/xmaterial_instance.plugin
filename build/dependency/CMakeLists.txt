include(${CMAKE_BINARY_DIR}/_deps/xcmake_tools/Common.cmake)

# Fetch dependencies
FetchAndPopulate("https://github.com/LIONant-depot/xserializer.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xresource_pipeline_v2.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xresource_mgr.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xstrtool.git" "main" )

DefineInterfaceComponent(xmaterial_instance_compiler_ "dependencies"
  "source/xmaterial_instance_data_file.h"
  "source/xmaterial_instance_runtime.h"
  "source/xmaterial_instance_xgpu_rsc_loader.h"
  "source/xmaterial_intance_descriptor.h"
)
