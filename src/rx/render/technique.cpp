#include <string.h>

#include <rx/render/technique.h>
#include <rx/render/frontend.h>

#include <rx/core/log.h>
#include <rx/core/optional.h>
#include <rx/core/filesystem/file.h>

RX_LOG("render/technique", log_technique);

namespace rx::render {

// simple recursive descent parser and evaluator for declarative predicates
// <ident> := [a-Z0-9_]
// <op>    := "&&"" | "||"
// <expr>  := <ident> | (<expr>) | <expr> <op> <expr>
enum {
  k_unmatched_parenthesis        = -1,
  k_unexpected_character         = -2,
  k_unexpected_end_of_expression = -3,
  k_undeclared_identifier        = -4
};

static constexpr const char* binexp_result_to_string(int _result) {
  switch (_result) {
  case k_unmatched_parenthesis:
    return "unmatched parenthesis";
  case k_unexpected_character:
    return "unexpected character";
  case k_unexpected_end_of_expression:
    return "unexpected end of expression";
  case k_undeclared_identifier:
    return "undeclared identifier";
  }
  return nullptr;
}

static void binexp_skip_spaces(const char*& expression_) {
  for (; *expression_ == ' '; expression_++);
}

static int binexp_parse_binary(const char*& expression_, int& parenthesis_, const map<string, bool>& _values);
static int binexp_parse_atom(const char*& expression_, int& parenthesis_, const map<string, bool>& _values) {
  binexp_skip_spaces(expression_);

  bool negated{false};
  if (*expression_ == '!') {
    negated = true;
    expression_++; // skip '!'
  }

  if (*expression_ == '(') {
    expression_++; // skip '('
    parenthesis_++;
    const auto result{binexp_parse_binary(expression_, parenthesis_, _values)};
    if (result < 0) {
      return result;
    }
    if (*expression_ != ')') {
      return k_unmatched_parenthesis;
    }
    expression_++; // skip ')'
    parenthesis_--;
    return negated ? !result : result;
  }

  string identifier;
  const char* end{strpbrk(expression_, " )")}; // ' ' or ')' marks end of an identifier
  if (end) {
    identifier = {expression_, end};
  } else {
    // otherwise the identifier is at the end of the string
    identifier = expression_;
  }

  const auto find{_values.find(identifier)};
  if (!find) {
    return k_undeclared_identifier;
  }

  expression_ += identifier.size();

  return negated ? !*find : *find;
}

static int binexp_parse_binary(const char*& expression_, int& parenthesis_, const map<string, bool>& _values) {
  auto result{binexp_parse_atom(expression_, parenthesis_, _values)};
  if (result < 0) {
    return result;
  }

  for (;;) {
    binexp_skip_spaces(expression_);
    const bool is_and{!strncmp(expression_, "&&", 2)};
    const bool is_or{!strncmp(expression_, "||", 2)};
    if (!is_and && !is_or) {
      return result;
    }

    expression_ += 2; // skip '&&' or '||'
    const auto tail_result{binexp_parse_atom(expression_, parenthesis_, _values)};
    if (tail_result < 0) {
      return tail_result;
    }

    if (is_and) {
      result &= tail_result;
    } else {
      result |= tail_result;
    }
  }

  return k_unexpected_end_of_expression;
}

static int binexp_evaluate(const char* _expression, const map<string, bool>& _values) {
  if (!*_expression) {
    return 1;
  }

  int parenthesis{0};
  const auto result{binexp_parse_binary(_expression, parenthesis, _values)};
  if (result < 0) {
    return result;
  }
  if (parenthesis != 0 || *_expression == ')') {
    return k_unmatched_parenthesis;
  }
  if (*_expression != '\0') {
    return k_unexpected_character;
  }
  return result;
}

optional<uniform::category> uniform_category_from_string(const string& _category) {
  static constexpr const struct {
    const char* match;
    uniform::category category;
  } k_table[]{
    {"sampler1D",  uniform::category::k_sampler1D},
    {"sampler2D",  uniform::category::k_sampler2D},
    {"sampler3D",  uniform::category::k_sampler3D},
    {"samplerCM",  uniform::category::k_samplerCM},
    {"bool",       uniform::category::k_bool},
    {"float",      uniform::category::k_float},
    {"vec2i",      uniform::category::k_vec2i},
    {"vec3i",      uniform::category::k_vec3i},
    {"vec4i",      uniform::category::k_vec4i},
    {"vec2f",      uniform::category::k_vec2f},
    {"vec3f",      uniform::category::k_vec3f},
    {"vec4f",      uniform::category::k_vec4f},
    {"mat4x4f",    uniform::category::k_mat4x4f},
    {"mat3x3f",    uniform::category::k_mat3x3f}
  };

  for (const auto& element : k_table) {
    if (_category == element.match) {
      return element.category;
    }
  }

  return nullopt;
}

optional<shader::inout_category> inout_category_from_string(const string& _category) {
  static constexpr const struct {
    const char* match;
    shader::inout_category category;
  } k_table[]{
    {"vec2i", shader::inout_category::k_vec2i},
    {"vec3i", shader::inout_category::k_vec3i},
    {"vec4i", shader::inout_category::k_vec4i},
    {"vec2f", shader::inout_category::k_vec2f},
    {"vec3f", shader::inout_category::k_vec3f},
    {"vec4f", shader::inout_category::k_vec4f},
    {"vec4b", shader::inout_category::k_vec4b}
  };

  for (const auto& element : k_table) {
    if (element.match == _category) {
      return element.category;
    }
  }

  return nullopt;
}

technique::technique(frontend* _frontend)
  : m_frontend{_frontend}
  , m_programs{m_frontend->allocator()}
  , m_permute_flags{m_frontend->allocator()}
  , m_name{m_frontend->allocator()}
  , m_error{m_frontend->allocator()}
  , m_shader_definitions{m_frontend->allocator()}
  , m_uniforms{m_frontend->allocator()}
  , m_specializations{m_frontend->allocator()}
{
}

technique::~technique() {
  m_programs.each_fwd([this](program* _program) {
    m_frontend->destroy_program(RX_RENDER_TAG("technique"), _program);
  });
}

technique::technique(technique&& _technique)
  : m_frontend{_technique.m_frontend}
  , m_type{_technique.m_type}
  , m_programs{utility::move(_technique.m_programs)}
  , m_permute_flags{utility::move(_technique.m_permute_flags)}
  , m_name{utility::move(_technique.m_name)}
  , m_error{utility::move(_technique.m_error)}
  , m_shader_definitions{utility::move(_technique.m_shader_definitions)}
  , m_uniforms{utility::move(_technique.m_uniforms)}
  , m_specializations{utility::move(_technique.m_specializations)}
{
  _technique.m_frontend = nullptr;
}

technique& technique::operator=(technique&& _technique) {
  m_frontend = _technique.m_frontend;
  m_type = _technique.m_type;
  m_programs = utility::move(_technique.m_programs);
  m_name = utility::move(_technique.m_name);
  m_error = utility::move(_technique.m_error);
  m_shader_definitions = utility::move(_technique.m_shader_definitions);
  m_uniforms = utility::move(_technique.m_uniforms);
  m_specializations = utility::move(_technique.m_specializations);
  return *this;
}

bool technique::evaluate_when_for_permute(const string& _when, rx_u64 _flags) const {
  map<string, bool> values{m_frontend->allocator()};
  for (rx_size i{0}; i < m_specializations.size(); i++) {
    values.insert(m_specializations[i], _flags & (1_u64 << i));
  }
  const int result{binexp_evaluate(_when.data(), values)};
  if (result < 0) {
    return error("when expression evaluation failed: %s", binexp_result_to_string(result));
  }
  return result;
}

bool technique::evaluate_when_for_variant(const string& _when, rx_size _index) const {
  map<string, bool> values{m_frontend->allocator()};
  for (rx_size i{0}; i < m_specializations.size(); i++) {
    values.insert(m_specializations[i], i == _index);
  }
  const int result{binexp_evaluate(_when.data(), values)};
  if (result < 0) {
    return error("when expression evaluation failed: %s", binexp_result_to_string(result));
  }
  return result;
}

bool technique::evaluate_when_for_basic(const string& _when) const {
  return _when.is_empty();
}

bool technique::compile() {
  shader_definition* vertex{nullptr};
  shader_definition* fragment{nullptr};
  m_shader_definitions.each_fwd([&](shader_definition& _shader) {
    if (_shader.type == shader::category::k_fragment) {
      fragment = &_shader;
    } else if (_shader.type == shader::category::k_vertex) {
      vertex = &_shader;
    }
    return true;
  });

  // if we have a fragment shader, ensure we have a vertex shader to go with it
  if (fragment) {
    if (!vertex) {
      return error("missing vertex shader");
    }

    // ensure all fragment inputs wire correctly into vertex outputs
    const bool check_inouts{
      // enumerate all vertex outputs and check for matching fragment inputs
      vertex->outputs.each([&](rx_size, const string& _name, const shader_definition::inout& _inout_definition) {
        const auto check{fragment->inputs.find(_name)};
        if (!check) {
          return error("could not find fragment input for vertex output '%s'", _name);
        }
        if (check->type != _inout_definition.type) {
          return error("type mismatch for fragment input '%s'", _name);
        }
        if (check->when != _inout_definition.when) {
          return error("when mismatch for fragment input '%s'", _name);
        }
        return true;
      })
      &&
      // enumerate all fragment inputs and check for matching vertex outputs
      fragment->inputs.each([&](rx_size, const string& _name, const shader_definition::inout& _inout_definition) {
        const auto check{vertex->outputs.find(_name)};
        if (!check) {
          return error("could not find vertex output for fragment input '%s'", _name);
        }
        if (check->type != _inout_definition.type) {
          return error("type mismatch for vertex output '%s'", _name);
        }
        if (check->when != _inout_definition.when) {
          return error("when mismatch for vertex output '%s'", _name);
        }
        return true;
      })
    };

    if (!check_inouts) {
      return false;
    }

    // ensure there is at least one fragment output
    if (fragment->outputs.is_empty()) {
      return error("missing output in fragment shader");
    }
  }

  if (m_type == category::k_basic) {
    // create and add just a single program to m_programs
    auto program{m_frontend->create_program(RX_RENDER_TAG("technique"))};

    m_shader_definitions.each_fwd([&](const shader_definition& _shader_definition) {
      if (evaluate_when_for_basic(_shader_definition.when)) {
        shader specialized_shader;
        specialized_shader.type = _shader_definition.type;
        specialized_shader.source = _shader_definition.source;

        // emit inputs
        _shader_definition.inputs.each([&](rx_size, const string& _name, const shader_definition::inout& _inout) {
          if (evaluate_when_for_basic(_inout.when)) {
            specialized_shader.inputs.insert(_name, {_inout.index, _inout.type});
          }
        });

        // emit outputs
        _shader_definition.outputs.each([&](rx_size, const string& _name, const shader_definition::inout& _inout){
          if (evaluate_when_for_basic(_inout.when)) {
            specialized_shader.outputs.insert(_name, {_inout.index, _inout.type});
          }
        });

        program->add_shader(utility::move(specialized_shader));
      }
    });

    m_uniforms.each([&](rx_size, const string& _name, const uniform_definition& _uniform_definition) {
      if (evaluate_when_for_basic(_uniform_definition.when)) {
        program->add_uniform(_name, _uniform_definition.type);
      }
    });

    m_frontend->initialize_program(RX_RENDER_TAG("technique"), program);

    m_programs.push_back(program);
  } else if (m_type == category::k_permute) {
    const rx_u64 mask{(1_u64 << m_specializations.size()) - 1};
    auto generate{[&](rx_u64 _flags) {
      m_permute_flags.push_back(_flags);

      auto program{m_frontend->create_program(RX_RENDER_TAG("technique"))};
      m_shader_definitions.each_fwd([&](const shader_definition& _shader_definition) {
        if (evaluate_when_for_permute(_shader_definition.when, _flags)) {
          shader specialized_shader;
          specialized_shader.type = _shader_definition.type;

          // emit #defines
          const rx_size specializations{m_specializations.size()};
          for (rx_size i{0}; i < specializations; i++) {
            const string& specialication{m_specializations[i]};
            if (_flags & (1_u64 << i)) {
              specialized_shader.source.append(string::format("#define %s\n", specialication));
            }
          }

          // append shader source
          specialized_shader.source.append(_shader_definition.source);

          // emit inputs
          _shader_definition.inputs.each([&](rx_size, const string& _name, const shader_definition::inout& _inout) {
            if (evaluate_when_for_permute(_inout.when, _flags)) {
              specialized_shader.inputs.insert(_name, {_inout.index, _inout.type});
            }
          });

          // emit outputs
          _shader_definition.outputs.each([&](rx_size, const string& _name, const shader_definition::inout& _inout){
            if (evaluate_when_for_permute(_inout.when, _flags)) {
              specialized_shader.outputs.insert(_name, {_inout.index, _inout.type});
            }
          });

          program->add_shader(utility::move(specialized_shader));
        }
      });

      // emit uniforms
      m_uniforms.each([&](rx_size, const string& _name, const uniform_definition& _uniform_definition) {
        if (evaluate_when_for_permute(_uniform_definition.when, _flags)) {
          program->add_uniform(_name, _uniform_definition.type);
        }
      });

      // initialize and track
      m_frontend->initialize_program(RX_RENDER_TAG("technique"), program);
      m_programs.push_back(program);
    }};

    for (rx_u64 flags{0}; flags != mask; flags = ((flags | ~mask) + 1_u64) & mask) {
      generate(flags);
    }

    generate(mask);
  } else if (m_type == category::k_variant) {
    const rx_size specializations{m_specializations.size()};
    for (rx_size i{0}; i < specializations; i++) {
      const auto& specialization{m_specializations[i]};
      auto program{m_frontend->create_program(RX_RENDER_TAG("technique"))};
      m_shader_definitions.each_fwd([&](const shader_definition& _shader_definition) {
        if (evaluate_when_for_variant(_shader_definition.when, i)) {
          shader specialized_shader;
          specialized_shader.type = _shader_definition.type;

          // emit #defines
          specialized_shader.source.append(string::format("#define %s\n", specialization));

          // append shader source
          specialized_shader.source.append(_shader_definition.source);

          // emit inputs
          _shader_definition.inputs.each([&](rx_size, const string& _name, const shader_definition::inout& _inout) {
            if (evaluate_when_for_variant(_inout.when, i)) {
              specialized_shader.inputs.insert(_name, {_inout.index, _inout.type});
            }
          });

          // emit outputs
          _shader_definition.outputs.each([&](rx_size, const string& _name, const shader_definition::inout& _inout){
            if (evaluate_when_for_variant(_inout.when, i)) {
              specialized_shader.outputs.insert(_name, {_inout.index, _inout.type});
            }
          });

          program->add_shader(utility::move(specialized_shader));
        }
      });

      // emit uniforms
      m_uniforms.each([&](rx_size, const string& _name, const uniform_definition& _uniform_definition) {
        if (evaluate_when_for_variant(_uniform_definition.when, i)) {
          program->add_uniform(_name, _uniform_definition.type);
        }
      });
    
      // initialize and track
      m_frontend->initialize_program(RX_RENDER_TAG("technique"), program);
      m_programs.push_back(program);
    };
  }

  return true;
}

technique::operator program*() const {
  RX_ASSERT(m_type == category::k_basic, "not a basic technique");
  return m_programs[0];
}

program* technique::permute(rx_u64 _flags) const {
  RX_ASSERT(m_type == category::k_permute, "not a permute technique");

  const rx_size permutations{m_permute_flags.size()};
  for (rx_size i{0}; i < permutations; i++) {
    if (m_permute_flags[i] != _flags) {
      continue;
    }
    return m_programs[i];
  }

  return nullptr;
}

program* technique::variant(rx_size _index) const {
  RX_ASSERT(m_type == category::k_variant, "not a variant technique");
  return m_programs[_index];
}

bool technique::load(const string& _file_name) {
  const auto data{filesystem::read_binary_file(_file_name)};
  if (!data) {
    return false;
  }

  string contents{m_frontend->allocator()};
  contents.resize(data->size());
  memcpy(contents.data(), data->data(), data->size());

  if (!parse({contents})) {
    return false;
  }

  return compile();
}

bool technique::parse(const json& _description) {
  if (!_description) {
    const auto json_error{_description.error()};
    if (json_error) {
      return error("%s", *json_error);
    } else {
      return error("empty description");
    }
  }

  const auto& name{_description["name"]};
  if (!name) {
    return error("missing name");
  }

  if (!name.is_string()) {
    return error("expected String");
  }

  m_name = utility::move(name.as_string());

  const auto& uniforms{_description["uniforms"]};
  const auto& shaders{_description["shaders"]};
  const auto& permutes{_description["permutes"]};
  const auto& variants{_description["variants"]};

  if (!shaders) {
    return error("missing shaders");
  }

  if (permutes && variants) {
    return error("cannot define both permutes and variants");
  }

  if (uniforms && !parse_uniforms(uniforms)) {
    return false;
  }

  if (!parse_shaders(shaders)) {
    return false;
  }

  if (permutes) {
    if (!parse_specializations(permutes, "permutes")) {
      return false;
    }
    m_type = category::k_permute;
  } else if (variants) {
    if (!parse_specializations(variants, "variants")) {
      return false;
    }
    m_type = category::k_variant;
  } else {
    m_type = category::k_basic;
  }

  return true;
}

void technique::write_log(log::level _level, string&& _message) const {
  if (m_name.is_empty()) {
    log_technique(_level, "%s", _message);
  } else {
    log_technique(_level, "technique '%s': %s", m_name, _message);
  }
}

bool technique::parse_uniforms(const json& _uniforms) {
  if (!_uniforms.is_array()) {
    return error("expected Array for 'uniforms'");
  }

  return _uniforms.each([this](const json& _uniform) {
    return parse_uniform(_uniform);
  });
}

bool technique::parse_shaders(const json& _shaders) {
  if (!_shaders.is_array()) {
    return error("expected Array for 'shaders'");
  }

  return _shaders.each([this](const json& _shader) {
    return parse_shader(_shader);
  });
}

bool technique::parse_uniform(const json& _uniform) {
  if (!_uniform.is_object()) {
    return error("expected Object");
  }

  const auto& name{_uniform["name"]};
  const auto& type{_uniform["type"]};
  const auto& when{_uniform["when"]};

  if (!name) {
    return error("missing 'name' in uniform");
  }

  if (!type) {
    return error("missing 'type' in uniform");
  }

  if (!name.is_string()) {
    return error("expected String for 'name'");
  }

  if (!type.is_string()) {
    return error("expected String for 'type'");
  }

  if (when && !when.is_string()) {
    return error("expected String for 'when'");
  }

  const auto name_string{name.as_string()};
  const auto type_string{type.as_string()};

  if (m_uniforms.find(name_string)) {
    return error("duplicate uniform '%s'", name_string);
  }

  const auto type_category{uniform_category_from_string(type_string)};
  if (!type_category) {
    return error("unknown type '%s' for '%s'", type_string, name_string);
  }

  m_uniforms.insert(name_string, {*type_category, when ? when.as_string() : ""});
  return true;
}

bool technique::parse_shader(const json& _shader) {
  if (!_shader.is_object()) {
    return error("expected Object");
  }

  const auto& type{_shader["type"]};
  const auto& source{_shader["source"]};
  const auto& when{_shader["when"]};

  if (!type) {
    return error("missing 'type' in shader");
  }

  if (!source) {
    return error("missing 'source' in shader");
  }

  if (!type.is_string()) {
    return error("expected String for 'type'");
  }

  if (!source.is_string()) {
    return error("expected String for 'source'");
  }

  if (when && !when.is_string()) {
    return error("expected String for 'when'");
  }

  const auto type_string{type.as_string()};
  shader::category shader_type;
  if (type_string == "vertex") {
    shader_type = shader::category::k_vertex;
  } else if (type_string == "fragment") {
    shader_type = shader::category::k_fragment;
  } else {
    return error("unknown type '%s' for shader", type_string);
  }

  // ensure we don't have multiple definitions of the same shader
  if (!m_shader_definitions.each_fwd([shader_type](const shader_definition& _shader)
    { return _shader.type != shader_type; }))
  {
    return error("multiple %s shaders present", type_string);
  }

  shader_definition definition;
  definition.type = shader_type;
  definition.source = _shader["source"].as_string();
  definition.when = when ? when.as_string() : "";

  const auto& inputs{_shader["inputs"]};
  if (inputs && !parse_inouts(inputs, "input", definition.inputs)) {
    return false;
  }

  const auto& outputs{_shader["outputs"]};
  if (outputs && !parse_inouts(outputs, "output", definition.outputs)) {
    return false;
  }

  m_shader_definitions.push_back(utility::move(definition));
  return true;
}

bool technique::parse_inouts(const json& _inouts, const char* _type, map<string, shader_definition::inout>& inouts_) {
  if (!_inouts.is_array()) {
    return error("expected Array in %ss", _type);
  }

  return _inouts.each([&](const json& _inout) {
    return parse_inout(_inout, _type, inouts_);
  });
}

bool technique::parse_inout(const json& _inout, const char* _type, map<string, shader_definition::inout>& inouts_) {
  if (!_inout.is_object()) {
    return error("expected Object for %s", _type);
  }

  const auto& name{_inout["name"]};
  const auto& type{_inout["type"]};
  const auto& when{_inout["when"]};

  if (!name) {
    return error("missing 'name' in %s", _type);
  }

  if (!type) {
    return error("missing 'type' in %s", _type);
  }

  if (!name.is_string()) {
    return error("expected String for 'name'");
  }

  if (!type.is_string()) {
    return error("expected String for 'type'");
  }

  if (when && !when.is_string()) {
    return error("expected String for 'when'");
  }

  const auto name_string{name.as_string()};
  if (inouts_.find(name_string)) {
    return error("duplicate '%s'", name_string);
  }

  const auto type_string{type.as_string()};
  const auto type_category{inout_category_from_string(type_string)};
  if (!type_category) {
    return error("unknown type '%s' for '%s'", type_string, name_string);
  }

  shader_definition::inout inout;
  inout.index = inouts_.size();
  inout.type = *type_category;
  inout.when = when ? when.as_string() : "";

  inouts_.insert(name_string, inout);
  return true;
}

bool technique::parse_specializations(const json& _specializations, const char* _type) {
  if (!_specializations.is_array()) {
    return error("expected Array for '%ss'", _type);
  }

  return _specializations.each([this, _type](const json& _specialization) {
    return parse_specialization(_specialization, _type);
  });
}

bool technique::parse_specialization(const json& _specialization, const char* _type) {
  if (!_specialization.is_string()) {
    return error("expected String for '%s'", _type);
  }

  m_specializations.push_back(_specialization.as_string());
  return true;
}

} // namespace rx::render