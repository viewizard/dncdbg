// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/typeprinter.h"
#include "metadata/sigparse.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <sstream>
#include <unordered_map>
#include <string_view>
#include <vector>

namespace dncdbg::TypePrinter
{

namespace
{

std::string ConsumeGenericArgs(const std::string &name, std::list<std::string> &args)
{
    if (args.empty())
    {
        return name;
    }

    const std::size_t offset = name.find_last_not_of("0123456789");

    if (offset == std::string::npos || offset == name.size() - 1 || name.at(offset) != '`')
    {
        return name;
    }

    unsigned long numArgs = 0;
    try
    {
        numArgs = std::stoul(name.substr(offset + 1));
    }
    catch (const std::invalid_argument &/*e*/)
    {
        return name;
    }
    catch (const std::out_of_range &/*e*/)
    {
        return name;
    }

    if (numArgs == 0 || numArgs > args.size())
    {
        return name;
    }

    std::ostringstream ss;
    ss << name.substr(0, offset);
    ss << "<";
    const char *sep = "";
    while ((numArgs--) != 0U)
    {
        ss << sep;
        sep = ", ";
        ss << args.front();
        args.pop_front();
    }
    ss << ">";
    return ss.str();
}

HRESULT NameForTypeRef(mdTypeRef tkTypeRef, IMetaDataImport *pMDImport, std::string &mdName)
{
    // Note, instead of GetTypeDefProps(), GetTypeRefProps() return fully-qualified name.
    // CoreCLR use dynamic allocated or size fixed buffers up to 16kb for GetTypeRefProps().
    HRESULT Status = S_OK;
    ULONG refNameSize = 0;
    IfFailRet(pMDImport->GetTypeRefProps(tkTypeRef, nullptr, nullptr, 0, &refNameSize));

    WSTRING refName(refNameSize, 0);
    IfFailRet(pMDImport->GetTypeRefProps(tkTypeRef, nullptr, refName.data(), refNameSize, nullptr));

    mdName = to_utf8(refName.c_str());

    return S_OK;
}

HRESULT AddGenericArgs(ICorDebugType *pType, std::list<std::string> &args)
{
    ToRelease<ICorDebugTypeEnum> trTypeEnum;

    if (SUCCEEDED(pType->EnumerateTypeParameters(&trTypeEnum)))
    {
        ULONG fetched = 0;
        ToRelease<ICorDebugType> trCurrentTypeParam;

        while (SUCCEEDED(trTypeEnum->Next(1, &trCurrentTypeParam, &fetched)) && fetched == 1)
        {
            std::string name;
            GetTypeOfValue(trCurrentTypeParam, name);
            args.emplace_back(name);
            trCurrentTypeParam.Free();
        }
    }

    return S_OK;
}

HRESULT AddGenericArgs(ICorDebugFrame *pFrame, std::list<std::string> &args)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugILFrame2> trILFrame2;
    IfFailRet(pFrame->QueryInterface(IID_ICorDebugILFrame2, reinterpret_cast<void **>(&trILFrame2)));

    ToRelease<ICorDebugTypeEnum> trTypeEnum;
    if (SUCCEEDED(trILFrame2->EnumerateTypeParameters(&trTypeEnum)))
    {
        ULONG numTypes = 0;
        ToRelease<ICorDebugType> trCurrentTypeParam;

        while (SUCCEEDED(trTypeEnum->Next(1, &trCurrentTypeParam, &numTypes)) && numTypes == 1)
        {
            std::string name;
            GetTypeOfValue(trCurrentTypeParam, name);
            args.emplace_back(name);
            trCurrentTypeParam.Free();
        }
    }

    return S_OK;
}

} // unnamed namespace

std::string RenameToSystem(const std::string &typeName)
{
    static const std::unordered_map<std::string, std::string> cs2system = {
        {"void",    "System.Void"},
        {"bool",    "System.Boolean"},
        {"byte",    "System.Byte"},
        {"sbyte",   "System.SByte"},
        {"char",    "System.Char"},
        {"decimal", "System.Decimal"},
        {"double",  "System.Double"},
        {"float",   "System.Single"},
        {"int",     "System.Int32"},
        {"uint",    "System.UInt32"},
        {"long",    "System.Int64"},
        {"ulong",   "System.UInt64"},
        {"object",  "System.Object"},
        {"short",   "System.Int16"},
        {"ushort",  "System.UInt16"},
        {"string",  "System.String"},
        {"IntPtr",  "System.IntPtr"},
        {"UIntPtr", "System.UIntPtr"}
    };
    auto renamed = cs2system.find(typeName);
    return renamed != cs2system.end() ? renamed->second : typeName;
}

std::string RenameToCSharp(const std::string &typeName)
{
    static const std::unordered_map<std::string, std::string> system2cs = {
        {"System.Void",    "void"},
        {"System.Boolean", "bool"},
        {"System.Byte",    "byte"},
        {"System.SByte",   "sbyte"},
        {"System.Char",    "char"},
        {"System.Decimal", "decimal"},
        {"System.Double",  "double"},
        {"System.Single",  "float"},
        {"System.Int32",   "int"},
        {"System.UInt32",  "uint"},
        {"System.Int64",   "long"},
        {"System.UInt64",  "ulong"},
        {"System.Object",  "object"},
        {"System.Int16",   "short"},
        {"System.UInt16",  "ushort"},
        {"System.String",  "string"},
        {"System.IntPtr",  "IntPtr"},
        {"System.UIntPtr", "UIntPtr"}
    };
    auto renamed = system2cs.find(typeName);
    return renamed != system2cs.end() ? renamed->second : typeName;
}

HRESULT NameForTypeDef(mdTypeDef tkTypeDef, IMetaDataImport *pMDImport,
                       std::string &mdName, std::list<std::string> *args)
{
    HRESULT Status = S_OK;
    mdTypeDef currentType = tkTypeDef;
    // Stack to hold nested type names as we traverse up the hierarchy
    std::vector<std::string> nameStack;

    // Phase 1: Collect all nested type names (iteratively walk up the hierarchy)
    while (true)
    {
        ULONG nameLen = 0;
        IfFailRet(pMDImport->GetTypeDefProps(currentType, nullptr, 0,
                                             &nameLen, nullptr, nullptr));

        DWORD flags = 0;
        WSTRING name(nameLen - 1, '\0'); // nameLen includes null terminator
        IfFailRet(pMDImport->GetTypeDefProps(currentType, name.data(), nameLen,
                                             nullptr, &flags, nullptr));

        nameStack.push_back(to_utf8(name.c_str()));

        if (!IsTdNested(flags))
        {
            break; // Reached the outermost non-nested type
        }

        // Move to enclosing class
        mdTypeDef enclosingClass = mdTypeDefNil;
        IfFailRet(pMDImport->GetNestedClassProps(currentType, &enclosingClass));

        currentType = enclosingClass;
    }

    // Phase 2: Build the fully-qualified name from outside-in
    // nameStack contains: [innermost, ..., outermost]
    // Process generic args from outermost to innermost
    mdName.clear();
    for (auto it = nameStack.rbegin(); it != nameStack.rend(); ++it)
    {
        if (!mdName.empty())
        {
            mdName += ".";
        }

        std::string currentName = *it;
        if (args != nullptr)
        {
            currentName = ConsumeGenericArgs(currentName, *args);
        }
        mdName += currentName;
    }

    return S_OK;
}

HRESULT NameForTypeByToken(mdToken mb, IMetaDataImport *pMDImport, std::string &mdName, std::list<std::string> *args)
{
    mdName[0] = L'\0';
    if (TypeFromToken(mb) != mdtTypeDef && TypeFromToken(mb) != mdtTypeRef)
    {
        return E_FAIL;
    }

    HRESULT hr = S_OK;
    if (TypeFromToken(mb) == mdtTypeDef)
    {
        hr = NameForTypeDef(mb, pMDImport, mdName, args);
    }
    else if (TypeFromToken(mb) == mdtTypeRef)
    {
        hr = NameForTypeRef(mb, pMDImport, mdName);
    }
    else
    {
        hr = E_FAIL;
    }

    return hr;
}

HRESULT NameForTypeByType(ICorDebugType *pType, std::string &mdName)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugClass> trClass;
    IfFailRet(pType->GetClass(&trClass));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trClass->GetModule(&trModule));
    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));
    mdToken tk = mdTokenNil;
    IfFailRet(trClass->GetToken(&tk));
    std::list<std::string> args;
    AddGenericArgs(pType, args);
    return NameForTypeByToken(tk, trMDImport, mdName, &args);
}

HRESULT NameForTypeByValue(ICorDebugValue *pValue, std::string &mdName)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue2> trValue2;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
    ToRelease<ICorDebugType> trType;
    IfFailRet(trValue2->GetExactType(&trType));
    return NameForTypeByType(trType, mdName);
}

HRESULT NameForToken(mdToken mb, IMetaDataImport *pMDImport, std::string &mdName, bool bClassName,
                     std::list<std::string> *args)
{
    mdName[0] = L'\0';
    if (TypeFromToken(mb) != mdtTypeDef &&
        TypeFromToken(mb) != mdtFieldDef &&
        TypeFromToken(mb) != mdtMethodDef &&
        TypeFromToken(mb) != mdtMemberRef &&
        TypeFromToken(mb) != mdtTypeRef)
    {
        // ExtOut("unsupported\n");
        return E_FAIL;
    }

    HRESULT Status = S_OK;

    if (TypeFromToken(mb) == mdtTypeDef)
    {
        IfFailRet(NameForTypeDef(mb, pMDImport, mdName, args));
    }
    else if (TypeFromToken(mb) == mdtFieldDef)
    {
        ULONG size = 0;
        IfFailRet(pMDImport->GetMemberProps(mb, nullptr, nullptr, 0, &size, nullptr, nullptr,
                                            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));
        mdTypeDef mdClass = mdTypeDefNil;
        WSTRING name(size - 1, '\0'); // size - string size + null terminated symbol
        IfFailRet(pMDImport->GetMemberProps(mb, &mdClass, name.data(), size, nullptr, nullptr, nullptr,
                                            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));
        if (mdClass != mdTypeDefNil && bClassName)
        {
            IfFailRet(NameForTypeDef(mdClass, pMDImport, mdName, args));
            mdName += ".";
        }
        mdName += to_utf8(name.c_str());
    }
    else if (TypeFromToken(mb) == mdtMethodDef)
    {
        ULONG size = 0;
        IfFailRet(pMDImport->GetMethodProps(mb, nullptr, nullptr, 0, &size,
                                            nullptr, nullptr, nullptr, nullptr, nullptr));
        mdTypeDef mdClass = mdTypeDefNil;
        WSTRING name(size - 1, '\0'); // size - string size + null terminated symbol
        IfFailRet(pMDImport->GetMethodProps(mb, &mdClass, name.data(), size, nullptr,
                                            nullptr, nullptr, nullptr, nullptr, nullptr));
        if (mdClass != mdTypeDefNil && bClassName)
        {
            IfFailRet(NameForTypeDef(mdClass, pMDImport, mdName, args));
            mdName += ".";
        }
        mdName += to_utf8(name.c_str());
    }
    else if (TypeFromToken(mb) == mdtMemberRef)
    {
        ULONG size = 0;
        IfFailRet(pMDImport->GetMemberRefProps(mb, nullptr, nullptr, 0, &size, nullptr, nullptr));
        mdTypeDef mdClass = mdTypeDefNil;
        WSTRING name(size - 1, '\0'); // size - string size + null terminated symbol
        IfFailRet(pMDImport->GetMemberRefProps(mb, &mdClass, name.data(), size, nullptr, nullptr, nullptr));
        if (TypeFromToken(mdClass) == mdtTypeRef && bClassName)
        {
            IfFailRet(NameForTypeRef(mdClass, pMDImport, mdName));
            mdName += ".";
        }
        else if (TypeFromToken(mdClass) == mdtTypeDef && bClassName)
        {
            IfFailRet(NameForTypeDef(mdClass, pMDImport, mdName, args));
            mdName += ".";
        }
        // TODO TypeSpec
        mdName += to_utf8(name.c_str());
    }
    else if (TypeFromToken(mb) == mdtTypeRef)
    {
        IfFailRet(NameForTypeRef(mb, pMDImport, mdName));
    }
    else
    {
        // Unsupported token type
        return CORDBG_E_UNSUPPORTED;
    }


    mdName = RenameToCSharp(mdName);
    return S_OK;
}

HRESULT GetTypeOfValue(ICorDebugValue *pValue, std::string &output)
{
    ToRelease<ICorDebugType> trType;
    ToRelease<ICorDebugValue2> trValue2;
    if (SUCCEEDED(pValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>((&trValue2)))) &&
        SUCCEEDED(trValue2->GetExactType(&trType)))
    {
        return GetTypeOfValue(trType, output);
    }
    else
    {
        output = "<unknown>";
    }

    return S_OK;
}

// From strike.cpp

HRESULT GetTypeOfValue(ICorDebugType *pType, std::string &elementType, std::string &arrayType)
{
    if (pType == nullptr)
    {
        return E_INVALIDARG;
    }

    HRESULT Status = S_OK;

    CorElementType corElemType = ELEMENT_TYPE_MAX;
    IfFailRet(pType->GetType(&corElemType));

    switch (corElemType)
    {
    // List of unsupported CorElementTypes:
    // ELEMENT_TYPE_END            = 0x0,
    // ELEMENT_TYPE_VAR            = 0x13,     // a class type variable VAR <U1>
    // ELEMENT_TYPE_GENERICINST    = 0x15,     // GENERICINST <generic type> <argCnt> <arg1> ... <argn>
    // ELEMENT_TYPE_TYPEDBYREF     = 0x16,     // TYPEDREF  (it takes no args) a typed reference to some other type
    // ELEMENT_TYPE_MVAR           = 0x1e,     // a method type variable MVAR <U1>
    // ELEMENT_TYPE_CMOD_REQD      = 0x1F,     // required C modifier : E_T_CMOD_REQD <mdTypeRef/mdTypeDef>
    // ELEMENT_TYPE_CMOD_OPT       = 0x20,     // optional C modifier : E_T_CMOD_OPT <mdTypeRef/mdTypeDef>
    // ELEMENT_TYPE_INTERNAL       = 0x21,     // INTERNAL <typehandle>
    // ELEMENT_TYPE_MAX            = 0x22,     // first invalid element type
    // ELEMENT_TYPE_MODIFIER       = 0x40,
    // ELEMENT_TYPE_SENTINEL       = 0x01 | ELEMENT_TYPE_MODIFIER, // sentinel for varargs
    // ELEMENT_TYPE_PINNED         = 0x05 | ELEMENT_TYPE_MODIFIER,
    // ELEMENT_TYPE_R4_HFA         = 0x06 | ELEMENT_TYPE_MODIFIER, // used only internally for R4 HFA types
    // ELEMENT_TYPE_R8_HFA         = 0x07 | ELEMENT_TYPE_MODIFIER, // used only internally for R8 HFA types
    default:
    {
        std::ostringstream ss;
        ss << "(Unhandled CorElementType: 0x" << std::hex << corElemType << ")";
        elementType = ss.str();
    }
    break;

    case ELEMENT_TYPE_VALUETYPE:
    case ELEMENT_TYPE_CLASS:
    {
        std::ostringstream ss;
        // Defaults in case we fail...
        elementType = (corElemType == ELEMENT_TYPE_VALUETYPE) ? "struct" : "class";

        mdTypeDef typeDef = mdTypeDefNil;
        ToRelease<ICorDebugClass> trClass;
        if (SUCCEEDED(pType->GetClass(&trClass)) && SUCCEEDED(trClass->GetToken(&typeDef)))
        {
            ToRelease<ICorDebugModule> trModule;
            IfFailRet(trClass->GetModule(&trModule));

            ToRelease<IUnknown> trUnknown;
            IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
            ToRelease<IMetaDataImport> trMDImport;
            IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

            std::string name;
            std::list<std::string> args;
            AddGenericArgs(pType, args);
            if (SUCCEEDED(NameForToken(TokenFromRid(typeDef, mdtTypeDef), trMDImport, name, false, &args)))
            {
                static const std::string_view nullablePattern = "System.Nullable<";
                if (name.rfind(nullablePattern, 0) == 0)
                {
                    ss << name.substr(nullablePattern.size(), name.rfind('>') - nullablePattern.size()) << "?";
                }
                else
                {
                    ss << name;
                }
            }
        }
        elementType = ss.str();
        return S_OK;
    }
    break;
    case ELEMENT_TYPE_VOID:
        elementType = "void";
        break;
    case ELEMENT_TYPE_BOOLEAN:
        elementType = "bool";
        break;
    case ELEMENT_TYPE_CHAR:
        elementType = "char";
        break;
    case ELEMENT_TYPE_I1:
        elementType = "sbyte";
        break;
    case ELEMENT_TYPE_U1:
        elementType = "byte";
        break;
    case ELEMENT_TYPE_I2:
        elementType = "short";
        break;
    case ELEMENT_TYPE_U2:
        elementType = "ushort";
        break;
    case ELEMENT_TYPE_I4:
        elementType = "int";
        break;
    case ELEMENT_TYPE_U4:
        elementType = "uint";
        break;
    case ELEMENT_TYPE_I8:
        elementType = "long";
        break;
    case ELEMENT_TYPE_U8:
        elementType = "ulong";
        break;
    case ELEMENT_TYPE_R4:
        elementType = "float";
        break;
    case ELEMENT_TYPE_R8:
        elementType = "double";
        break;
    case ELEMENT_TYPE_OBJECT:
        elementType = "object";
        break;
    case ELEMENT_TYPE_STRING:
        elementType = "string";
        break;
    case ELEMENT_TYPE_I:
        elementType = "IntPtr";
        break;
    case ELEMENT_TYPE_U:
        elementType = "UIntPtr";
        break;
    case ELEMENT_TYPE_SZARRAY:
    case ELEMENT_TYPE_ARRAY:
    case ELEMENT_TYPE_BYREF:
    case ELEMENT_TYPE_PTR:
    {
        std::string subElementType;
        std::string subArrayType;
        ToRelease<ICorDebugType> trFirstParameter;
        if (SUCCEEDED(pType->GetFirstTypeParameter(&trFirstParameter)))
        {
            GetTypeOfValue(trFirstParameter, subElementType, subArrayType);
        }
        else
        {
            subElementType = "<unknown>";
        }

        elementType = subElementType;

        switch (corElemType)
        {
        case ELEMENT_TYPE_SZARRAY:
            arrayType = "[]" + subArrayType;
            return S_OK;
        case ELEMENT_TYPE_ARRAY:
        {
            std::ostringstream ss;
            uint32_t rank = 0;
            pType->GetRank(&rank);
            ss << "[";
            for (uint32_t i = 0; i < rank - 1; i++)
            {
                ss << ",";
            }
            ss << "]";
            arrayType = ss.str() + subArrayType;
        }
            return S_OK;
        case ELEMENT_TYPE_BYREF:
            arrayType = subArrayType; // + "&";
            return S_OK;
        case ELEMENT_TYPE_PTR:
            arrayType = subArrayType + "*";
            return S_OK;
        default:
            // note we can never reach here as this is a nested switch
            // and corElemType can only be one of the values above
            break;
        }
    }
    break;
    case ELEMENT_TYPE_FNPTR:
        elementType = "*(...)";
        break;
    case ELEMENT_TYPE_TYPEDBYREF:
        elementType = "typedbyref";
        break;
    }
    return S_OK;
}

HRESULT GetTypeOfValue(ICorDebugType *pType, std::string &output)
{
    HRESULT Status = S_OK;
    std::string elementType;
    std::string arrayType;
    IfFailRet(GetTypeOfValue(pType, elementType, arrayType));
    output = elementType + arrayType;
    return S_OK;
}

HRESULT GetTypeAndMethod(ICorDebugFrame *pFrame, std::string &typeName, std::string &methodName)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(pFrame->GetFunction(&trFunction));

    ToRelease<ICorDebugClass> trClass;
    IfFailRet(trFunction->GetClass(&trClass));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunction->GetModule(&trModule));
    mdMethodDef methodDef = mdMethodDefNil;
    IfFailRet(trFunction->GetToken(&methodDef));

    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    mdTypeDef typeDef = mdTypeDefNil;
    IfFailRet(trClass->GetToken(&typeDef));

    ToRelease<IMetaDataImport2> trMDImport2;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport2, reinterpret_cast<void **>(&trMDImport2)));

    ULONG nameLen = 0;
    IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &nameLen,
                                         nullptr, nullptr, nullptr, nullptr, nullptr));

    mdTypeDef memTypeDef = mdTypeDefNil;
    WSTRING szFunctionName(nameLen - 1, '\0'); // nameLen - string size + null terminated symbol
    IfFailRet(trMDImport->GetMethodProps(methodDef, &memTypeDef, szFunctionName.data(), nameLen,
                                         nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));

    std::string funcName = to_utf8(szFunctionName.c_str());

    ULONG methodGenericsCount = 0;
    HCORENUM hEnum = nullptr;
    mdGenericParam gp = mdGenericParamNil;
    ULONG fetched = 0;
    while (SUCCEEDED(trMDImport2->EnumGenericParams(&hEnum, methodDef, &gp, 1, &fetched)) && fetched == 1)
    {
        methodGenericsCount++;
    }
    trMDImport2->CloseEnum(hEnum);

    if (methodGenericsCount > 0)
    {
        std::ostringstream ss;
        ss << funcName << '`' << methodGenericsCount;
        funcName = ss.str();
    }

    std::list<std::string> args;
    AddGenericArgs(pFrame, args);

    if (memTypeDef != mdTypeDefNil)
    {
        if (FAILED(NameForTypeDef(memTypeDef, trMDImport, typeName, &args)))
        {
            typeName = "";
        }
    }

    methodName = ConsumeGenericArgs(funcName, args);

    return S_OK;
}

HRESULT GetMethodName(ICorDebugFrame *pFrame, std::string &output)
{
    HRESULT Status = S_OK;

    std::string typeName;
    std::string methodName;

    std::ostringstream ss;
    IfFailRet(GetTypeAndMethod(pFrame, typeName, methodName));
    if (!typeName.empty())
    {
        ss << typeName << ".";
    }
    ss << methodName << "(";

    auto addMethodParameters = [&]() -> HRESULT
    {
        ToRelease<ICorDebugFunction> trFunction;
        IfFailRet(pFrame->GetFunction(&trFunction));

        ToRelease<ICorDebugModule> trModule;
        IfFailRet(trFunction->GetModule(&trModule));

        ToRelease<IUnknown> trUnknown;
        IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

        mdMethodDef methodDef = mdMethodDefNil;
        IfFailRet(trFunction->GetToken(&methodDef));

        ToRelease<ICorDebugILFrame> trILFrame;
        IfFailRet(pFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));

        ULONG cArguments = 0;
        ToRelease<ICorDebugValueEnum> trArgumentEnum;
        IfFailRet(trILFrame->EnumerateArguments(&trArgumentEnum));
        IfFailRet(trArgumentEnum->GetCount(&cArguments));

        DWORD methodAttr = 0;
        PCCOR_SIGNATURE pSig = nullptr;
        ULONG cbSig = 0;
        IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, nullptr,
                                             &methodAttr, &pSig, &cbSig, nullptr, nullptr));

        SigElementType returnElementType;
        std::vector<SigElementType> argElementTypes;
        const std::vector<SigElementType> typeGenerics; // TODO fill this vector
        const std::vector<SigElementType> methodGenerics; // TODO fill this vector
        // Ignore failed return code here, we need all we could parse from sig.
        ParseMethodSig(trMDImport, pSig, pSig + cbSig, returnElementType, argElementTypes, true);

        const ULONG i_start = (methodAttr & mdStatic) == 0 ? 1 : 0;
        for (ULONG i = i_start; i < cArguments; i++)
        {
            // https://docs.microsoft.com/en-us/dotnet/framework/unmanaged-api/metadata/imetadataimport-getparamformethodindex-method
            // The ordinal position in the parameter list where the requested parameter occurs. Parameters are numbered starting from one, with the method's return value in position zero.
            // Note, IMetaDataImport::GetParamForMethodIndex() don't include "this", but ICorDebugILFrame::GetArgument() do. This is why we have different logic here.
            const ULONG idx = ((methodAttr & mdStatic) == 0) ? i : (i + 1);
            mdParamDef paramDef = mdParamDefNil;
            ULONG paramNameLen = 0;
            if (FAILED(trMDImport->GetParamForMethodIndex(methodDef, idx, &paramDef)) ||
                FAILED(trMDImport->GetParamProps(paramDef, nullptr, nullptr, nullptr, 0,
                                                 &paramNameLen, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            WSTRING wParamName(paramNameLen - 1, '\0'); // paramNameLen - string size + null terminated symbol
            if (FAILED(trMDImport->GetParamProps(paramDef, nullptr, nullptr, wParamName.data(), paramNameLen,
                                                 nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            if (i != i_start)
            {
                    ss << ", ";
            }

            std::string valueType;
            ToRelease<ICorDebugValue> trValue;
            if (argElementTypes.size() > i && !argElementTypes[i].typeName.empty()) // FIXME care about typeGenerics and methodGenerics
            {
                ss << argElementTypes[i].typeName << " ";
            }
            else if (SUCCEEDED(trILFrame->GetArgument(i, &trValue)) &&
                     SUCCEEDED(GetTypeOfValue(trValue, valueType)))
            {
                ss << valueType << " ";
            }
            // else
            //    in case of fail, ignore parameter type, print only parameter name

            ss << to_utf8(wParamName.c_str());
        }
        return S_OK;
    };
    addMethodParameters();

    ss << ")";
    output = ss.str();
    return S_OK;
}

} // namespace dncdbg::TypePrinter
