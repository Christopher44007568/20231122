#include "json.h"
#include <cctype>

namespace tcpv {
namespace {

struct P {
  const char* s;
  size_t pos = 0;
  std::string* err;

  void ws() {
    while (s[pos] && std::isspace((unsigned char)s[pos])) pos++;
  }
  bool fail(const std::string& m) {
    *err = m + " at offset " + std::to_string(pos);
    return false;
  }
  bool parse(Json& out);
  bool parseStr(std::string& out) {
    if (s[pos] != '"') return fail("expected string");
    pos++;
    out.clear();
    while (s[pos] && s[pos] != '"') {
      char c = s[pos++];
      if (c == '\\') {
        char e = s[pos++];
        switch (e) {
          case 'n': out += '\n'; break;
          case 't': out += '\t'; break;
          case 'r': out += '\r'; break;
          case 'u': pos += 4; out += '?'; break;
          default: out += e;
        }
      } else {
        out += c;
      }
    }
    if (s[pos] != '"') return fail("unterminated string");
    pos++;
    return true;
  }
};

bool P::parse(Json& out) {
  ws();
  char c = s[pos];
  if (c == '{') {
    out.type = Json::Obj;
    pos++;
    ws();
    if (s[pos] == '}') { pos++; return true; }
    while (true) {
      ws();
      std::string key;
      if (!parseStr(key)) return false;
      ws();
      if (s[pos] != ':') return fail("expected ':'");
      pos++;
      Json v;
      if (!parse(v)) return false;
      out.obj[key] = std::move(v);
      ws();
      if (s[pos] == ',') { pos++; continue; }
      if (s[pos] == '}') { pos++; return true; }
      return fail("expected ',' or '}'");
    }
  }
  if (c == '[') {
    out.type = Json::Arr;
    pos++;
    ws();
    if (s[pos] == ']') { pos++; return true; }
    while (true) {
      Json v;
      if (!parse(v)) return false;
      out.arr.push_back(std::move(v));
      ws();
      if (s[pos] == ',') { pos++; continue; }
      if (s[pos] == ']') { pos++; return true; }
      return fail("expected ',' or ']'");
    }
  }
  if (c == '"') {
    out.type = Json::Str;
    return parseStr(out.str);
  }
  if (c == 't' && std::string(s + pos, 4) == "true") {
    out.type = Json::Bool; out.boolean = true; pos += 4; return true;
  }
  if (c == 'f' && std::string(s + pos, 5) == "false") {
    out.type = Json::Bool; out.boolean = false; pos += 5; return true;
  }
  if (c == 'n' && std::string(s + pos, 4) == "null") {
    out.type = Json::Null; pos += 4; return true;
  }
  // number
  char* end = nullptr;
  double v = std::strtod(s + pos, &end);
  if (end == s + pos) return fail("expected value");
  pos = size_t(end - s);
  out.type = Json::Num;
  out.num = v;
  return true;
}

} // namespace

bool parseJson(const std::string& text, Json& out, std::string& err) {
  P p{text.c_str(), 0, &err};
  if (!p.parse(out)) return false;
  p.ws();
  if (text[p.pos] != 0) return p.fail("trailing data");
  return true;
}

} // namespace tcpv
