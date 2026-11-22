#pragma once
#include <map>
#include <string>
#include <vector>

namespace tcpv {

// Minimal JSON value + parser, enough for manifests and review files.
struct Json {
  enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
  bool boolean = false;
  double num = 0.0;
  std::string str;
  std::vector<Json> arr;
  std::map<std::string, Json> obj;

  const Json* find(const std::string& key) const {
    if (type != Obj) return nullptr;
    auto it = obj.find(key);
    return it == obj.end() ? nullptr : &it->second;
  }
  std::string getStr(const std::string& key,
                     const std::string& dflt = "") const {
    const Json* v = find(key);
    return (v && v->type == Str) ? v->str : dflt;
  }
  double getNum(const std::string& key, double dflt = 0.0) const {
    const Json* v = find(key);
    return (v && v->type == Num) ? v->num : dflt;
  }
};

bool parseJson(const std::string& text, Json& out, std::string& err);

} // namespace tcpv
