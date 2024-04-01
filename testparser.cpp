#include <gtest/gtest.h>

#include <fstream>
#include <istream>

#include "ast.h"
#include "astbuilder.h"
#include "parser.h"

namespace {

TEST(Parser, GenericRemainingArgumentTypesMatch) {
  lang::ASTBuilder builder;
  const auto &callable_ty = builder.getCallableType(
      builder.getIOType(), {&builder.getIOType(), &builder.getGenericType()});
  std::vector<const lang::Type *> args{&builder.getIOType(),
                                       &builder.getGenericRemainingType()};
  ASSERT_TRUE(callable_ty.ArgumentTypesMatch(args));

  // This should also match because we don't know at parse time if the remaining
  // args can fit enough for this callable until we lower it.
  const auto &callable_ty2 = builder.getCallableType(
      builder.getIOType(), {&builder.getIOType(), &builder.getGenericType(),
                            &builder.getGenericRemainingType()});
  ASSERT_TRUE(callable_ty2.ArgumentTypesMatch(args));
}

TEST(Errors, ShowLineInError) {
  constexpr char kMissingType[] =
      "def main = \\IO io -> IO\n"
      "  let i = 2\n"
      "  io";
  constexpr std::string_view kExpectedError =
      "2:11: Expected a type; instead found `2`\n"
      "  let i = 2\n"
      "          ^";
  std::stringstream input;
  input << kMissingType;
  lang::Lexer lexer(input);
  lang::Parser parser(lexer);
  auto res = parser.Parse();
  ASSERT_TRUE(res.hasError());
  ASSERT_EQ(res.getError(), kExpectedError);
}

TEST(Errors, ReturnTypeMismatch) {
  constexpr char kMismatch[] = "def main = \\IO io -> IO 2\n";
  constexpr std::string_view kExpectedError =
      "1:25: Expression type mismatch; found `int` but expected `IO`";
  std::stringstream input;
  input << kMismatch;
  lang::Lexer lexer(input);
  lang::Parser parser(lexer);
  auto res = parser.Parse();
  ASSERT_TRUE(res.hasError());
  ASSERT_TRUE(res.getError().starts_with(kExpectedError)) << res.getError();
}

TEST(Errors, MultipleCallableOverrides) {
  std::ifstream input(EXAMPLES_DIR "/multiple-callable-overrides.lang");
  constexpr std::string_view kExpectedError =
      "9:15: Callable `writeln` with type `\\IO int GENERIC -> IO` is handled "
      "by "
      "another callable\n"
      "def writeln = \\IO io int arg1 GENERIC arg2 -> IO\n"
      "              ^\n";
  lang::Lexer lexer(input);
  lang::Parser parser(lexer);
  auto res = parser.Parse();
  ASSERT_TRUE(res.hasError());
  ASSERT_TRUE(res.getError().starts_with(kExpectedError)) << res.getError();
}

TEST(Errors, VariadicMultipleCallableOverrides) {
  std::ifstream input(EXAMPLES_DIR
                      "/variadic-args-multiple-candidates-error.lang");
  constexpr std::string_view kExpectedError =
      "5:15: Callable `writeln` with type `\\IO GENERIC GENERIC_REMAINING -> "
      "IO` "
      "is handled by another callable\n";
  lang::Lexer lexer(input);
  lang::Parser parser(lexer);
  auto res = parser.Parse();
  ASSERT_TRUE(res.hasError());
  ASSERT_TRUE(res.getError().starts_with(kExpectedError)) << res.getError();
}

}  // namespace