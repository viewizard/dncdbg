// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef DEBUG_INTERNAL_TESTS

#include "debuginfo/sourcefilemap.h"
#include "utils/utftoupper.h"
#include <json/json.hpp>
#include <cassert>
#include <string>

void RunInternalTests() // NOLINT(misc-use-internal-linkage)
{
    // nlohmann/json has internal dump serializer and cares about escaped characters
    {
        nlohmann::json testj;
        testj.emplace("test", std::string("te\023st\nte\023st\nte\023st\nte\023st\nte\023st234\n"));
        const std::string expected(R"({"test":"te\u0013st\nte\u0013st\nte\u0013st\nte\u0013st\nte\u0013st234\n"})");
        assert(testj.dump() == expected);
    }

    // SourceFileMap
    {
        dncdbg::SourceFileMap::GetMap().emplace(R"(C:\Dir1)", "/dir1");
        dncdbg::SourceFileMap::GetMap().emplace("/dir2", R"(C:\Dir2)");
        dncdbg::SourceFileMap::GetMap().emplace(R"(C:\Test)", "/testdir");
        dncdbg::SourceFileMap::GetMap().emplace(R"(C:\Test\Sub)", "/testdir/sub");
        dncdbg::SourceFileMap::GetMap().emplace("/testdir", R"(C:\Test\Sub)");
        dncdbg::SourceFileMap::GetMap().emplace(R"(C:\Test2\Sub2)", "/test\\dir/sub");
        dncdbg::SourceFileMap::GetMap().emplace(R"(C:\test1\test3\Project.cs)", "/test1/test2/file.cs");
        assert(std::string{"/dir1/Project.cs"} == dncdbg::SourceFileMap::Path(R"(C:\Dir1\Project.cs)"));
        assert(std::string{R"(C:\Dir2\Project.cs)"} == dncdbg::SourceFileMap::Path("/dir2/Project.cs"));
        assert(std::string{"/testdir/sub/Project.cs"} == dncdbg::SourceFileMap::Path(R"(C:\Test\Sub\Project.cs)"));
        assert(std::string{R"(C:\Test\Sub\Project.cs)"} == dncdbg::SourceFileMap::Path("/testdir/Project.cs"));
        assert(std::string{"/test\\dir/sub/Project.cs"} == dncdbg::SourceFileMap::Path(R"(C:\Test2\Sub2\Project.cs)"));
        assert(std::string{"/test1/test2/file.cs"} == dncdbg::SourceFileMap::Path(R"(C:\test1\test3\Project.cs)"));
        dncdbg::SourceFileMap::GetMap().clear();
    }

    // Test UTF-8 to uppercase
    {
        const std::string testString = dncdbg::to_uppercase("привет, hello, auf wiedersehen, grüße, καλημέρα");
        assert(testString == std::string("ПРИВЕТ, HELLO, AUF WIEDERSEHEN, GRÜSSE, ΚΑΛΗΜΈΡΑ") ||
               testString == std::string("ПРИВЕТ, HELLO, AUF WIEDERSEHEN, GRÜßE, ΚΑΛΗΜΈΡΑ"));
    }
}

#endif // DEBUG_INTERNAL_TESTS
