#include <string>
#include <vector>

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

double NumberToDouble(int value) {
    return value * 2.0;
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

int ConstantNumber() {
    return 42;
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
    Expected<std::vector<int>, std::string> result(std::vector<int>{1, 2, 3, 4});
    EXPECT_EQ((*result).size(), 4u);
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
    Expected<double, std::string> result = Expected<int, std::string>(21).transform(NumberToDouble);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), 42.0);
}

TEST(ExpectedTest, TransformPropagatesError) {
    Expected<double, std::string> result =
        Expected<int, std::string>(Unexpected(std::string("boom"))).transform(NumberToDouble);
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
    Expected<int, std::string> result = Expected<void, std::string>().transform(ConstantNumber);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), 42);
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
