#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "astbuilder.h"
#include "compiler.h"
#include "lang.h"
#include "lexer.h"
#include "parser.h"

namespace {

// FIXME: Ensure the commands and classes below are threadsafe such that the
// tests can be run in parralel.
class TmpFile {
 public:
  TmpFile() : path_(tmpnam(nullptr)) {}
  ~TmpFile() { std::filesystem::remove(path_); }

  std::string_view getPath() const { return path_; }
  std::string getContents() const {
    if (std::filesystem::exists(path_)) {
      std::stringstream ss;
      ss << std::ifstream(path_).rdbuf();
      return ss.str();
    }
    return "";
  }

 private:
  std::string path_;
};

#if defined(__linux__)
int RunCommand(std::string_view cmd, std::string *out = nullptr,
               std::string *err = nullptr, std::string *in_text = nullptr) {
  std::string sys_cmd;
  if (in_text)
    sys_cmd.append("echo -n '").append(*in_text).append("' | ");
  sys_cmd.append(cmd);

  TmpFile out_file, err_file;
  if (out)
    sys_cmd.append(" >").append(out_file.getPath());
  if (err)
    sys_cmd.append(" 2>").append(err_file.getPath());

  int res = system(sys_cmd.c_str());
  if (out)
    *out = out_file.getContents();
  if (err)
    *err = err_file.getContents();

  if (WIFEXITED(res))
    return WEXITSTATUS(res);
  return res;
}

void BuildAndCheckOutput(lang::Module &mod, std::string_view expected,
                         std::string *in_text = nullptr) {
  constexpr std::string_view kOut = "a.out";
  TmpFile obj_file;
  lang::ASTBuilder builder;
  lang::Lower(mod, builder);
  ASSERT_TRUE(lang::Compile(mod, obj_file.getPath(), lang::File));
  ASSERT_EQ(RunCommand(std::format("clang {} -o {}", obj_file.getPath(), kOut)),
            0);

  std::string out;
  ASSERT_EQ(
      RunCommand(std::format("./{}", kOut), &out, /*err=*/nullptr, in_text), 0);
  ASSERT_EQ(out, expected);
}
#else
#error "Unhanlded OS"
#endif

void BuildAndCheckOutput(std::istream &input, std::string_view expected,
                         std::string *in_text = nullptr) {
  lang::Lexer lexer(input);
  lang::Parser parser(lexer);
  auto maybe_mod = parser.Parse();
  ASSERT_FALSE(maybe_mod.hasError()) << maybe_mod.getError();
  BuildAndCheckOutput(**maybe_mod, expected, in_text);
}

void BuildAndCheckOutput(std::string_view code, std::string_view expected,
                         std::string *in_text = nullptr) {
  std::stringstream input;
  input << code;
  BuildAndCheckOutput(input, expected, in_text);
}

TEST(E2E, HelloWorld) {
  constexpr char kHelloWorldStr[] =
      "def main = \\IO io -> IO\n"
      "  call write io \"Hello world\\n\" end";
  BuildAndCheckOutput(kHelloWorldStr, "Hello world\n");
}

class LangCompilerE2E : public testing::Test {
 public:
  static constexpr std::string_view kLangCompilerName = "./lang.out";

 protected:
  void SetUp() override {
    // The second stage compiler creates an object file called obj.obj. We will
    // use the second stage to create this, so let's delete it first.
    std::filesystem::remove("obj.obj");

    ASSERT_EQ(RunCommand("./lang " EXAMPLES_DIR "/compiler.lang"), 0);
    ASSERT_EQ(RunCommand(std::format(
                  "clang " EXAMPLES_DIR "/compiler.lang.obj $(llvm-config "
                  "--ldflags --system-libs --libs core) -o {}",
                  kLangCompilerName)),
              0);
  }
};

// Test building hello-world.lang with the compiler in the language.
TEST_F(LangCompilerE2E, HelloWorld) {
  std::ifstream t(EXAMPLES_DIR "/hello-world.lang");
  std::stringstream buffer;
  buffer << t.rdbuf();
  std::string in(buffer.str());

  // This creates an object file called obj.obj.
  ASSERT_EQ(
      RunCommand(kLangCompilerName, /*out=*/nullptr, /*err=*/nullptr, &in), 0);
  ASSERT_EQ(RunCommand("clang obj.obj -o hello-world.out"), 0);
  std::string out;

  // TODO: Right now, the executable return value is meaningless, so ignore it
  // for now, but we should check it in the future.
  RunCommand("./hello-world.out", &out);
  ASSERT_EQ(out, "Hello world\n");
}

TEST(E2E, HelloWorldLet) {
  std::ifstream input(EXAMPLES_DIR "/hello-world-let.lang");
  BuildAndCheckOutput(input, "Hello world\n");
}

TEST(E2E, WriteInt) {
  constexpr char kWriteIntStr[] =
      "def main = \\IO io -> IO\n"
      "  call write io 5 end";
  BuildAndCheckOutput(kWriteIntStr, "5");
}

TEST(E2E, If) {
  constexpr char kIfTrueStr[] =
      "def main = \\IO io -> IO\n"
      "  let x = int if true 5 else 6\n"
      "  call write io x end";
  BuildAndCheckOutput(kIfTrueStr, "5");

  constexpr char kIfFalseStr[] =
      "def main = \\IO io -> IO\n"
      "  let x = int if false 5 else 6\n"
      "  call write io x end";
  BuildAndCheckOutput(kIfFalseStr, "6");
}

TEST(E2E, Fib) {
  constexpr char kFibStr[] =
      "decl fib = \\int -> int              \n"
      "def fib = \\int n -> int             \n"
      "  if LT n 2                          \n"
      "    n                                \n"
      "  else                               \n"
      "    let x = int call fib SUB n 1 end \n"
      "    let y = int call fib SUB n 2 end \n"
      "    ADD x y                          \n"
      "def main = \\IO io -> IO             \n"
      "  let x = int call fib 10 end        \n"
      "  call write io x end";

  // fib(10) = 55
  BuildAndCheckOutput(kFibStr, "55");
}

TEST(E2E, FibNonRecurse) {
  std::ifstream input(EXAMPLES_DIR "/fib-non-recurse.lang");
  BuildAndCheckOutput(input, "55");
}

TEST(E2E, ProjectEuler1) {
  std::ifstream input(EXAMPLES_DIR "/project-euler-1.lang");
  BuildAndCheckOutput(input, "233168");
}

TEST(E2E, FuncAsArgument) {
  std::ifstream input(EXAMPLES_DIR "/func-as-argument.lang");
  BuildAndCheckOutput(input, "11");
}

TEST(E2E, Readc) {
  constexpr char kReadc[] =
      "\
def main = \\IO io -> IO \
  let x = <IO int> call readc io end \
  let io2 = IO GET IO x 0 \
  let c = int GET int x 1 \
  call write io2 CAST char c end";
  std::string in("a");
  BuildAndCheckOutput(kReadc, "a", &in);
}

TEST(E2E, ReadAllStdin) {
  constexpr char kReadc[] =
      "\
  decl echo = \\IO -> IO \
  def echo = \\IO io -> IO \
    let x = <IO int> call readc io end \
    let io2 = IO GET IO x 0 \
    let c = int GET int x 1 \
    if LT c 0 \
      io2 \
    else \
      let io3 = IO call write io2 CAST char c end \
      call echo io3 end \
  def main = \\IO io -> IO \
    call echo io end";
  std::string in(kReadc);
  BuildAndCheckOutput(kReadc, kReadc, &in);
}

TEST(E2E, Buffer) {
  std::ifstream input(EXAMPLES_DIR "/buffer.lang");
  constexpr char kExpected[] =
      "abc\n"
      "xyz\n"
      "123\n";
  BuildAndCheckOutput(input, kExpected);
}

TEST(E2E, PrintChars) {
  std::ifstream input(EXAMPLES_DIR "/composite-type.lang");
  constexpr char kExpected[] =
      "97 a\n"
      "98 b\n"
      "99 c\n"
      "100 d\n"
      "101 e\n"
      "102 f\n"
      "103 g\n"
      "104 h\n"
      "105 i\n"
      "106 j\n"
      "107 k\n"
      "108 l\n"
      "109 m\n"
      "110 n\n"
      "111 o\n"
      "112 p\n"
      "113 q\n"
      "114 r\n"
      "115 s\n"
      "116 t\n"
      "117 u\n"
      "118 v\n"
      "119 w\n"
      "120 x\n"
      "121 y\n"
      "122 z\n";
  BuildAndCheckOutput(input, kExpected);
}

TEST(E2E, CharsArrayStr) {
  std::ifstream input(EXAMPLES_DIR "/char-array-str.lang");
  BuildAndCheckOutput(input, "abc\n");
}

TEST(E2E, ReadSingleToken) {
  std::ifstream t(EXAMPLES_DIR "/hello-world.lang");
  std::stringstream buffer;
  buffer << t.rdbuf();
  std::string in(buffer.str());

  std::ifstream input(EXAMPLES_DIR "/read-token.lang");
  BuildAndCheckOutput(input, "def\n", &in);
}

TEST(E2E, ReadAllTokens) {
  std::ifstream t(EXAMPLES_DIR "/hello-world.lang");
  std::stringstream buffer;
  buffer << t.rdbuf();
  std::string in(buffer.str());

  std::ifstream input(EXAMPLES_DIR "/read-all-tokens.lang");
  constexpr char kExpected[] =
      "def\n"
      "main\n"
      "=\n"
      "\\IO\n"
      "io\n"
      "->\n"
      "IO\n"
      "call\n"
      "write\n"
      "io\n"
      "\"Hello world\\n\"\n"
      "end\n";
  BuildAndCheckOutput(input, kExpected, &in);
}

TEST(E2E, HelloWorldWithComments) {
  std::ifstream input(EXAMPLES_DIR "/hello-world-comments.lang");
  BuildAndCheckOutput(input, "Hello world\n");
}

TEST(RegressionTests, Regression1) {
  // Assert we can compile this normally. Prior, this would hang infinitely in
  // doRAU when replacing a value with itself.
  std::ifstream input(EXAMPLES_DIR "/regression-test-1.lang");
  lang::Lexer lexer(input);
  lang::Parser parser(lexer);
  auto maybe_mod = parser.Parse();
  ASSERT_FALSE(maybe_mod.hasError()) << maybe_mod.getError();
  TmpFile obj_file;
  lang::Module &mod = **maybe_mod;
  lang::ASTBuilder builder;
  lang::Lower(mod, builder);
  ASSERT_TRUE(lang::Compile(mod, obj_file.getPath(), lang::File));
}

TEST(E2E, TypeDeduction) {
  std::ifstream input(EXAMPLES_DIR "/type-deduction.lang");
  BuildAndCheckOutput(input, "abc\n123\n");
}

TEST(E2E, Metaprogramming) {
  std::ifstream input(EXAMPLES_DIR "/metaprogramming.lang");
  BuildAndCheckOutput(input, "abc\n123\ndef456\n");
}

TEST(E2E, AltCallSyntax) {
  std::ifstream input(EXAMPLES_DIR "/alt-call-syntax.lang");
  BuildAndCheckOutput(input, "result is: 55\nabc\n123\ndef456\n");
}

TEST(E2E, VariadicArgs) {
  std::ifstream input(EXAMPLES_DIR "/variadic-args.lang");
  constexpr char kExpected[] =
      "abc\n"
      "123\n"
      "def456789xyz\n";
  BuildAndCheckOutput(input, kExpected);
}

TEST(E2E, VariadicArgsOldCallSyntax) {
  std::ifstream input(EXAMPLES_DIR "/variadic-args-old-call-syntax.lang");
  constexpr char kExpected[] =
      "abc\n"
      "123\n"
      "def456789xyz\n";
  BuildAndCheckOutput(input, kExpected);
}

}  // namespace
