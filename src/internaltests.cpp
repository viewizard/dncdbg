// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef DEBUG_INTERNAL_TESTS

#include "debuginfo/sourcefilemap.h"
#include "utils/utftoupper.h"
#include <base64/base64.hpp>
#include <json/json.hpp>
#include <cassert>
#include <cstring>
#include <map>
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
        std::map<std::string, std::string> sourceFileMap;
        sourceFileMap.emplace(R"(C:\Dir1)", "/dir1");
        sourceFileMap.emplace("/dir2", R"(C:\Dir2)");
        sourceFileMap.emplace(R"(C:\Test)", "/testdir");
        sourceFileMap.emplace(R"(C:\Test\Sub)", "/testdir/sub");
        sourceFileMap.emplace("/testdir", R"(C:\Test\Sub)");
        sourceFileMap.emplace(R"(C:\Test2\Sub2)", "/test\\dir/sub");
        sourceFileMap.emplace(R"(C:\test1\test3\Project.cs)", "/test1/test2/file.cs");
        dncdbg::SourceFileMap::SetSourceFileMap(std::move(sourceFileMap));

        assert(std::string{"/dir1/Project.cs"} == dncdbg::SourceFileMap::Path(R"(C:\Dir1\Project.cs)"));
        assert(std::string{R"(C:\Dir2\Project.cs)"} == dncdbg::SourceFileMap::Path("/dir2/Project.cs"));
        assert(std::string{"/testdir/sub/Project.cs"} == dncdbg::SourceFileMap::Path(R"(C:\Test\Sub\Project.cs)"));
        assert(std::string{R"(C:\Test\Sub\Project.cs)"} == dncdbg::SourceFileMap::Path("/testdir/Project.cs"));
        assert(std::string{"/test\\dir/sub/Project.cs"} == dncdbg::SourceFileMap::Path(R"(C:\Test2\Sub2\Project.cs)"));
        assert(std::string{"/test1/test2/file.cs"} == dncdbg::SourceFileMap::Path(R"(C:\test1\test3\Project.cs)"));

        std::map<std::string, std::string> sourceFileMapEmpty;
        dncdbg::SourceFileMap::SetSourceFileMap(std::move(sourceFileMapEmpty));
    }

    // Test UTF-8 to uppercase
    {
        const std::string testString = dncdbg::ToUppercase("привет, hello, auf wiedersehen, grüße, καλημέρα");
        assert(testString == std::string("ПРИВЕТ, HELLO, AUF WIEDERSEHEN, GRÜSSE, ΚΑΛΗΜΈΡΑ") ||
               testString == std::string("ПРИВЕТ, HELLO, AUF WIEDERSEHEN, GRÜßE, ΚΑΛΗΜΈΡΑ") ||
               testString == std::string("ПРИВЕТ, HELLO, AUF WIEDERSEHEN, GRÜẞE, ΚΑΛΗΜΈΡΑ"));
    }

    // Test base64 encoding and decoding
    {
        auto encoded = base64::to_base64("Hello, World!");
        assert(encoded == "SGVsbG8sIFdvcmxkIQ==");

        const auto decoded = base64::from_base64("SGVsbG8sIFdvcmxkIQ==");
        assert(decoded == "Hello, World!");

        constexpr int32_t value = -34532535;
        int32_t i1 = value;

        encoded = base64::to_base64({reinterpret_cast<char *>(&i1), sizeof(i1)});
#if BIGENDIAN
        // TODO
#else
        assert(encoded == "SRPx/Q==");
#endif

        auto decVector = base64::decode_into<std::vector<uint8_t>>(encoded);
        int32_t i2 = 0;
        assert(decVector.size() == sizeof(int32_t));
        std::memcpy(&i2, decVector.data(), sizeof(int32_t));
        assert(i2 == value);
    }
}

#endif // DEBUG_INTERNAL_TESTS
