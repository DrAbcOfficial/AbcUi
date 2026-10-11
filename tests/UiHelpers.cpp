#include "framework/TextUtil.h"
#include <abc/Format.h>
#include <cstdlib>
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << "\n"; std::abort(); } } while(0)
int main() {
    using namespace framework::TextUtil;
    CHECK(trimNewlines("").empty()); CHECK(trimNewlines("\n\n").empty());
    CHECK(trimNewlines("\n\nhello\n") == "hello"); CHECK(trimNewlines("a\nb") == "a\nb");
    std::string key,value; CHECK(parseTranslationLine(" key = value \r",key,value)); CHECK(key=="key" && value=="value");
    CHECK(!parseTranslationLine(" # comment",key,value)); CHECK(!parseTranslationLine(" = value",key,value));
    CHECK(escapeRml("<&>")=="&lt;&amp;&gt;");
    auto longName=std::string(4096,'x'); CHECK(abc::Format("{0}/{1}",longName,"a")==longName+"/a");
}
