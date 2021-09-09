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

#include "prediction/predictor.h"

#include <cstddef>
#include <memory>
#include <string>

#include "base/logging.h"
#include "base/singleton.h"
#include "base/system_util.h"
#include "composer/composer.h"
#include "config/config_handler.h"
#include "converter/segments.h"
#include "data_manager/testing/mock_data_manager.h"
#include "dictionary/dictionary_mock.h"
#include "dictionary/pos_matcher.h"
#include "dictionary/suppression_dictionary.h"
#include "prediction/predictor_interface.h"
#include "prediction/user_history_predictor.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "session/request_test_util.h"
#include "testing/base/public/gmock.h"
#include "testing/base/public/googletest.h"
#include "testing/base/public/gunit.h"
#include "absl/memory/memory.h"

namespace mozc {
namespace {

using ::mozc::dictionary::DictionaryMock;
using ::mozc::dictionary::SuppressionDictionary;
using ::testing::_;
using ::testing::AtMost;
using ::testing::Return;

class CheckCandSizePredictor : public PredictorInterface {
 public:
  explicit CheckCandSizePredictor(int expected_cand_size)
      : expected_cand_size_(expected_cand_size),
        predictor_name_("CheckCandSizePredictor") {}

  bool PredictForRequest(const ConversionRequest &request,
                         Segments *segments) const override {
    EXPECT_EQ(expected_cand_size_, segments->max_prediction_candidates_size());
    return true;
  }

  const std::string &GetPredictorName() const override {
    return predictor_name_;
  }

 private:
  const int expected_cand_size_;
  const std::string predictor_name_;
};

class NullPredictor : public PredictorInterface {
 public:
  explicit NullPredictor(bool ret)
      : return_value_(ret),
        predict_called_(false),
        predictor_name_("NullPredictor") {}

  bool PredictForRequest(const ConversionRequest &request,
                         Segments *segments) const override {
    predict_called_ = true;
    return return_value_;
  }

  bool predict_called() const { return predict_called_; }

  void Clear() { predict_called_ = false; }

  const std::string &GetPredictorName() const override {
    return predictor_name_;
  }

 private:
  bool return_value_;
  mutable bool predict_called_;
  const std::string predictor_name_;
};

class MockPredictor : public PredictorInterface {
 public:
  MockPredictor() = default;
  ~MockPredictor() override = default;
  MOCK_METHOD(bool, PredictForRequest,
              (const ConversionRequest &request, Segments *segments),
              (const, override));
  MOCK_METHOD(const std::string &, GetPredictorName, (), (const, override));
};

}  // namespace

class MobilePredictorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    config_.reset(new config::Config);
    config::ConfigHandler::GetDefaultConfig(config_.get());

    request_.reset(new commands::Request);
    commands::RequestForUnitTest::FillMobileRequest(request_.get());
    composer_.reset(
        new composer::Composer(nullptr, request_.get(), config_.get()));

    convreq_.reset(
        new ConversionRequest(composer_.get(), request_.get(), config_.get()));
  }

  std::unique_ptr<mozc::composer::Composer> composer_;
  std::unique_ptr<commands::Request> request_;
  std::unique_ptr<config::Config> config_;
  std::unique_ptr<ConversionRequest> convreq_;
};

TEST_F(MobilePredictorTest, CallPredictorsForMobileSuggestion) {
  auto predictor = absl::make_unique<MobilePredictor>(
      absl::make_unique<CheckCandSizePredictor>(20),
      absl::make_unique<CheckCandSizePredictor>(3));
  Segments segments;
  {
    segments.set_request_type(Segments::SUGGESTION);
    Segment *segment = segments.add_segment();
    CHECK(segment);
  }
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
}

TEST_F(MobilePredictorTest, CallPredictorsForMobilePartialSuggestion) {
  auto predictor = absl::make_unique<MobilePredictor>(
      absl::make_unique<CheckCandSizePredictor>(20),
      // We don't call history predictior
      absl::make_unique<CheckCandSizePredictor>(-1));
  Segments segments;
  {
    segments.set_request_type(Segments::PARTIAL_SUGGESTION);
    Segment *segment = segments.add_segment();
    CHECK(segment);
  }
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
}

TEST_F(MobilePredictorTest, CallPredictorsForMobilePrediction) {
  auto predictor = absl::make_unique<MobilePredictor>(
      absl::make_unique<CheckCandSizePredictor>(200),
      absl::make_unique<CheckCandSizePredictor>(3));
  Segments segments;
  {
    segments.set_request_type(Segments::PREDICTION);
    Segment *segment = segments.add_segment();
    CHECK(segment);
  }
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
}

TEST_F(MobilePredictorTest, CallPredictorsForMobilePartialPrediction) {
  DictionaryMock dictionary_mock;
  testing::MockDataManager data_manager;
  const dictionary::POSMatcher pos_matcher(data_manager.GetPOSMatcherData());
  auto predictor = absl::make_unique<MobilePredictor>(
      absl::make_unique<CheckCandSizePredictor>(200),
      absl::make_unique<UserHistoryPredictor>(
          &dictionary_mock, &pos_matcher,
          Singleton<SuppressionDictionary>::get(), true));
  Segments segments;
  {
    segments.set_request_type(Segments::PARTIAL_PREDICTION);
    Segment *segment = segments.add_segment();
    CHECK(segment);
  }
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
}

TEST_F(MobilePredictorTest, CallPredictForRequetMobile) {
  auto predictor1 = absl::make_unique<MockPredictor>();
  auto predictor2 = absl::make_unique<MockPredictor>();
  EXPECT_CALL(*predictor1, PredictForRequest(_, _))
      .Times(AtMost(1))
      .WillOnce(Return(true));
  EXPECT_CALL(*predictor2, PredictForRequest(_, _))
      .Times(AtMost(1))
      .WillOnce(Return(true));

  auto predictor = absl::make_unique<MobilePredictor>(std::move(predictor1),
                                                      std::move(predictor2));
  Segments segments;
  {
    segments.set_request_type(Segments::SUGGESTION);
    Segment *segment = segments.add_segment();
    CHECK(segment);
  }
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
}

class PredictorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    config_ = absl::make_unique<config::Config>();
    config::ConfigHandler::GetDefaultConfig(config_.get());

    request_ = absl::make_unique<commands::Request>();
    composer_ = absl::make_unique<composer::Composer>(nullptr, request_.get(),
                                                      config_.get());
    convreq_ = absl::make_unique<ConversionRequest>(
        composer_.get(), request_.get(), config_.get());
  }

  std::unique_ptr<mozc::composer::Composer> composer_;
  std::unique_ptr<commands::Request> request_;
  std::unique_ptr<config::Config> config_;
  std::unique_ptr<ConversionRequest> convreq_;
};

TEST_F(PredictorTest, AllPredictorsReturnTrue) {
  auto predictor = absl::make_unique<DefaultPredictor>(
      absl::make_unique<NullPredictor>(true),
      absl::make_unique<NullPredictor>(true));
  Segments segments;
  {
    segments.set_request_type(Segments::SUGGESTION);
    Segment *segment = segments.add_segment();
    CHECK(segment);
  }
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
}

TEST_F(PredictorTest, MixedReturnValue) {
  auto predictor = absl::make_unique<DefaultPredictor>(
      absl::make_unique<NullPredictor>(true),
      absl::make_unique<NullPredictor>(false));
  Segments segments;
  {
    segments.set_request_type(Segments::SUGGESTION);
    Segment *segment = segments.add_segment();
    CHECK(segment);
  }
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
}

TEST_F(PredictorTest, AllPredictorsReturnFalse) {
  auto predictor = absl::make_unique<DefaultPredictor>(
      absl::make_unique<NullPredictor>(false),
      absl::make_unique<NullPredictor>(false));
  Segments segments;
  {
    segments.set_request_type(Segments::SUGGESTION);
    Segment *segment = segments.add_segment();
    CHECK(segment);
  }
  EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
}

TEST_F(PredictorTest, CallPredictorsForSuggestion) {
  const int suggestions_size =
      config::ConfigHandler::DefaultConfig().suggestions_size();
  auto predictor = absl::make_unique<DefaultPredictor>(
      absl::make_unique<CheckCandSizePredictor>(suggestions_size),
      absl::make_unique<CheckCandSizePredictor>(suggestions_size));
  Segments segments;
  {
    segments.set_request_type(Segments::SUGGESTION);
    Segment *segment = segments.add_segment();
    CHECK(segment);
  }
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
}

TEST_F(PredictorTest, CallPredictorsForPrediction) {
  const int kPredictionSize = 100;
  auto predictor = absl::make_unique<DefaultPredictor>(
      absl::make_unique<CheckCandSizePredictor>(kPredictionSize),
      absl::make_unique<CheckCandSizePredictor>(kPredictionSize));
  Segments segments;
  {
    segments.set_request_type(Segments::PREDICTION);
    Segment *segment = segments.add_segment();
    CHECK(segment);
  }
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
}

TEST_F(PredictorTest, CallPredictForRequet) {
  auto predictor1 = absl::make_unique<MockPredictor>();
  auto predictor2 = absl::make_unique<MockPredictor>();
  EXPECT_CALL(*predictor1, PredictForRequest(_, _))
      .Times(AtMost(1))
      .WillOnce(Return(true));
  EXPECT_CALL(*predictor2, PredictForRequest(_, _))
      .Times(AtMost(1))
      .WillOnce(Return(true));

  auto predictor = absl::make_unique<DefaultPredictor>(std::move(predictor1),
                                                       std::move(predictor2));
  Segments segments;
  {
    segments.set_request_type(Segments::SUGGESTION);
    Segment *segment = segments.add_segment();
    CHECK(segment);
  }
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
}

TEST_F(PredictorTest, DisableAllSuggestion) {
  auto predictor1 = absl::make_unique<NullPredictor>(true);
  auto predictor2 = absl::make_unique<NullPredictor>(true);
  const auto *pred1 = predictor1.get();  // Keep the reference
  const auto *pred2 = predictor2.get();  // Keep the reference
  auto predictor = absl::make_unique<DefaultPredictor>(std::move(predictor1),
                                                       std::move(predictor2));
  Segments segments;
  {
    segments.set_request_type(Segments::SUGGESTION);
    Segment *segment = segments.add_segment();
    CHECK(segment);
  }

  config_->set_presentation_mode(true);
  EXPECT_FALSE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_FALSE(pred1->predict_called());
  EXPECT_FALSE(pred2->predict_called());

  config_->set_presentation_mode(false);
  EXPECT_TRUE(predictor->PredictForRequest(*convreq_, &segments));
  EXPECT_TRUE(pred1->predict_called());
  EXPECT_TRUE(pred2->predict_called());
}

}  // namespace mozc
