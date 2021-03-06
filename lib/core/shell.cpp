// Metashell - Interactive C++ template metaprogramming shell
// Copyright (C) 2013, Abel Sinkovics (abel@sinkovics.hu)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <metashell/metashell.hpp>

#include <metashell/shell.hpp>
#include <metashell/version.hpp>
#include <metashell/header_file_environment.hpp>
#include <metashell/metashell_pragma.hpp>
#include <metashell/data/command.hpp>
#include <metashell/to_string.hpp>
#include <metashell/exception.hpp>
#include <metashell/null_history.hpp>

#include <cctype>
#include <algorithm>
#include <sstream>

using namespace metashell;

namespace
{
  bool determine_echo(const data::config& cfg_)
  {
    return cfg_.preprocessor_mode;
  }

  bool determine_show_cpp_errors(const data::config& cfg_)
  {
    return !cfg_.preprocessor_mode;
  }

  bool determine_evaluate_metaprograms(const data::config& cfg_)
  {
    return !cfg_.preprocessor_mode;
  }

  std::string wrap(const std::string& s_, const std::string& wrapper_)
  {
    return wrapper_ + s_ + wrapper_;
  }

  void make_failure(data::result& r_, const std::string& msg_)
  {
    r_.successful = false;
    r_.output.clear();
    r_.error = msg_;
  }

  std::string extend(const std::string& s_, int width_)
  {
    const int len = s_.length();
    return len < width_ ? s_ + std::string(width_ - len, ' ') : s_;
  }

  std::string max_template_depth_info(int depth_)
  {
    std::ostringstream s;
    s << "Maximum template depth: " << depth_;
    return s.str();
  }

  void display(const data::result& r_,
               iface::displayer& displayer_,
               bool show_as_type_)
  {
    if (!r_.info.empty())
    {
      displayer_.show_raw_text(r_.info);
    }
    if (!r_.successful)
    {
      displayer_.show_error(r_.error);
    }
    if (r_.successful && !r_.output.empty())
    {
      if (show_as_type_)
      {
        displayer_.show_type(data::type(r_.output));
      }
      else
      {
        displayer_.show_cpp_code(r_.output);
      }
    }
  }

  bool has_non_whitespace(const std::string& s_)
  {
    for (char c : s_)
    {
      if (!std::isspace(c))
      {
        return true;
      }
    }
    return false;
  }

  bool is_empty_line(const data::command& cmd_)
  {
    return std::find_if(cmd_.begin(), cmd_.end(),
                        [](const data::token& t_) -> bool
                        {
                          const data::token_category c = t_.category();
                          return c != data::token_category::whitespace &&
                                 c != data::token_category::comment;
                        }) == cmd_.end();
  }

  data::result eval_tmp_formatted(const iface::environment& env_,
                                  const std::string& tmp_exp_,
                                  bool use_precompiled_headers_,
                                  iface::engine& engine_,
                                  logger* logger_)
  {
    using std::string;
    using std::pair;

    METASHELL_LOG(
        logger_,
        "Checking if metaprogram can be evaluated without metashell::format: " +
            tmp_exp_);

    const data::result simple =
        engine_.eval(env_, tmp_exp_, boost::none, use_precompiled_headers_);

    METASHELL_LOG(
        logger_,
        !simple.successful ?
            "Errors occured during metaprogram evaluation. Displaying errors"
            " coming from the metaprogram without metashell::format" :
            "No errors occured during metaprogram evaluation. Re-evaluating it"
            " with metashell::format");

    return simple.successful ?
               engine_.eval(env_, "::metashell::format<" + tmp_exp_ + ">::type",
                            boost::none, use_precompiled_headers_) :
               simple;
  }

  const char default_env[] =
"#define __METASHELL\n"
"#define __METASHELL_MAJOR " TO_STRING(METASHELL_MAJOR) "\n"
"#define __METASHELL_MINOR " TO_STRING(METASHELL_MINOR) "\n"
"#define __METASHELL_PATCH " TO_STRING(METASHELL_PATCH) "\n"

R"(
namespace metashell {
  namespace impl {
    template <class T>
    struct wrap {}; // This is a metashell internal type

    template <class T>
    typename T::tag* tag_of(::metashell::impl::wrap<T>);

    void* tag_of(...);

    template <class T>
    struct remove_ptr;

    template <class T>
    struct remove_ptr<T*> { typedef T type; };
  } // namespace impl

  template <class Tag>
  struct format_impl
  {
    typedef format_impl type;

    template <class T>
    struct apply { typedef T type; };
  };

  template <class T>
  struct format :
    ::metashell::format_impl<
      typename ::metashell::impl::remove_ptr<
        decltype(::metashell::impl::tag_of(::metashell::impl::wrap<T>()))
      >::type
    >::template apply<T>
  {};
} // namespace metashell
)";
}

shell::shell(const data::config& config_,
             const boost::filesystem::path& internal_dir_,
             const boost::filesystem::path& env_filename_,
             const boost::filesystem::path& mdb_temp_dir_,
             std::unique_ptr<iface::engine> engine_,
             logger* logger_)
  : _internal_dir(internal_dir_),
    _env_filename(env_filename_),
    _env(),
    _config(config_),
    _stopped(false),
    _logger(logger_),
    _engine(std::move(engine_)),
    _echo(determine_echo(config_)),
    _show_cpp_errors(determine_show_cpp_errors(config_)),
    _evaluate_metaprograms(determine_evaluate_metaprograms(config_))
{
  rebuild_environment();
  init(nullptr, mdb_temp_dir_);
}

shell::shell(const data::config& config_,
             command_processor_queue& cpq_,
             const boost::filesystem::path& internal_dir_,
             const boost::filesystem::path& env_filename_,
             const boost::filesystem::path& mdb_temp_dir_,
             std::unique_ptr<iface::engine> engine_,
             logger* logger_)
  : _internal_dir(internal_dir_),
    _env_filename(env_filename_),
    _env(),
    _config(config_),
    _stopped(false),
    _logger(logger_),
    _engine(std::move(engine_)),
    _echo(determine_echo(config_)),
    _show_cpp_errors(determine_show_cpp_errors(config_)),
    _evaluate_metaprograms(determine_evaluate_metaprograms(config_))
{
  rebuild_environment();
  init(&cpq_, mdb_temp_dir_);
}

shell::shell(const data::config& config_,
             std::unique_ptr<iface::environment> env_,
             command_processor_queue& cpq_,
             const boost::filesystem::path& internal_dir_,
             const boost::filesystem::path& env_filename_,
             const boost::filesystem::path& mdb_temp_dir_,
             std::unique_ptr<iface::engine> engine_,
             logger* logger_)
  : _internal_dir(internal_dir_),
    _env_filename(env_filename_),
    _env(std::move(env_)),
    _config(config_),
    _stopped(false),
    _logger(logger_),
    _engine(std::move(engine_)),
    _echo(determine_echo(config_)),
    _show_cpp_errors(determine_show_cpp_errors(config_)),
    _evaluate_metaprograms(determine_evaluate_metaprograms(config_))
{
  init(&cpq_, mdb_temp_dir_);
}

void shell::cancel_operation() {}

void shell::display_splash(
    iface::displayer& displayer_,
    const std::map<std::string, std::string>& dependency_versions_)
{
  using data::paragraph;

  const std::string version_desc =
#include "version_desc.hpp"
      ;

  const paragraph empty_line("");

  data::text splash_text;
  splash_text.paragraphs.push_back(
      paragraph("Template metaprogramming shell " + version()));

  if (!version_desc.empty())
  {
    splash_text.paragraphs.push_back(paragraph(version_desc));
  }

  splash_text.paragraphs.push_back(empty_line);
  splash_text.paragraphs.push_back(paragraph(
      "Metashell Copyright (C) 2015 Abel Sinkovics (abel@sinkovics.hu)"));
  splash_text.paragraphs.push_back(paragraph(
      "                             Andras Kucsma  (andras.kucsma@gmail.com)"));
  splash_text.paragraphs.push_back(paragraph(
      "This program comes with ABSOLUTELY NO WARRANTY. This is free software,"
      " and you are welcome to redistribute it under certain conditions; for"
      " details visit <http://www.gnu.org/licenses/>."));
  splash_text.paragraphs.push_back(empty_line);
  splash_text.paragraphs.push_back(paragraph("Based on"));
  for (const auto& d : dependency_versions_)
  {
    splash_text.paragraphs.push_back(
        paragraph(d.second, "             ", extend("  " + d.first, 13)));
  }
  splash_text.paragraphs.push_back(empty_line);
  splash_text.paragraphs.push_back(paragraph(
      _config.use_precompiled_headers ? "Using precompiled headers" :
                                        "Not using precompiled headers"));
  splash_text.paragraphs.push_back(
      paragraph(max_template_depth_info(_config.max_template_depth)));
  splash_text.paragraphs.push_back(empty_line);
  splash_text.paragraphs.push_back(paragraph("Getting help: #msh help"));

  displayer_.show_comment(splash_text);

  if (_config.verbose)
  {
    displayer_.show_comment(data::text("Verbose mode: ON"));
  }
}

void shell::line_available(const std::string& s_,
                           iface::displayer& displayer_,
                           iface::history& history_)
{
  try
  {
    if (s_.empty() || s_.back() != '\\')
    {
      const std::string s = _line_prefix + s_;
      _line_prefix.clear();

      const data::command cmd(s);

      if (has_non_whitespace(s))
      {
        if (_prev_line != s)
        {
          history_.add(s);
          _prev_line = s;
        }

        if (!is_empty_line(cmd))
        {
          if (boost::optional<data::command::iterator> p = parse_pragma(cmd))
          {
            _pragma_handlers.process(*p, cmd.end(), displayer_);
          }
          else if (!_echo || preprocess(displayer_, s, true))
          {
            if (is_environment_setup_command(cmd))
            {
              store_in_buffer(s, displayer_);
            }
            else
            {
              run_metaprogram(s, displayer_);
            }
          }
        }
      }
    }
    else
    {
      _line_prefix += s_.substr(0, s_.size() - 1);
    }
  }
  catch (const std::exception& e)
  {
    displayer_.show_error(std::string("Error: ") + e.what());
  }
  catch (...)
  {
    displayer_.show_error("Unknown error");
  }
}

std::string shell::prompt() const
{
  return _line_prefix.empty() ? ">" : "...>";
}

bool shell::store_in_buffer(const std::string& s_, iface::displayer& displayer_)
{
  const data::result r =
      _engine->validate_code(s_, _config, *_env, using_precompiled_headers());

  if (r.successful)
  {
    try
    {
      _env->append(s_);
    }
    catch (const std::exception& e)
    {
      displayer_.show_error(e.what());
      return false;
    }
  }
  if (_show_cpp_errors || r.successful)
  {
    display(r, displayer_, true);
  }
  return r.successful;
}

void shell::code_complete(const std::string& s_,
                          std::set<std::string>& out_) const
{
  try
  {
    _engine->code_complete(*_env, s_, out_, using_precompiled_headers());
  }
  catch (...)
  {
    // ignore
  }
}

void shell::init(command_processor_queue* cpq_,
                 const boost::filesystem::path& mdb_temp_dir_)
{
  _env->append(default_env);

  // TODO: move it to initialisation later
  _pragma_handlers =
      pragma_handler_map::build_default(*this, cpq_, mdb_temp_dir_, _logger);
}

const pragma_handler_map& shell::pragma_handlers() const
{
  return _pragma_handlers;
}

void shell::verbose(bool enabled_) { _config.verbose = enabled_; }

bool shell::verbose() const { return _config.verbose; }

bool shell::stopped() const { return _stopped; }

void shell::stop() { _stopped = true; }

bool shell::using_precompiled_headers() const
{
  return _config.use_precompiled_headers;
}

void shell::using_precompiled_headers(bool enabled_)
{
  _config.use_precompiled_headers = enabled_;
  rebuild_environment();
}

iface::environment& shell::env() { return *_env; }

const iface::environment& shell::env() const { return *_env; }

void shell::rebuild_environment(const std::string& content_)
{
  _env.reset(new header_file_environment(
      *_engine, _config, _internal_dir, _env_filename));
  if (!content_.empty())
  {
    _env->append(content_);
  }
}

void shell::rebuild_environment()
{
  rebuild_environment(_env ? _env->get_all() : std::string());
}

void shell::push_environment() { _environment_stack.push(_env->get_all()); }

void shell::pop_environment()
{
  if (_environment_stack.empty())
  {
    throw exception("The environment stack is empty.");
  }
  else
  {
    rebuild_environment(_environment_stack.top());
    _environment_stack.pop();
  }
}

void shell::display_environment_stack_size(iface::displayer& displayer_)
{
  if (_environment_stack.empty())
  {
    displayer_.show_comment(data::text("Environment stack is empty"));
  }
  else if (_environment_stack.size() == 1)
  {
    displayer_.show_comment(data::text("Environment stack has 1 entry"));
  }
  else
  {
    std::ostringstream s;
    s << "Environment stack has " << _environment_stack.size() << " entries";
    displayer_.show_comment(data::text(s.str()));
  }
}

void shell::run_metaprogram(const std::string& s_, iface::displayer& displayer_)
{
  if (_evaluate_metaprograms)
  {
    const data::result r = eval_tmp_formatted(
        *_env, s_, using_precompiled_headers(), *_engine, _logger);
    if (_show_cpp_errors || r.successful)
    {
      display(r, displayer_, true);
    }
  }
}

void shell::reset_environment()
{
  rebuild_environment("");
  _env->append(default_env);
}

const data::config& shell::get_config() const { return _config; }

void shell::line_available(const std::string& s_, iface::displayer& displayer_)
{
  null_history h;
  line_available(s_, displayer_, h);
}

iface::engine& shell::engine() { return *_engine; }

boost::filesystem::path shell::env_path() const
{
  return _internal_dir / _env_filename;
}

bool shell::preprocess(iface::displayer& displayer_,
                       const std::string& exp_,
                       bool process_directives_) const
{
  const std::string marker =
      wrap("* __METASHELL_PP_MARKER *", process_directives_ ? "\n" : "");

  data::result r =
      _engine->precompile(_env->get_all() + "\n" + marker + exp_ + marker);

  if (r.successful)
  {
    const auto p1 = r.output.find(marker);
    if (p1 == std::string::npos)
    {
      make_failure(r, "Marker (" + marker +
                          ") not found in preprocessed output."
                          " Does it contain a macro that has been defined?");
    }
    else
    {
      const auto m_len = marker.size();
      const auto p2 = r.output.find(marker, p1 + m_len);
      if (p2 == std::string::npos)
      {
        make_failure(r, "Marker (" + marker +
                            ") found only once in preprocessed output.");
      }
      else if (r.output.find(marker, p2 + m_len) == std::string::npos)
      {
        r.output = r.output.substr(p1 + m_len, p2 - p1 - m_len);
      }
      else
      {
        make_failure(
            r, "Marker (" + marker +
                   ") found more than two times in preprocessed output.");
      }
    }
  }

  display(r, displayer_, false);
  return r.successful;
}

void shell::echo(bool enabled_) { _echo = enabled_; }

bool shell::echo() const { return _echo; }

void shell::show_cpp_errors(bool enabled_) { _show_cpp_errors = enabled_; }

bool shell::show_cpp_errors() const { return _show_cpp_errors; }

void shell::evaluate_metaprograms(bool enabled_)
{
  _evaluate_metaprograms = enabled_;
}

bool shell::evaluate_metaprograms() const { return _evaluate_metaprograms; }
