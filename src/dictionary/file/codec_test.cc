// Copyright 2010-2020, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "dictionary/file/codec.h"

#include <memory>

#include "base/file_stream.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/util.h"
#include "dictionary/file/codec_factory.h"
#include "dictionary/file/codec_interface.h"
#include "dictionary/file/section.h"
#include "testing/base/public/googletest.h"
#include "testing/base/public/gunit.h"
#include "absl/memory/memory.h"

namespace mozc {
namespace dictionary {
namespace {

class CodecTest : public ::testing::Test {
 public:
  CodecTest() : test_file_(FLAGS_test_tmpdir + "testfile.txt") {}

 protected:
  void SetUp() override {
    DictionaryFileCodecFactory::SetCodec(nullptr);
    FileUtil::Unlink(test_file_);
  }

  void TearDown() override {
    // Reset to default setting
    DictionaryFileCodecFactory::SetCodec(nullptr);
    FileUtil::Unlink(test_file_);
  }

  void AddSection(const DictionaryFileCodecInterface *codec,
                  const std::string &name, const char *ptr, int len,
                  std::vector<DictionaryFileSection> *sections) const {
    CHECK(codec);
    CHECK(sections);
    sections->push_back(
        DictionaryFileSection(ptr, len, codec->GetSectionName(name)));
  }

  bool FindSection(const DictionaryFileCodecInterface *codec,
                   const std::vector<DictionaryFileSection> &sections,
                   const std::string &name, int *index) const {
    CHECK(codec);
    CHECK(index);
    const std::string name_find = codec->GetSectionName(name);
    for (size_t i = 0; i < sections.size(); ++i) {
      if (sections[i].name == name_find) {
        *index = i;
        return true;
      }
    }
    return false;
  }

  bool CheckValue(const DictionaryFileSection &section,
                  const std::string &expected) const {
    const std::string value(section.ptr, section.len);
    return (expected == value);
  }

  const std::string test_file_;
};

class CodecMock : public DictionaryFileCodecInterface {
 public:
  void WriteSections(const std::vector<DictionaryFileSection> &sections,
                     std::ostream *ofs) const override {
    const std::string value = "dummy value";
    ofs->write(value.data(), value.size());
  }

  mozc::Status ReadSections(
      const char *image, int length,
      std::vector<DictionaryFileSection> *sections) const override {
    sections->emplace_back(nullptr, 0, "dummy name");
    return mozc::Status();
  }

  std::string GetSectionName(const std::string &name) const override {
    return "dummy section name";
  }
};

TEST_F(CodecTest, FactoryTest) {
  auto codec_mock = absl::make_unique<CodecMock>();
  DictionaryFileCodecFactory::SetCodec(codec_mock.get());
  const DictionaryFileCodecInterface *codec =
      DictionaryFileCodecFactory::GetCodec();
  EXPECT_TRUE(codec != nullptr);
  std::vector<DictionaryFileSection> sections;
  {
    OutputFileStream ofs;
    ofs.open(test_file_.c_str(), std::ios_base::out | std::ios_base::binary);
    codec->WriteSections(sections, &ofs);
  }
  {
    EXPECT_TRUE(FileUtil::FileExists(test_file_));
    InputFileStream ifs;
    ifs.open(test_file_.c_str(), std::ios_base::in | std::ios_base::binary);
    ifs.seekg(0, std::ios::end);
    const int len = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    char buf[64];
    ifs.read(buf, len);
    EXPECT_EQ("dummy value", std::string(buf, len));
  }
  {
    EXPECT_EQ(0, sections.size());
    EXPECT_TRUE(codec->ReadSections(nullptr, 0, &sections).ok());
    EXPECT_EQ(1, sections.size());
    EXPECT_EQ("dummy name", sections[0].name);
  }
  { EXPECT_EQ("dummy section name", codec->GetSectionName("test")); }
}

TEST_F(CodecTest, DefaultTest) {
  const DictionaryFileCodecInterface *codec =
      DictionaryFileCodecFactory::GetCodec();
  EXPECT_TRUE(codec != nullptr);
  {
    std::vector<DictionaryFileSection> write_sections;
    const std::string value0 = "Value 0 test";
    AddSection(codec, "Section 0", value0.data(), value0.size(),
               &write_sections);
    const std::string value1 = "Value 1 test test";
    AddSection(codec, "Section 1", value1.data(), value1.size(),
               &write_sections);
    OutputFileStream ofs;
    ofs.open(test_file_.c_str(), std::ios_base::out | std::ios_base::binary);
    codec->WriteSections(write_sections, &ofs);
  }
  std::string buf;  // sections will reference this buffer.
  std::vector<DictionaryFileSection> sections;
  ASSERT_TRUE(FileUtil::FileExists(test_file_));
  buf = InputFileStream(test_file_.c_str(),
                        std::ios_base::in | std::ios_base::binary)
            .Read();
  ASSERT_TRUE(codec->ReadSections(buf.data(), buf.size(), &sections).ok());
  ASSERT_EQ(2, sections.size());
  int index = -1;
  ASSERT_TRUE(FindSection(codec, sections, "Section 0", &index));
  ASSERT_EQ(0, index);
  EXPECT_TRUE(CheckValue(sections[index], "Value 0 test"));

  ASSERT_TRUE(FindSection(codec, sections, "Section 1", &index));
  ASSERT_EQ(1, index);
  EXPECT_TRUE(CheckValue(sections[index], "Value 1 test test"));
}

TEST_F(CodecTest, RandomizedCodecTest) {
  auto internal_codec = absl::make_unique<DictionaryFileCodec>();
  DictionaryFileCodecFactory::SetCodec(internal_codec.get());
  const DictionaryFileCodecInterface *codec =
      DictionaryFileCodecFactory::GetCodec();
  EXPECT_TRUE(codec != nullptr);
  {
    std::vector<DictionaryFileSection> write_sections;
    const std::string value0 = "Value 0 test";
    AddSection(codec, "Section 0", value0.data(), value0.size(),
               &write_sections);
    const std::string value1 = "Value 1 test test";
    AddSection(codec, "Section 1", value1.data(), value1.size(),
               &write_sections);
    OutputFileStream ofs;
    ofs.open(test_file_.c_str(), std::ios_base::out | std::ios_base::binary);
    codec->WriteSections(write_sections, &ofs);
  }
  std::string buf;  // sections will reference this buffer.
  std::vector<DictionaryFileSection> sections;
  ASSERT_TRUE(FileUtil::FileExists(test_file_));
  buf = InputFileStream(test_file_.c_str(),
                        std::ios_base::in | std::ios_base::binary)
            .Read();
  ASSERT_TRUE(codec->ReadSections(buf.data(), buf.size(), &sections).ok());
  ASSERT_EQ(2, sections.size());
  int index = -1;
  ASSERT_TRUE(FindSection(codec, sections, "Section 0", &index));
  ASSERT_EQ(0, index);
  EXPECT_TRUE(CheckValue(sections[index], "Value 0 test"));

  ASSERT_TRUE(FindSection(codec, sections, "Section 1", &index));
  ASSERT_EQ(1, index);
  EXPECT_TRUE(CheckValue(sections[index], "Value 1 test test"));
}

}  // namespace
}  // namespace dictionary
}  // namespace mozc
