#include <string>

#include <gtest/gtest.h>

#include "MotionStudio/common/Expected.h"

using motion::Expected;
using motion::Unexpected;

namespace {

enum class ErrorCode {
    ParseFailed,
    NotFound
};

Expected<int, std::string> DivideByTwo(int value) {
    if (value == 0) {
        return Unexpected(std::string("division by zero"));
    }
    return value / 2;
}

std::string NumberToText(int value) {
    return std::to_string(value);
}

void ConsumeNumber(int) {
}

Expected<int, std::string> RecoverWithZero(const std::string &) {
    return 0;
}

ErrorCode ClassifyError(const std::string &) {
    return ErrorCode::ParseFailed;
}

Expected<int, std::string> ProduceSeven() {
    return 7;
}

Expected<void, std::string> RecoverVoid(const std::string &) {
    return {};
}

std::string ConstantText() {
    return "ok";
}

void DoNothing() {
}

}  // namespace

TEST(ExpectedTest, ValueConstructionHoldsValue) {
    Expected<int, std::string> result(42);
    EXPECT_TRUE(result.hasValue());
    EXPECT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(result.value(), 42);
}

TEST(ExpectedTest, ErrorConstructionHoldsError) {
    Expected<int, std::string> result(Unexpected(std::string("boom")));
    EXPECT_FALSE(result.hasValue());
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_EQ(result.error(), "boom");
}

TEST(ExpectedTest, DereferenceOperatorsExposeValue) {
    Expected<std::string, std::string> result(std::string("text"));
    EXPECT_EQ(*result, "text");
    EXPECT_EQ(result->size(), 4u);
}

TEST(ExpectedTest, ValueOrReturnsValueWhenPresent) {
    Expected<int, std::string> result(42);
    EXPECT_EQ(result.valueOr(7), 42);
}

TEST(ExpectedTest, ValueOrReturnsFallbackOnError) {
    Expected<int, std::string> result(Unexpected(std::string("boom")));
    EXPECT_EQ(result.valueOr(7), 7);
}

TEST(ExpectedTest, CustomErrorTypeIsSupported) {
    Unexpected<ErrorCode> unexpected(ErrorCode::NotFound);
    Expected<int, ErrorCode> result(unexpected);
    EXPECT_FALSE(result.hasValue());
    EXPECT_EQ(result.error(), ErrorCode::NotFound);
}

TEST(ExpectedTest, ValueAndErrorMayShareTheSameType) {
    Expected<std::string, std::string> value(std::string("ok"));
    ASSERT_TRUE(value.hasValue());
    EXPECT_EQ(value.value(), "ok");

    Expected<std::string, std::string> error(Unexpected(std::string("boom")));
    ASSERT_FALSE(error.hasValue());
    EXPECT_EQ(error.error(), "boom");
}

TEST(ExpectedTest, VoidDefaultIsSuccess) {
    Expected<void, std::string> result;
    EXPECT_TRUE(result.hasValue());
}

TEST(ExpectedTest, VoidUnexpectedHoldsError) {
    Expected<void, std::string> result(Unexpected(std::string("boom")));
    EXPECT_FALSE(result.hasValue());
    EXPECT_EQ(result.error(), "boom");
}

TEST(ExpectedTest, AndThenChainsOnValue) {
    Expected<int, std::string> result = Expected<int, std::string>(84).andThen(DivideByTwo).andThen(DivideByTwo);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), 21);
}

TEST(ExpectedTest, AndThenPropagatesError) {
    Expected<int, std::string> result = Expected<int, std::string>(Unexpected(std::string("boom"))).andThen(DivideByTwo);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error(), "boom");
}

TEST(ExpectedTest, TransformMapsValue) {
    Expected<std::string, std::string> result = Expected<int, std::string>(21).transform(NumberToText);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "21");
}

TEST(ExpectedTest, TransformPropagatesError) {
    Expected<std::string, std::string> result =
        Expected<int, std::string>(Unexpected(std::string("boom"))).transform(NumberToText);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error(), "boom");
}

TEST(ExpectedTest, TransformToVoidDropsValue) {
    Expected<void, std::string> result = Expected<int, std::string>(21).transform(ConsumeNumber);
    EXPECT_TRUE(result.hasValue());
}

TEST(ExpectedTest, OrElseRecoversFromError) {
    Expected<int, std::string> result = Expected<int, std::string>(Unexpected(std::string("boom"))).orElse(RecoverWithZero);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), 0);
}

TEST(ExpectedTest, OrElsePassesValueThrough) {
    Expected<int, std::string> result = Expected<int, std::string>(42).orElse(RecoverWithZero);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), 42);
}

TEST(ExpectedTest, TransformErrorMapsError) {
    Expected<int, ErrorCode> result = Expected<int, std::string>(Unexpected(std::string("boom"))).transformError(ClassifyError);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error(), ErrorCode::ParseFailed);
}

TEST(ExpectedTest, TransformErrorPassesValueThrough) {
    Expected<int, ErrorCode> result = Expected<int, std::string>(42).transformError(ClassifyError);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), 42);
}

TEST(ExpectedTest, VoidAndThenRunsOnSuccess) {
    Expected<int, std::string> result = Expected<void, std::string>().andThen(ProduceSeven);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), 7);
}

TEST(ExpectedTest, VoidAndThenPropagatesError) {
    Expected<int, std::string> result = Expected<void, std::string>(Unexpected(std::string("boom"))).andThen(ProduceSeven);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error(), "boom");
}

TEST(ExpectedTest, VoidTransformMapsToValue) {
    Expected<std::string, std::string> result = Expected<void, std::string>().transform(ConstantText);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "ok");
}

TEST(ExpectedTest, VoidTransformStaysVoid) {
    Expected<void, std::string> result = Expected<void, std::string>().transform(DoNothing);
    EXPECT_TRUE(result.hasValue());
}

TEST(ExpectedTest, VoidOrElseRecoversFromError) {
    Expected<void, std::string> result = Expected<void, std::string>(Unexpected(std::string("boom"))).orElse(RecoverVoid);
    EXPECT_TRUE(result.hasValue());
}

TEST(ExpectedTest, VoidTransformErrorMapsError) {
    Expected<void, ErrorCode> result = Expected<void, std::string>(Unexpected(std::string("boom"))).transformError(ClassifyError);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error(), ErrorCode::ParseFailed);
}

TEST(ExpectedTest, ValueInErrorStateDies) {
    Expected<int, std::string> result(Unexpected(std::string("boom")));
    EXPECT_DEATH({ result.value(); }, "error state");
}

TEST(ExpectedTest, ErrorInValueStateDies) {
    Expected<int, std::string> result(42);
    EXPECT_DEATH({ result.error(); }, "value state");
}

TEST(ExpectedTest, VoidErrorInValueStateDies) {
    Expected<void, std::string> result;
    EXPECT_DEATH({ result.error(); }, "value state");
}
