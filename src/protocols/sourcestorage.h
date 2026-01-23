// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "cor.h"
#include "interfaces/idebugger.h"

#include <vector>
#include <string>
#include <list>

#define STORAGE_MAX_SIZE    1000000

namespace dncdbg
{

class SourceStorage 
{
    struct SourceFile
    {
        std::string filePath;
        std::vector<char*> lines;
        char* text;
        int size;
    };

    std::list<SourceFile*> files;
    IDebugger* m_dbg;
    int totalLen;

private:
    HRESULT loadFile(std::string& file, const char **errMessage);

public:
    SourceStorage(IDebugger* d) 
    {
        m_dbg = d;
        totalLen = 0;
    }
    ~SourceStorage();

    char* getLine(std::string& file, int linenum, const char **errMessage);

}; // class sourcestorage
} // namespace