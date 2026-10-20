// Which textures come along when the material of an instance changes (source/xmaterial_instance_carry.h). Standalone: built and run by
// source/Editors/LevelEditor/smoke/test_material_instance_carry.py. Exit 0 when every check holds.
#include "source/xmaterial_instance_carry.h"

#include <cstdio>

namespace
{
    int g_Failed = 0;

    void Check(bool bOk, const char* pWhat)
    {
        if (bOk) return;
        ++g_Failed;
        std::printf("FAIL: %s\n", pWhat);
    }

    using names = std::vector<std::string>;
    using flags = std::vector<bool>;
    using ints  = std::vector<int>;
}

int main()
{
    using namespace xmaterial_instance;

    // slots with the same name take the texture the person assigned, whatever their place in the new material
    {
        const auto M = MatchCarriedTextures(names{ "SamplerNormal", "SamplerAlbedo", "SamplerORME" }, flags{ true, true, true }, names{ "SamplerAlbedo", "SamplerORME", "SamplerNormal" });
        Check(M == ints{ 1, 2, 0 }, "the textures follow the names, not the places");
    }

    // what the person did not assign is the material's own: the new material's default is used
    {
        const auto M = MatchCarriedTextures(names{ "SamplerNormal", "SamplerAlbedo" }, flags{ false, true }, names{ "SamplerNormal", "SamplerAlbedo" });
        Check(M == ints{ -1, 1 }, "a slot that is the old material's default is not carried");
    }

    // a slot the new material does not have is dropped; a slot the old one did not have starts from the default
    {
        const auto M = MatchCarriedTextures(names{ "SamplerAlbedo", "SamplerEmission" }, flags{ true, true }, names{ "SamplerAlbedo", "SamplerHeight" });
        Check(M == ints{ 0, -1 }, "a name only in the old material goes nowhere, a name only in the new one gets nothing");
    }

    // names are exact
    {
        const auto M = MatchCarriedTextures(names{ "SamplerAlbedo" }, flags{ true }, names{ "sampleralbedo", "SamplerAlbedo2", "SamplerAlbedo" });
        Check(M == ints{ -1, -1, 0 }, "only an identical name matches");
    }

    // a slot is taken from once: two new slots of one name take two old slots of that name, in order; one old slot is not copied twice
    {
        const auto Two = MatchCarriedTextures(names{ "Map", "Map" }, flags{ true, true }, names{ "Map", "Map" });
        Check(Two == ints{ 0, 1 }, "two slots of one name take two old ones, in order");
        const auto One = MatchCarriedTextures(names{ "Map" }, flags{ true }, names{ "Map", "Map" });
        Check(One == ints{ 0, -1 }, "an old slot is carried once");
    }

    // nothing to carry
    {
        Check(MatchCarriedTextures({}, {}, names{ "A", "B" }) == ints{ -1, -1 }, "an instance with no material before carries nothing");
        Check(MatchCarriedTextures(names{ "A" }, flags{ true }, {}).empty(), "a material with no textures has nothing to fill");
        Check(MatchCarriedTextures(names{ "A", "B" }, flags{ true, true }, names{ "A", "B" }) == ints{ 0, 1 } , "the same material again keeps every assigned texture");
        // (a flags list shorter than the names, which a broken descriptor can give, must not read past its end)
        Check(MatchCarriedTextures(names{ "A", "B" }, flags{ true }, names{ "B" }) == ints{ -1 }, "a short flags list is not read past its end");
    }

    if (g_Failed == 0) std::printf("all checks passed\n");
    return g_Failed == 0 ? 0 : 1;
}
