// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
#ifdef FILE_PREVIEW_STANDALONE
#include <windows.h>
#include <cassert>
#include <iostream>
#define BEGIN_TEST_CLASS(...)
#define TEST_CLASS_PROPERTY(...)
#define END_TEST_CLASS()
#define TEST_METHOD(name) public: void name()
#define VERIFY_ARE_EQUAL(expected, actual) assert((expected) == (actual))
#define VERIFY_IS_TRUE(value) assert(value)
#define VERIFY_IS_FALSE(value) assert(!(value))
#else
#include "pch.h"
#endif
#include "../TerminalApp/FilePreviewReader.h"
#include <filesystem>

#ifndef FILE_PREVIEW_STANDALONE
using namespace WEX::TestExecution;
#endif
using namespace Microsoft::Terminal::FilePreview;

namespace ControlUnitTests
{
    struct PackageFixture
    {
        winrt::com_ptr<IOpcFactory> factory;
        winrt::com_ptr<IOpcPackage> package;
        std::wstring path;
        PackageFixture()
        {
            winrt::init_apartment();
            winrt::check_hresult(CoCreateInstance(__uuidof(OpcFactory), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IOpcFactory), factory.put_void()));
            winrt::check_hresult(factory->CreatePackage(package.put()));
            wchar_t directory[MAX_PATH]{}, file[MAX_PATH]{};
            if (!GetTempPathW(MAX_PATH, directory) || !GetTempFileNameW(directory, L"wfp", 0, file)) winrt::throw_last_error();
            path = file;
        }
        ~PackageFixture()
        {
            package = nullptr;
            factory = nullptr;
            DeleteFileW(path.c_str());
            winrt::uninit_apartment();
        }
        void Add(const wchar_t* name, const std::string& xml)
        {
            winrt::com_ptr<IOpcPartSet> parts;
            winrt::check_hresult(package->GetPartSet(parts.put()));
            winrt::com_ptr<IOpcPartUri> uri;
            winrt::check_hresult(factory->CreatePartUri(name, uri.put()));
            winrt::com_ptr<IOpcPart> part;
            winrt::check_hresult(parts->CreatePart(uri.get(), L"application/xml", OPC_COMPRESSION_NORMAL, part.put()));
            winrt::com_ptr<IStream> stream;
            winrt::check_hresult(part->GetContentStream(stream.put()));
            winrt::check_hresult(stream->Write(xml.data(), static_cast<ULONG>(xml.size()), nullptr));
        }
        void Save()
        {
            winrt::com_ptr<IStream> stream;
            winrt::check_hresult(factory->CreateStreamOnFile(path.c_str(), OPC_STREAM_IO_WRITE, nullptr, FILE_ATTRIBUTE_NORMAL, stream.put()));
            winrt::check_hresult(factory->WritePackageToStream(package.get(), OPC_WRITE_DEFAULT, stream.get()));
        }
    };

    class FilePreviewTests
    {
        BEGIN_TEST_CLASS(FilePreviewTests)
            TEST_CLASS_PROPERTY(L"ThreadingModel", L"MTA")
        END_TEST_CLASS()
        TEST_METHOD(TextEncodings);
        TEST_METHOD(BoundedUtf8);
        TEST_METHOD(OfficeParagraphs);
        TEST_METHOD(OfficeSheets);
        TEST_METHOD(RejectDtd);
    };

    void FilePreviewTests::TextEncodings()
    {
        VERIFY_ARE_EQUAL(std::wstring{ L"hello\nworld" }, DecodeText("hello\nworld", false).value());
        VERIFY_ARE_EQUAL(std::wstring{ L"\u00e9" }, DecodeText("\xef\xbb\xbf\xc3\xa9", false).value());
        VERIFY_ARE_EQUAL(std::wstring{ L"A" }, DecodeText(std::string{ "\xff\xfe\x41\0", 4 }, false).value());
        VERIFY_ARE_EQUAL(std::wstring{ L"A" }, DecodeText(std::string{ "\xfe\xff\0\x41", 4 }, false).value());
        VERIFY_IS_FALSE(DecodeText(std::string{ "abc\0def", 7 }, false).has_value());
        VERIFY_IS_FALSE(DecodeText("\xff invalid UTF-8", false).has_value());
        VERIFY_IS_FALSE(DecodeText(std::string{ "\xff\xfe\0\xd8", 4 }, false).has_value());
        VERIFY_IS_TRUE(DecodeText("", false).has_value());
    }

    void FilePreviewTests::BoundedUtf8()
    {
        VERIFY_IS_TRUE(DecodeText("\xc3\xa9", true)->starts_with(L"\u00e9\n"));
        VERIFY_IS_TRUE(DecodeText("a\xe2\x82", true)->starts_with(L"a\n[Preview truncated"));
        VERIFY_IS_FALSE(DecodeText("a\xe2\x82", false).has_value());
    }

    void FilePreviewTests::OfficeParagraphs()
    {
        PackageFixture file;
        file.Add(L"/word/document.xml", R"(<w:document xmlns:w="urn:word"><w:body><w:p><w:r><w:t>Hello</w:t></w:r></w:p><w:p><w:r><w:t>World</w:t></w:r></w:p></w:body></w:document>)");
        file.Save();
        const auto result = ReadOfficeSections(file.path, L".docx");
        VERIFY_ARE_EQUAL(size_t{ 1 }, result.size());
        VERIFY_ARE_EQUAL(std::wstring{ L"Hello\nWorld\n" }, result[0].second);
    }

    void FilePreviewTests::OfficeSheets()
    {
        PackageFixture file;
        file.Add(L"/xl/workbook.xml", R"(<workbook xmlns:r="urn:rel"><sheets><sheet name="Totals" r:id="s1"/></sheets></workbook>)");
        {
            winrt::com_ptr<IOpcPartSet> parts;
            winrt::check_hresult(file.package->GetPartSet(parts.put()));
            winrt::com_ptr<IOpcPartUri> uri;
            winrt::check_hresult(file.factory->CreatePartUri(L"/xl/workbook.xml", uri.put()));
            winrt::com_ptr<IOpcPart> part;
            winrt::check_hresult(parts->GetPart(uri.get(), part.put()));
            winrt::com_ptr<IOpcRelationshipSet> rels;
            winrt::check_hresult(part->GetRelationshipSet(rels.put()));
            winrt::com_ptr<IUri> target;
            winrt::check_hresult(CreateUri(L"worksheets/sheet1.xml", Uri_CREATE_ALLOW_RELATIVE, 0, target.put()));
            winrt::com_ptr<IOpcRelationship> relationship;
            winrt::check_hresult(rels->CreateRelationship(L"s1", L"urn:sheet", target.get(), OPC_URI_TARGET_MODE_INTERNAL, relationship.put()));
        }
        file.Add(L"/xl/sharedStrings.xml", R"(<sst><si><t>Revenue</t></si></sst>)");
        file.Add(L"/xl/worksheets/sheet1.xml", R"(<worksheet><sheetData><row r="1"><c r="A1" t="s"><v>0</v></c><c r="C1"><f>1+2</f><v>3</v></c></row><row r="201"><c r="A201"><v>99</v></c></row></sheetData></worksheet>)");
        file.Save();
        const auto result = ReadOfficeSections(file.path, L".xlsx");
        VERIFY_ARE_EQUAL(std::wstring{ L"Totals" }, result[0].first);
        VERIFY_IS_TRUE(result[0].second.starts_with(L"Revenue\t\t3\t\n"));
        VERIFY_IS_TRUE(result[0].second.find(L"Preview limited") != std::wstring::npos);
        VERIFY_IS_TRUE(result[0].second.find(L"99") == std::wstring::npos);
    }

    void FilePreviewTests::RejectDtd()
    {
        PackageFixture file;
        file.Add(L"/word/document.xml", R"(<!DOCTYPE document [<!ENTITY local SYSTEM "file:///C:/Windows/win.ini">]><document><t>&local;</t></document>)");
        file.Save();
        bool rejected = false;
        try { ReadOfficeSections(file.path, L".docx"); }
        catch (const winrt::hresult_error&) { rejected = true; }
        VERIFY_IS_TRUE(rejected);
    }
}

#ifdef FILE_PREVIEW_STANDALONE
int main()
{
    try
    {
        ControlUnitTests::FilePreviewTests tests;
        tests.TextEncodings();
        tests.BoundedUtf8();
        tests.OfficeParagraphs();
        tests.OfficeSheets();
        tests.RejectDtd();
        std::cout << "All 5 native file preview tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#endif
