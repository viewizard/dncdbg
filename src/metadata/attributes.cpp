// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/attributes.h"
#include "metadata/typeprinter.h"
#include <algorithm>
#include <functional>

namespace dncdbg
{

static bool ForEachAttribute(IMetaDataImport *pMD, mdToken tok, const std::function<bool(const std::string &AttrName)> &cb)
{
    bool found = false;
    ULONG numAttributes = 0;
    HCORENUM fEnum = nullptr;
    mdCustomAttribute attr = 0;
    while (SUCCEEDED(pMD->EnumCustomAttributes(&fEnum, tok, 0, &attr, 1, &numAttributes)) && numAttributes != 0)
    {
        std::string mdName;
        mdToken ptkObj = mdTokenNil;
        mdToken ptkType = mdTokenNil;
        if (FAILED(pMD->GetCustomAttributeProps(attr, &ptkObj, &ptkType, nullptr, nullptr)) ||
            FAILED(TypePrinter::NameForToken(ptkType, pMD, mdName, true, nullptr)))
            continue;

        found = cb(mdName);
        if (found)
            break;
    }
    pMD->CloseEnum(fEnum);
    return found;
}

bool HasAttribute(IMetaDataImport *pMD, mdToken tok, const std::string_view &attrName)
{
    return ForEachAttribute(pMD, tok,
        [&attrName](const std::string &AttrName) -> bool
        {
            return AttrName == attrName;
        });
}

bool HasAttribute(IMetaDataImport *pMD, mdToken tok, const std::vector<std::string_view> &attrNames)
{
    return ForEachAttribute(pMD, tok,
        [&attrNames](const std::string &AttrName) -> bool
        {
            return std::find(attrNames.begin(), attrNames.end(), AttrName) != attrNames.end();
        });
}

} // namespace dncdbg
