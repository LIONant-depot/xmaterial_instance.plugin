// Which overrides come along when the material of an instance changes (source/xmaterial_instance_carry.h). Standalone: built and run by
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

    using ids = std::vector<std::uint64_t>;
    using ps  = std::vector<xmaterial_instance::carry_param>;
}

int main()
{
    using namespace xmaterial_instance;

    // an id the new material has stays where it is, whatever its name or place
    Check(MatchCarriedOverrides(ps{ { 1, "A", 0 }, { 2, "B", 4 } }, ps{ { 2, "Renamed", 4 }, { 1, "A", 0 } }) == ids{ 1, 2 }, "the ids decide, not the names or the places");

    // an id it does not have goes to the parameter of the same name and type
    Check(MatchCarriedOverrides(ps{ { 7, "Tint", 4 }, { 8, "Albedo", 5 } }, ps{ { 70, "Albedo", 5 }, { 71, "Tint", 4 } }) == ids{ 71, 70 }, "the same name and type under another id carries");
    Check(MatchCarriedOverrides(ps{ { 7, "Tint", 4 } }, ps{ { 70, "Tint", 3 } }) == ids{ 0 }, "the same name with another type is not the same parameter");
    Check(MatchCarriedOverrides(ps{ { 7, "Tint", 4 } }, ps{ { 70, "tint", 4 } }) == ids{ 0 }, "names are exact");
    Check(MatchCarriedOverrides(ps{ { 7, "", 4 } }, ps{ { 70, "", 4 } }) == ids{ 0 }, "an override with no name hint never matches by name");

    // an id match wins over a name match, and a parameter is taken once
    Check(MatchCarriedOverrides(ps{ { 1, "X", 0 } }, ps{ { 1, "Y", 0 }, { 2, "X", 0 } }) == ids{ 1 }, "the id match is served before the name match");
    Check(MatchCarriedOverrides(ps{ { 7, "Map", 5 }, { 8, "Map", 5 } }, ps{ { 70, "Map", 5 } }) == ids{ 70, 0 }, "a parameter is carried to once, the other is an orphan");
    Check(MatchCarriedOverrides(ps{ { 7, "Map", 5 }, { 70, "Other", 5 } }, ps{ { 70, "Map", 5 } }) == ids{ 0, 70 }, "a parameter an id match took is not given to a name");

    // nothing to carry
    Check(MatchCarriedOverrides({}, ps{ { 1, "A", 0 } }).empty(), "no overrides, nothing to carry");
    Check(MatchCarriedOverrides(ps{ { 1, "A", 0 } }, {}) == ids{ 0 }, "a material with no parameters keeps everything as orphans");

    if (g_Failed == 0) std::printf("all checks passed\n");
    return g_Failed == 0 ? 0 : 1;
}
