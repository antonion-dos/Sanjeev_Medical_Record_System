#pragma once

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <stdexcept>
#include <cstdint>
#include <cctype>
#include <type_traits>

namespace Sanjeev {

class JsonValue {
public:
    enum Type { TYPE_NULL, TYPE_BOOL, TYPE_NUMBER, TYPE_STRING, TYPE_ARRAY, TYPE_OBJECT };

    Type type = TYPE_NULL;
    bool bool_val = false;
    double num_val = 0.0;
    int64_t int_val = 0;
    std::string str_val;
    std::vector<JsonValue> arr_val;
    std::map<std::string, JsonValue> obj_val;

    JsonValue() : type(TYPE_NULL) {}
    JsonValue(bool b) : type(TYPE_BOOL), bool_val(b) {}
    template <typename T, typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value, int>::type = 0>
    JsonValue(T val) : type(TYPE_NUMBER), num_val(static_cast<double>(val)), int_val(static_cast<int64_t>(val)) {}
    JsonValue(double d) : type(TYPE_NUMBER), num_val(d), int_val(static_cast<int64_t>(d)) {}
    JsonValue(const char* s) : type(TYPE_STRING), str_val(s) {}
    JsonValue(const std::string& s) : type(TYPE_STRING), str_val(s) {}
    JsonValue(const std::vector<JsonValue>& arr) : type(TYPE_ARRAY), arr_val(arr) {}
    JsonValue(const std::map<std::string, JsonValue>& obj) : type(TYPE_OBJECT), obj_val(obj) {}

    static JsonValue array() {
        JsonValue v;
        v.type = TYPE_ARRAY;
        return v;
    }

    static JsonValue object() {
        JsonValue v;
        v.type = TYPE_OBJECT;
        return v;
    }

    void push_back(const JsonValue& v) {
        if (type != TYPE_ARRAY) type = TYPE_ARRAY;
        arr_val.push_back(v);
    }

    JsonValue& operator[](const std::string& key) {
        if (type != TYPE_OBJECT) type = TYPE_OBJECT;
        return obj_val[key];
    }

    const JsonValue& operator[](const std::string& key) const {
        static const JsonValue null_v;
        if (type != TYPE_OBJECT) return null_v;
        auto it = obj_val.find(key);
        if (it != obj_val.end()) return it->second;
        return null_v;
    }

    bool has_key(const std::string& key) const {
        if (type != TYPE_OBJECT) return false;
        return obj_val.find(key) != obj_val.end();
    }

    std::string as_string(const std::string& def = "") const {
        if (type == TYPE_STRING) return str_val;
        if (type == TYPE_NUMBER) return std::to_string(int_val);
        if (type == TYPE_BOOL) return bool_val ? "true" : "false";
        return def;
    }

    int64_t as_int(int64_t def = 0) const {
        if (type == TYPE_NUMBER) return int_val;
        if (type == TYPE_STRING) {
            try { return std::stoll(str_val); } catch (...) { return def; }
        }
        return def;
    }

    uint64_t as_uint64(uint64_t def = 0) const {
        if (type == TYPE_NUMBER) return static_cast<uint64_t>(int_val);
        if (type == TYPE_STRING) {
            try { return std::stoull(str_val); } catch (...) { return def; }
        }
        return def;
    }

    bool as_bool(bool def = false) const {
        if (type == TYPE_BOOL) return bool_val;
        return def;
    }

    static std::string escape_string(const std::string& s) {
        std::ostringstream o;
        for (char c : s) {
            if (c == '"') o << "\\\"";
            else if (c == '\\') o << "\\\\";
            else if (c == '\b') o << "\\b";
            else if (c == '\f') o << "\\f";
            else if (c == '\n') o << "\\n";
            else if (c == '\r') o << "\\r";
            else if (c == '\t') o << "\\t";
            else if (static_cast<unsigned char>(c) <= 0x1f) {
                o << "\\u00" << (c < 16 ? "0" : "") << std::hex << (int)(unsigned char)c;
            } else {
                o << c;
            }
        }
        return o.str();
    }

    std::string dump() const {
        std::ostringstream ss;
        serialize(ss);
        return ss.str();
    }

    std::string to_string() const {
        return dump();
    }

    void serialize(std::ostream& os) const {
        switch (type) {
            case TYPE_NULL: os << "null"; break;
            case TYPE_BOOL: os << (bool_val ? "true" : "false"); break;
            case TYPE_NUMBER:
                if (num_val == static_cast<double>(int_val)) os << int_val;
                else os << num_val;
                break;
            case TYPE_STRING: os << "\"" << escape_string(str_val) << "\""; break;
            case TYPE_ARRAY: {
                os << "[";
                for (size_t i = 0; i < arr_val.size(); ++i) {
                    if (i > 0) os << ",";
                    arr_val[i].serialize(os);
                }
                os << "]";
                break;
            }
            case TYPE_OBJECT: {
                os << "{";
                bool first = true;
                for (const auto& pair : obj_val) {
                    if (!first) os << ",";
                    first = false;
                    os << "\"" << escape_string(pair.first) << "\":";
                    pair.second.serialize(os);
                }
                os << "}";
                break;
            }
        }
    }

    static JsonValue parse(const std::string& json_str) {
        size_t idx = 0;
        skip_ws(json_str, idx);
        return parse_val(json_str, idx);
    }

private:
    static void skip_ws(const std::string& s, size_t& idx) {
        while (idx < s.size() && (s[idx] == ' ' || s[idx] == '\t' || s[idx] == '\n' || s[idx] == '\r')) {
            idx++;
        }
    }

    static JsonValue parse_val(const std::string& s, size_t& idx) {
        skip_ws(s, idx);
        if (idx >= s.size()) return JsonValue();

        char c = s[idx];
        if (c == 'n') { idx += 4; return JsonValue(); }
        if (c == 't') { idx += 4; return JsonValue(true); }
        if (c == 'f') { idx += 5; return JsonValue(false); }
        if (c == '"') return parse_str(s, idx);
        if (c == '[') return parse_arr(s, idx);
        if (c == '{') return parse_obj(s, idx);
        if (c == '-' || std::isdigit(c)) return parse_num(s, idx);

        return JsonValue();
    }

    static JsonValue parse_str(const std::string& s, size_t& idx) {
        idx++; // skip open quote
        std::string res;
        while (idx < s.size()) {
            char c = s[idx++];
            if (c == '"') return JsonValue(res);
            if (c == '\\' && idx < s.size()) {
                char esc = s[idx++];
                if (esc == '"') res += '"';
                else if (esc == '\\') res += '\\';
                else if (esc == '/') res += '/';
                else if (esc == 'b') res += '\b';
                else if (esc == 'f') res += '\f';
                else if (esc == 'n') res += '\n';
                else if (esc == 'r') res += '\r';
                else if (esc == 't') res += '\t';
                else if (esc == 'u' && idx + 4 <= s.size()) {
                    // Skip unicode hex for basic support
                    idx += 4;
                    res += '?';
                }
            } else {
                res += c;
            }
        }
        return JsonValue(res);
    }

    static JsonValue parse_num(const std::string& s, size_t& idx) {
        size_t start = idx;
        if (s[idx] == '-') idx++;
        while (idx < s.size() && std::isdigit(s[idx])) idx++;
        bool is_float = false;
        if (idx < s.size() && s[idx] == '.') {
            is_float = true;
            idx++;
            while (idx < s.size() && std::isdigit(s[idx])) idx++;
        }
        std::string num_part = s.substr(start, idx - start);
        if (is_float) {
            return JsonValue(std::stod(num_part));
        } else {
            return JsonValue(std::stoll(num_part));
        }
    }

    static JsonValue parse_arr(const std::string& s, size_t& idx) {
        idx++; // skip '['
        JsonValue val = JsonValue::array();
        skip_ws(s, idx);
        if (idx < s.size() && s[idx] == ']') { idx++; return val; }

        while (idx < s.size()) {
            val.push_back(parse_val(s, idx));
            skip_ws(s, idx);
            if (idx < s.size() && s[idx] == ',') {
                idx++;
                skip_ws(s, idx);
            } else if (idx < s.size() && s[idx] == ']') {
                idx++;
                break;
            } else {
                break;
            }
        }
        return val;
    }

    static JsonValue parse_obj(const std::string& s, size_t& idx) {
        idx++; // skip '{'
        JsonValue val = JsonValue::object();
        skip_ws(s, idx);
        if (idx < s.size() && s[idx] == '}') { idx++; return val; }

        while (idx < s.size()) {
            skip_ws(s, idx);
            if (s[idx] != '"') break;
            JsonValue key = parse_str(s, idx);
            skip_ws(s, idx);
            if (idx < s.size() && s[idx] == ':') idx++;
            skip_ws(s, idx);
            val[key.str_val] = parse_val(s, idx);
            skip_ws(s, idx);
            if (idx < s.size() && s[idx] == ',') {
                idx++;
                skip_ws(s, idx);
            } else if (idx < s.size() && s[idx] == '}') {
                idx++;
                break;
            } else {
                break;
            }
        }
        return val;
    }
};

} // namespace Sanjeev
