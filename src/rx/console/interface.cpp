#include <string.h> // strcmp
#include <stdlib.h> // strtoll, strtof
#include <errno.h> // errno, ERANGE

#include "rx/console/interface.h"
#include "rx/console/variable.h"
#include "rx/console/command.h"
#include "rx/console/parser.h"

#include "rx/core/concurrency/spin_lock.h"
#include "rx/core/concurrency/scope_lock.h"

#include "rx/core/filesystem/file.h"
#include "rx/core/map.h"

#include "rx/core/log.h" // RX_LOG

namespace rx::console {

static concurrency::spin_lock g_lock;
static variable_reference* g_head; // protected by |g_lock|

RX_LOG("console", logger);

static RX_GLOBAL_GROUP("cvars", g_group_cvars);
static RX_GLOBAL_GROUP("console", g_group_console);

// TODO(dweiler): limited line count queue for messages on the console.
static RX_GLOBAL<vector<string>> g_lines{"console", "lines", &memory::g_system_allocator};
static RX_GLOBAL<map<string, command>> g_commands{"console", "commands", &memory::g_system_allocator};

void interface::write(const string& message_) {
  g_lines->push_back(message_);
}

void interface::clear() {
  g_lines->clear();
}

const vector<string>& interface::lines() {
  return *g_lines;
}

void interface::add_command(const string& _name, const char* _signature,
  function<bool(const vector<command::argument>&)>&& _function)
{
  g_commands->insert(_name, {_name, _signature, utility::move(_function)});
}

static bool type_check(variable_type _variable_type, token::type _token_type) {
  switch (_variable_type) {
  case variable_type::k_boolean:
    return _token_type == token::type::k_boolean;
  case variable_type::k_string:
    return _token_type == token::type::k_string;
  case variable_type::k_int:
    return _token_type == token::type::k_int;
  case variable_type::k_float:
    return _token_type == token::type::k_float;
  case variable_type::k_vec4f:
    return _token_type == token::type::k_vec4f;
  case variable_type::k_vec4i:
    return _token_type == token::type::k_vec4i;
  case variable_type::k_vec3f:
    return _token_type == token::type::k_vec3f;
  case variable_type::k_vec3i:
    return _token_type == token::type::k_vec3i;
  case variable_type::k_vec2f:
    return _token_type == token::type::k_vec2f;
  case variable_type::k_vec2i:
    return _token_type == token::type::k_vec2i;
  }
  return false;
}

bool interface::execute(const string& _contents) {
  parser parse{&memory::g_system_allocator};

  if (!parse.parse(_contents)) {
    const auto& diagnostic{parse.error()};

    print("^rerror: ^w%s", diagnostic.message);
    print("%s", _contents);

    string format;
    format += string::format("%*s^r", static_cast<int>(diagnostic.offset), "");
    if (!diagnostic.inside) {
      for (rx_size i{0}; i < diagnostic.length; i++) {
        format += '~';
      }
    }

    if (diagnostic.caret) {
      format += "^^";
    }

    print("%s", format);

    return false;
  }

  auto tokens{utility::move(parse.tokens())};

  if (tokens.is_empty()) {
    return false;
  }

  if (tokens[0].kind() != token::type::k_atom) {
    print("^rerror: ^wexpected atom");
    return false;
  }

  const auto& atom{tokens[0].as_atom()};
  if (auto* variable{find_variable_by_name(atom)}) {
    if (tokens.size() > 1) {
      switch (set_from_reference_and_token(variable, tokens[1])) {
      case variable_status::k_success:
        print("^gsuccess: ^wChanged: \"%s\" to %s", atom, tokens[1].print());
        break;
      case variable_status::k_out_of_range:
        print("^rerror: ^wOut of range: \"%s\" has range %s", atom,
          variable->print_range());
        break;
      case variable_status::k_type_mismatch:
        print("^rerror: ^wType mismatch: \"%s\" expected %s, got %s", atom,
          variable_type_as_string(variable->type()),
          token_type_as_string(tokens[1].kind()));
        break;
      }
    } else {
      print("^cinfo: ^w%s = %s", atom, variable->print_value());
    }
  } else if (auto* command{g_commands->find(atom)}) {
    tokens.erase(0, 1);
    command->execute_tokens(tokens);
  } else {
    print("^rerror: ^wCommand or variable \"%s\", not found", atom);
  }

  return true;
}

vector<string> interface::auto_complete_variables(const string& _prefix) {
  vector<string> results;
  for (variable_reference* node{g_head}; node; node = node->m_next) {
    if (!strncmp(node->m_name, _prefix.data(), _prefix.size())) {
      results.push_back(node->m_name);
    }
  }
  return results;
}

vector<string> interface::auto_complete_commands(const string& _prefix) {
  vector<string> results;
  g_commands->each_key([&](const string& _key) {
    if (!strncmp(_key.data(), _prefix.data(), _prefix.size())) {
      results.push_back(_key);
    }
  });
  return results;
}

bool interface::load(const char* file_name) {
  // sort references
  {
    concurrency::scope_lock locked(g_lock);
    g_head = sort(g_head);
  }

  filesystem::file file(file_name, "r");
  if (!file) {
    return false;
  }

  logger(log::level::k_info, "loading '%s'", file_name);

  parser parse{&memory::g_system_allocator};
  for (string line_contents; file.read_line(line_contents); ) {
    string line{line_contents.lstrip(" \t")};
    if (line.is_empty() || strchr("#;[", line[0])) {
      // ignore empty and comment lines
      continue;
    }

    if (!parse.parse(line_contents)) {
      logger(log::level::k_error, "%s", parse.error().message);
    } else {
      auto tokens{utility::move(parse.tokens())};

      if (tokens.size() < 2) {
        continue;
      }

      if (tokens[0].kind() != token::type::k_atom) {
        continue;
      }

      const auto& atom{tokens[0].as_atom()};
      if (auto* variable{find_variable_by_name(atom)}) {
        set_from_reference_and_token(variable, tokens[1]);
      } else {
        logger(log::level::k_error, "'%s' not found", atom);
      }
    }
  }

  return true;
}

bool interface::save(const char* file_name) {
  filesystem::file file(file_name, "w");
  if (!file) {
    return false;
  }

  logger(log::level::k_info, "saving '%s'", file_name);
  for (const variable_reference *head{g_head}; head; head = head->m_next) {
    if (head->type() == variable_type::k_boolean) {
      const auto handle{head->cast<bool>()};
      file.print("## %s (defaults to %s)\n",
        head->description(), handle->initial() ? "true" : "false");
      file.print(handle->get() == handle->initial() ? ";%s %s\n" : "%s %s\n",
        head->name(), handle->get() ? "true" : "false");
    } else if (head->type() == variable_type::k_int) {
      const auto handle{head->cast<rx_s32>()};
      const auto min{handle->min()};
      const auto max{handle->max()};
      const auto min_fmt{min == k_int_min ? "-inf" : string::format("%d", min)};
      const auto max_fmt{max == k_int_max ? "+inf" : string::format("%d", max)};
      file.print("## %s (in range [%s, %s], defaults to %d)\n",
        head->description(), min_fmt, max_fmt, handle->initial());
      file.print(handle->get() == handle->initial() ? ";%s %d\n" : "%s %d\n",
        head->name(), handle->get());
    } else if (head->type() == variable_type::k_float) {
      const auto handle{head->cast<rx_f32>()};
      const auto min{handle->min()};
      const auto max{handle->max()};
      const auto min_fmt{min == k_float_min ? "-inf" : string::format("%f", min)};
      const auto max_fmt{max == k_float_max ? "+inf" : string::format("%f", max)};
      file.print("## %s (in range [%s, %s], defaults to %f)\n",
        head->description(), min_fmt, max_fmt, handle->initial());
      file.print(handle->get() == handle->initial() ? ";%s %f\n" : "%s %f\n",
        head->name(), handle->get());
    } else if (head->type() == variable_type::k_string) {
      const auto escape = [](const string& contents) {
        string result;
        result.reserve(contents.size() * 4);
        for (rx_size i{0}; i < contents.size(); i++) {
          switch (contents[i]) {
          case '"':
            [[fallthrough]];
          case '\\':
            result += '\\';
            [[fallthrough]];
          default:
            result += contents[i];
            break;
          }
        }
        return result;
      };
      const auto handle{head->cast<string>()};
      const auto get_escaped{escape(handle->get())};
      const auto initial_escaped{escape(handle->initial())};
      file.print("## %s (defaults to \"%s\")\n",
        head->description(), initial_escaped);
      file.print(get_escaped == initial_escaped ? ";%s \"%s\"\n" : "%s \"%s\"\n",
        head->name(), get_escaped);
    } else if (head->type() == variable_type::k_vec4f) {
      const auto handle{head->cast<vec4f>()};
      const auto min{handle->min()};
      const auto max{handle->max()};
      string min_fmt;
      string max_fmt;
      if (min.is_any(k_float_min) || max.is_any(k_float_max)) {
        const auto min_x{min.x == k_float_min ? "-inf" : string::format("%f", min.x)};
        const auto min_y{min.y == k_float_min ? "-inf" : string::format("%f", min.y)};
        const auto min_z{min.z == k_float_min ? "-inf" : string::format("%f", min.z)};
        const auto min_w{min.w == k_float_min ? "-inf" : string::format("%f", min.w)};
        const auto max_x{max.x == k_float_max ? "+inf" : string::format("%f", max.x)};
        const auto max_y{max.y == k_float_max ? "+inf" : string::format("%f", max.y)};
        const auto max_z{max.z == k_float_max ? "+inf" : string::format("%f", max.z)};
        const auto max_w{max.w == k_float_max ? "+inf" : string::format("%f", max.w)};
        min_fmt = string::format("{%s, %s, %s, %s}", min_x, min_y, min_z, min_w);
        max_fmt = string::format("{%s, %s, %s, %s}", max_x, max_y, max_z, max_w);
      } else {
        min_fmt = string::format("%s", min);
        max_fmt = string::format("%s", max);
      }
      file.print("## %s (in range [%s, %s], defaults to %s)\n",
        head->description(), min_fmt, max_fmt, handle->initial());
      file.print(handle->get() == handle->initial() ? ";%s %s\n" : "%s %s\n",
        head->name(), handle->get());
    } else if (head->type() == variable_type::k_vec4i) {
      const auto handle{head->cast<vec4i>()};
      const auto min{handle->min()};
      const auto max{handle->max()};
      string min_fmt;
      string max_fmt;
      if (min.is_any(k_int_min) || max.is_any(k_int_max)) {
        const auto min_x{min.x == k_int_min ? "-inf" : string::format("%d", min.x)};
        const auto min_y{min.y == k_int_min ? "-inf" : string::format("%d", min.y)};
        const auto min_z{min.z == k_int_min ? "-inf" : string::format("%d", min.z)};
        const auto min_w{min.w == k_int_min ? "-inf" : string::format("%d", min.w)};
        const auto max_x{max.x == k_int_max ? "+inf" : string::format("%d", max.x)};
        const auto max_y{max.y == k_int_max ? "+inf" : string::format("%d", max.y)};
        const auto max_z{max.z == k_int_max ? "+inf" : string::format("%d", max.z)};
        const auto max_w{min.w == k_int_min ? "+inf" : string::format("%d", max.w)};
        min_fmt = string::format("{%s, %s, %s, %s}", min_x, min_y, min_z, min_w);
        max_fmt = string::format("{%s, %s, %s, %s}", max_x, max_y, max_z, max_w);
      } else {
        min_fmt = string::format("%s", min);
        max_fmt = string::format("%s", max);
      }
      file.print("## %s (in range [%s, %s], defaults to %s)\n",
        head->name(), min_fmt, max_fmt, handle->initial());
      file.print(handle->get() == handle->initial() ? ";%s %s\n" : "%s %s\n",
        head->name(), handle->get());
    } else if (head->type() == variable_type::k_vec3f) {
      const auto handle{head->cast<vec3f>()};
      const auto min{handle->min()};
      const auto max{handle->max()};
      string min_fmt;
      string max_fmt;
      if (min.is_any(k_float_min) || max.is_any(k_float_max)) {
        const auto min_x{min.x == k_float_min ? "-inf" : string::format("%f", min.x)};
        const auto min_y{min.y == k_float_min ? "-inf" : string::format("%f", min.y)};
        const auto min_z{min.z == k_float_min ? "-inf" : string::format("%f", min.z)};
        const auto max_x{max.x == k_float_max ? "+inf" : string::format("%f", max.x)};
        const auto max_y{max.y == k_float_max ? "+inf" : string::format("%f", max.y)};
        const auto max_z{max.z == k_float_max ? "+inf" : string::format("%f", max.z)};
        min_fmt = string::format("{%s, %s, %s}", min_x, min_y, min_z);
        max_fmt = string::format("{%s, %s, %s}", max_x, max_y, max_z);
      } else {
        min_fmt = string::format("%s", min);
        max_fmt = string::format("%s", max);
      }
      file.print("## %s (in range [%s, %s], defaults to %s)\n",
        head->description(), min_fmt, max_fmt, handle->initial());
      file.print(handle->get() == handle->initial() ? ";%s %s\n" : "%s %s\n",
        head->name(), handle->get());
    } else if (head->type() == variable_type::k_vec3i) {
      const auto handle{head->cast<vec3i>()};
      const auto min{handle->min()};
      const auto max{handle->max()};
      string min_fmt;
      string max_fmt;
      if (min.is_any(k_int_min) || max.is_any(k_int_max)) {
        const auto min_x{min.x == k_int_min ? "-inf" : string::format("%d", min.x)};
        const auto min_y{min.y == k_int_min ? "-inf" : string::format("%d", min.y)};
        const auto min_z{min.z == k_int_min ? "-inf" : string::format("%d", min.z)};
        const auto max_x{max.x == k_int_max ? "+inf" : string::format("%d", max.x)};
        const auto max_y{max.y == k_int_max ? "+inf" : string::format("%d", max.y)};
        const auto max_z{max.z == k_int_max ? "+inf" : string::format("%d", max.z)};
        min_fmt = string::format("{%s, %s, %s}", min_x, min_y, min_z);
        max_fmt = string::format("{%s, %s, %s}", max_x, max_y, max_z);
      } else {
        min_fmt = string::format("%s", min);
        max_fmt = string::format("%s", max);
      }
      file.print("## %s (in range [%s, %s], defaults to %s)\n",
        head->name(), min_fmt, max_fmt, handle->initial());
      file.print(handle->get() == handle->initial() ? ";%s %s\n" : "%s %s\n",
        head->name(), handle->get());
    } else if (head->type() == variable_type::k_vec2f) {
      const auto handle{head->cast<vec2f>()};
      const auto min{handle->min()};
      const auto max{handle->max()};
      string min_fmt;
      string max_fmt;
      if (min.is_any(k_float_min) || max.is_any(k_float_max)) {
        const auto min_x{min.x == k_float_min ? "-inf" : string::format("%f", min.x)};
        const auto min_y{min.y == k_float_min ? "-inf" : string::format("%f", min.y)};
        const auto max_x{max.x == k_float_max ? "+inf" : string::format("%f", max.x)};
        const auto max_y{max.y == k_float_max ? "+inf" : string::format("%f", max.y)};
        min_fmt = string::format("{%s, %s}", min_x, min_y);
        max_fmt = string::format("{%s, %s}", max_x, max_y);
      } else {
        min_fmt = string::format("%s", min);
        max_fmt = string::format("%s", max);
      }
      file.print("## %s (in range [%s, %s], defaults to %s)\n",
        head->description(), min_fmt, max_fmt, handle->initial());
      file.print(handle->get() == handle->initial() ? ";%s %s\n" : "%s %s\n",
        head->name(), handle->get());
    } else if (head->type() == variable_type::k_vec2i) {
      const auto handle{head->cast<vec2i>()};
      const auto min{handle->min()};
      const auto max{handle->max()};
      string min_fmt;
      string max_fmt;
      if (min.is_any(k_int_min) || max.is_any(k_int_max)) {
        const auto min_x{min.x == k_int_min ? "-inf" : string::format("%d", min.x)};
        const auto min_y{min.y == k_int_min ? "-inf" : string::format("%d", min.y)};
        const auto max_x{max.x == k_int_max ? "+inf" : string::format("%d", max.x)};
        const auto max_y{max.y == k_int_max ? "+inf" : string::format("%d", max.y)};
        min_fmt = string::format("{%s, %s}", min_x, min_y);
        max_fmt = string::format("{%s, %s}", max_x, max_y);
      } else {
        min_fmt = string::format("%s", min);
        max_fmt = string::format("%s", max);
      }
      file.print("## %s (in range [%s, %s], defaults to %s)\n",
        head->description(), min_fmt, max_fmt, handle->initial());
      file.print(handle->get() == handle->initial() ? ";%s %s\n" : "%s %s\n",
        head->name(), handle->get());
    }
  }

  return true;
}

template<typename T>
variable_status interface::set_from_reference_and_value(variable_reference* _reference, const T& _value) {
  if (auto* cast{_reference->try_cast<T>()}) {
    return cast->set(_value);
  } else {
    return variable_status::k_type_mismatch;
  }
}

template variable_status interface::set_from_reference_and_value<bool>(variable_reference* _reference, const bool& _value);
template variable_status interface::set_from_reference_and_value<string>(variable_reference* _reference, const string& _value);
template variable_status interface::set_from_reference_and_value<rx_s32>(variable_reference* _reference, const rx_s32& _value);
template variable_status interface::set_from_reference_and_value<rx_f32>(variable_reference* _reference, const rx_f32& _value);
template variable_status interface::set_from_reference_and_value<math::vec4f>(variable_reference* _reference, const math::vec4f& _value);
template variable_status interface::set_from_reference_and_value<math::vec4i>(variable_reference* _reference, const math::vec4i& _value);
template variable_status interface::set_from_reference_and_value<math::vec3f>(variable_reference* _reference, const math::vec3f& _value);
template variable_status interface::set_from_reference_and_value<math::vec3i>(variable_reference* _reference, const math::vec3i& _value);
template variable_status interface::set_from_reference_and_value<math::vec2f>(variable_reference* _reference, const math::vec2f& _value);
template variable_status interface::set_from_reference_and_value<math::vec2i>(variable_reference* _reference, const math::vec2i& _value);

variable_status interface::set_from_reference_and_token(variable_reference* reference_, const token& _token) {
  if (!type_check(reference_->type(), _token.kind())) {
    return variable_status::k_type_mismatch;
  }

  switch (reference_->type()) {
  case variable_type::k_boolean:
    return reference_->cast<bool>()->set(_token.as_boolean());
  case variable_type::k_string:
    return reference_->cast<string>()->set(_token.as_string());
  case variable_type::k_int:
    return reference_->cast<rx_s32>()->set(_token.as_int());
  case variable_type::k_float:
    return reference_->cast<rx_f32>()->set(_token.as_float());
  case variable_type::k_vec4f:
    return reference_->cast<math::vec4f>()->set(_token.as_vec4f());
  case variable_type::k_vec4i:
    return reference_->cast<math::vec4i>()->set(_token.as_vec4i());
  case variable_type::k_vec3f:
    return reference_->cast<math::vec3f>()->set(_token.as_vec3f());
  case variable_type::k_vec3i:
    return reference_->cast<math::vec3i>()->set(_token.as_vec3i());
  case variable_type::k_vec2f:
    return reference_->cast<math::vec2f>()->set(_token.as_vec2f());
  case variable_type::k_vec2i:
    return reference_->cast<math::vec2i>()->set(_token.as_vec2i());
  }

  RX_HINT_UNREACHABLE();
}

variable_reference* interface::find_variable_by_name(const char* _name) {
  for (variable_reference* head{g_head}; head; head = head->m_next) {
    if (!strcmp(head->name(), _name)) {
      return head;
    }
  }
  return nullptr;
}

variable_reference* interface::add_variable(variable_reference* reference) {
  logger(log::level::k_info, "registered '%s'", reference->m_name);
  concurrency::scope_lock locked(g_lock);
  variable_reference* next = g_head;
  g_head = reference;
  return next;
}

variable_reference* interface::split(variable_reference* reference) {
  if (!reference || !reference->m_next) {
    return nullptr;
  }
  variable_reference* splitted = reference->m_next;
  reference->m_next = splitted->m_next;
  splitted->m_next = split(splitted->m_next);
  return splitted;
}

variable_reference* interface::merge(variable_reference* lhs, variable_reference* rhs) {
  if (!lhs) {
    return rhs;
  }

  if (!rhs) {
    return lhs;
  }

  if (strcmp(lhs->m_name, rhs->m_name) > 0) {
    rhs->m_next = merge(lhs, rhs->m_next);
    return rhs;
  }

  lhs->m_next = merge(lhs->m_next, rhs);
  return lhs;
}

variable_reference* interface::sort(variable_reference* reference) {
  if (!reference) {
    return nullptr;
  }

  if (!reference->m_next) {
    return reference;
  }

  variable_reference* splitted = split(reference);

  return merge(sort(reference), sort(splitted));
}

} // namespace rx::console
