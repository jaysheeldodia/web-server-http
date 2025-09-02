#include "../../include/handlers/json_handler.h"
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cmath>

std::string JsonValue::to_string() const {
    switch (type) {
        case NULL_TYPE:
            return "null";
        case BOOL_TYPE:
            return bool_value ? "true" : "false";
        case NUMBER_TYPE: {
            std::ostringstream oss;
            // Check if it's an integer
            if (std::floor(number_value) == number_value) {
                oss << static_cast<long long>(number_value);
            } else {
                oss << std::fixed << std::setprecision(6) << number_value;
                // Remove trailing zeros
                std::string result = oss.str();
                result.erase(result.find_last_not_of('0') + 1, std::string::npos);
                result.erase(result.find_last_not_of('.') + 1, std::string::npos);
                return result;
            }
            return oss.str();
        }
        case STRING_TYPE:
            return "\"" + JsonHandler::escape_string(string_value) + "\"";
        case ARRAY_TYPE: {
            std::ostringstream oss;
            oss << "[";
            for (size_t i = 0; i < array_value.size(); ++i) {
                if (i > 0) oss << ",";
                oss << array_value[i]->to_string();
            }
            oss << "]";
            return oss.str();
        }
        case OBJECT_TYPE: {
            std::ostringstream oss;
            oss << "{";
            bool first = true;
            for (const auto& pair : object_value) {
                if (!first) oss << ",";
                oss << "\"" << JsonHandler::escape_string(pair.first) << "\":" << pair.second->to_string();
                first = false;
            }
            oss << "}";
            return oss.str();
        }
    }
    return "null";
}

std::shared_ptr<JsonValue> JsonHandler::parse(const std::string& json_str) {
    size_t pos = 0;
    skip_whitespace(json_str, pos);
    return parse_value(json_str, pos);
}

std::shared_ptr<JsonValue> JsonHandler::parse_value(const std::string& json, size_t& pos) {
    skip_whitespace(json, pos);
    
    if (pos >= json.length()) {
        return std::make_shared<JsonValue>();
    }
    
    char c = json[pos];
    
    if (c == '{') {
        return parse_object(json, pos);
    } else if (c == '[') {
        return parse_array(json, pos);
    } else if (c == '"') {
        return parse_string(json, pos);
    } else if (c == '-' || std::isdigit(c)) {
        return parse_number(json, pos);
    } else if (c == 't' || c == 'f' || c == 'n') {
        return parse_literal(json, pos);
    }
    
    return std::make_shared<JsonValue>();
}

std::shared_ptr<JsonValue> JsonHandler::parse_object(const std::string& json, size_t& pos) {
    auto obj = std::make_shared<JsonValue>();
    obj->make_object();
    
    if (pos >= json.length() || json[pos] != '{') {
        return obj;
    }
    
    ++pos; // Skip '{'
    skip_whitespace(json, pos);
    
    if (pos < json.length() && json[pos] == '}') {
        ++pos; // Skip '}'
        return obj;
    }
    
    while (pos < json.length()) {
        skip_whitespace(json, pos);
        
        // Parse key
        if (pos >= json.length() || json[pos] != '"') {
            break;
        }
        
        auto key_value = parse_string(json, pos);
        if (!key_value || !key_value->is_string()) {
            break;
        }
        
        std::string key = key_value->as_string();
        
        skip_whitespace(json, pos);
        
        // Expect ':'
        if (pos >= json.length() || json[pos] != ':') {
            break;
        }
        ++pos; // Skip ':'
        
        // Parse value
        auto value = parse_value(json, pos);
        obj->set_object_item(key, value);
        
        skip_whitespace(json, pos);
        
        if (pos >= json.length()) {
            break;
        }
        
        if (json[pos] == '}') {
            ++pos; // Skip '}'
            break;
        } else if (json[pos] == ',') {
            ++pos; // Skip ','
        } else {
            break;
        }
    }
    
    return obj;
}

std::shared_ptr<JsonValue> JsonHandler::parse_array(const std::string& json, size_t& pos) {
    auto arr = std::make_shared<JsonValue>();
    arr->make_array();
    
    if (pos >= json.length() || json[pos] != '[') {
        return arr;
    }
    
    ++pos; // Skip '['
    skip_whitespace(json, pos);
    
    if (pos < json.length() && json[pos] == ']') {
        ++pos; // Skip ']'
        return arr;
    }
    
    while (pos < json.length()) {
        auto value = parse_value(json, pos);
        arr->add_to_array(value);
        
        skip_whitespace(json, pos);
        
        if (pos >= json.length()) {
            break;
        }
        
        if (json[pos] == ']') {
            ++pos; // Skip ']'
            break;
        } else if (json[pos] == ',') {
            ++pos; // Skip ','
        } else {
            break;
        }
    }
    
    return arr;
}

std::shared_ptr<JsonValue> JsonHandler::parse_string(const std::string& json, size_t& pos) {
    if (pos >= json.length() || json[pos] != '"') {
        return std::make_shared<JsonValue>();
    }
    
    ++pos; // Skip opening '"'
    std::string result;
    
    while (pos < json.length() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.length()) {
            ++pos; // Skip backslash
            char escaped = json[pos];
            switch (escaped) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                default: result += escaped; break;
            }
        } else {
            result += json[pos];
        }
        ++pos;
    }
    
    if (pos < json.length() && json[pos] == '"') {
        ++pos; // Skip closing '"'
    }
    
    return std::make_shared<JsonValue>(result);
}

std::shared_ptr<JsonValue> JsonHandler::parse_number(const std::string& json, size_t& pos) {
    size_t start = pos;
    
    if (pos < json.length() && json[pos] == '-') {
        ++pos;
    }
    
    while (pos < json.length() && std::isdigit(json[pos])) {
        ++pos;
    }
    
    if (pos < json.length() && json[pos] == '.') {
        ++pos;
        while (pos < json.length() && std::isdigit(json[pos])) {
            ++pos;
        }
    }
    
    if (pos < json.length() && (json[pos] == 'e' || json[pos] == 'E')) {
        ++pos;
        if (pos < json.length() && (json[pos] == '+' || json[pos] == '-')) {
            ++pos;
        }
        while (pos < json.length() && std::isdigit(json[pos])) {
            ++pos;
        }
    }
    
    std::string number_str = json.substr(start, pos - start);
    double value = std::stod(number_str);
    return std::make_shared<JsonValue>(value);
}

std::shared_ptr<JsonValue> JsonHandler::parse_literal(const std::string& json, size_t& pos) {
    if (pos + 4 <= json.length() && json.substr(pos, 4) == "true") {
        pos += 4;
        return std::make_shared<JsonValue>(true);
    } else if (pos + 5 <= json.length() && json.substr(pos, 5) == "false") {
        pos += 5;
        return std::make_shared<JsonValue>(false);
    } else if (pos + 4 <= json.length() && json.substr(pos, 4) == "null") {
        pos += 4;
        return std::make_shared<JsonValue>();
    }
    
    return std::make_shared<JsonValue>();
}

void JsonHandler::skip_whitespace(const std::string& json, size_t& pos) {
    while (pos < json.length() && std::isspace(json[pos])) {
        ++pos;
    }
}

std::string JsonHandler::escape_string(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

std::string JsonHandler::build_success_response(const std::string& message, std::shared_ptr<JsonValue> data) {
    auto response = std::make_shared<JsonValue>();
    response->make_object();
    response->set_object_item("success", std::make_shared<JsonValue>(true));
    response->set_object_item("message", std::make_shared<JsonValue>(message));
    
    if (data) {
        response->set_object_item("data", data);
    }
    
    return response->to_string();
}

std::string JsonHandler::build_error_response(const std::string& message, int error_code) {
    auto response = std::make_shared<JsonValue>();
    response->make_object();
    response->set_object_item("success", std::make_shared<JsonValue>(false));
    response->set_object_item("error", std::make_shared<JsonValue>(message));
    response->set_object_item("code", std::make_shared<JsonValue>(error_code));
    
    return response->to_string();
}

std::string JsonHandler::build_api_response(std::shared_ptr<JsonValue> data) {
    if (!data) {
        return build_error_response("No data provided", 500);
    }
    return data->to_string();
}

std::string JsonHandler::build_user_response(int id, const std::string& name, const std::string& email) {
    auto user = std::make_shared<JsonValue>();
    user->make_object();
    user->set_object_item("id", std::make_shared<JsonValue>(id));
    user->set_object_item("name", std::make_shared<JsonValue>(name));
    user->set_object_item("email", std::make_shared<JsonValue>(email));
    
    return build_success_response("User data retrieved", user);
}

std::string JsonHandler::build_users_list_response(const std::vector<std::map<std::string, std::string>>& users) {
    auto users_array = std::make_shared<JsonValue>();
    users_array->make_array();
    
    for (const auto& user_data : users) {
        auto user = std::make_shared<JsonValue>();
        user->make_object();
        
        for (const auto& field : user_data) {
            user->set_object_item(field.first, std::make_shared<JsonValue>(field.second));
        }
        
        users_array->add_to_array(user);
    }
    
    return build_success_response("Users list retrieved", users_array);
}