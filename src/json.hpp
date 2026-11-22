#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace minijson {

// 仅供本项目配置文件使用的最小 JSON 实现：支持对象/数组/字符串/数字/bool/null
class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string str;
    std::vector<Value> array;
    std::map<std::string, Value> object;

    bool isObject() const { return type == Type::Object; }
    bool has(const std::string& key) const {
        return type == Type::Object && object.count(key) != 0;
    }
    const Value& get(const std::string& key) const;
    std::string asString(const std::string& fallback = "") const;
    double asNumber(double fallback = 0.0) const;
    bool asBool(bool fallback = false) const;
};

Value parse(const std::string& text, std::string* error = nullptr);
std::string escape(const std::string& raw);

}  // namespace minijson
