#ifndef JSON_HANDLER_H
#define JSON_HANDLER_H

#include <string>
#include <map>
#include <vector>
#include <memory>

// Simple JSON value representation
class JsonValue {
public:
    enum Type {
        NULL_TYPE,
        BOOL_TYPE,
        NUMBER_TYPE,
        STRING_TYPE,
        ARRAY_TYPE,
        OBJECT_TYPE
    };

private:
    Type type;
    std::string string_value;
    double number_value;
    bool bool_value;
    std::vector<std::shared_ptr<JsonValue>> array_value;
    std::map<std::string, std::shared_ptr<JsonValue>> object_value;

public:
    // Constructors
    JsonValue() : type(NULL_TYPE), number_value(0.0), bool_value(false) {}
    JsonValue(bool value) : type(BOOL_TYPE), number_value(0.0), bool_value(value) {}
    JsonValue(double value) : type(NUMBER_TYPE), number_value(value), bool_value(false) {}
    JsonValue(int value) : type(NUMBER_TYPE), number_value(static_cast<double>(value)), bool_value(false) {}
    JsonValue(const std::string& value) : type(STRING_TYPE), string_value(value), number_value(0.0), bool_value(false) {}
    JsonValue(const char* value) : type(STRING_TYPE), string_value(value), number_value(0.0), bool_value(false) {}

    // Type checking
    Type get_type() const { return type; }
    bool is_null() const { return type == NULL_TYPE; }
    bool is_bool() const { return type == BOOL_TYPE; }
    bool is_number() const { return type == NUMBER_TYPE; }
    bool is_string() const { return type == STRING_TYPE; }
    bool is_array() const { return type == ARRAY_TYPE; }
    bool is_object() const { return type == OBJECT_TYPE; }

    // Value getters
    bool as_bool() const { return bool_value; }
    double as_number() const { return number_value; }
    int as_int() const { return static_cast<int>(number_value); }
    const std::string& as_string() const { return string_value; }

    // Array operations
    void make_array() { type = ARRAY_TYPE; array_value.clear(); }
    void add_to_array(std::shared_ptr<JsonValue> value) { 
        if (type != ARRAY_TYPE) make_array();
        array_value.push_back(value); 
    }
    size_t array_size() const { return array_value.size(); }
    std::shared_ptr<JsonValue> get_array_item(size_t index) const {
        if (index < array_value.size()) return array_value[index];
        return std::make_shared<JsonValue>();
    }

    // Object operations
    void make_object() { type = OBJECT_TYPE; object_value.clear(); }
    void set_object_item(const std::string& key, std::shared_ptr<JsonValue> value) {
        if (type != OBJECT_TYPE) make_object();
        object_value[key] = value;
    }
    std::shared_ptr<JsonValue> get_object_item(const std::string& key) const {
        auto it = object_value.find(key);
        if (it != object_value.end()) return it->second;
        return std::make_shared<JsonValue>();
    }
    bool has_key(const std::string& key) const {
        return object_value.find(key) != object_value.end();
    }
    std::vector<std::string> get_object_keys() const {
        std::vector<std::string> keys;
        for (const auto& pair : object_value) {
            keys.push_back(pair.first);
        }
        return keys;
    }

    // Serialization
    std::string to_string() const;
};

class JsonHandler {
public:
    // Parse JSON string into JsonValue
    static std::shared_ptr<JsonValue> parse(const std::string& json_str);
    
    // Build JSON responses
    static std::string build_success_response(const std::string& message, std::shared_ptr<JsonValue> data = nullptr);
    static std::string build_error_response(const std::string& message, int error_code = 400);
    static std::string build_api_response(std::shared_ptr<JsonValue> data);
    
    // Helper functions for common responses
    static std::string build_user_response(int id, const std::string& name, const std::string& email);
    static std::string build_users_list_response(const std::vector<std::map<std::string, std::string>>& users);
    static std::string escape_string(const std::string& str);

private:
    // Parsing helpers
    static std::shared_ptr<JsonValue> parse_value(const std::string& json, size_t& pos);
    static std::shared_ptr<JsonValue> parse_object(const std::string& json, size_t& pos);
    static std::shared_ptr<JsonValue> parse_array(const std::string& json, size_t& pos);
    static std::shared_ptr<JsonValue> parse_string(const std::string& json, size_t& pos);
    static std::shared_ptr<JsonValue> parse_number(const std::string& json, size_t& pos);
    static std::shared_ptr<JsonValue> parse_literal(const std::string& json, size_t& pos);
    
    // Utility functions
    static void skip_whitespace(const std::string& json, size_t& pos);
    static std::string unescape_string(const std::string& str);
};

#endif // JSON_HANDLER_H