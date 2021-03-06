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

#include <metashell/parse_config.hpp>

#include "mock_environment_detector.hpp"

#include <just/test.hpp>

#include <sstream>
#include <iostream>
#include <vector>

using namespace metashell;

namespace
{
  parse_config_result parse_config(std::initializer_list<const char*> args_,
                                   std::ostringstream* out_ = nullptr,
                                   std::ostringstream* err_ = nullptr)
  {
    std::vector<const char*> args{"metashell"};
    args.insert(args.end(), args_.begin(), args_.end());
    mock_environment_detector env_detector;

    return metashell::parse_config(args.size(), args.data(),
                                   std::map<std::string, engine_entry>(),
                                   env_detector, out_, err_);
  }

  bool fails_and_displays_error(std::initializer_list<const char*> args_)
  {
    std::ostringstream err;
    const parse_config_result r = parse_config(args_, nullptr, &err);

    return parse_config_result::action_t::exit_with_error == r.action &&
           !err.str().empty();
  }
}

JUST_TEST_CASE(test_recognising_extra_clang_arg)
{
  const data::config cfg = parse_config({"--", "foo"}).cfg;

  JUST_ASSERT_EQUAL(1u, cfg.extra_clang_args.size());
  JUST_ASSERT_EQUAL("foo", cfg.extra_clang_args.front());
}

JUST_TEST_CASE(test_extra_clang_args_are_not_parsed)
{
  JUST_ASSERT(parse_config({"--", "foo"}).should_run_shell());
}

JUST_TEST_CASE(test_saving_is_enabled_by_default_during_parsing)
{
  const data::config cfg = parse_config({}).cfg;

  JUST_ASSERT(cfg.saving_enabled);
}

JUST_TEST_CASE(test_disabling_saving)
{
  const data::config cfg = parse_config({"--disable_saving"}).cfg;

  JUST_ASSERT(!cfg.saving_enabled);
}

JUST_TEST_CASE(test_default_console_type_is_readline)
{
  const data::config cfg = parse_config({}).cfg;

  JUST_ASSERT_EQUAL(data::console_type::readline, cfg.con_type);
}

JUST_TEST_CASE(test_setting_console_type_to_plain)
{
  const data::config cfg = parse_config({"--console", "plain"}).cfg;

  JUST_ASSERT_EQUAL(data::console_type::plain, cfg.con_type);
}

JUST_TEST_CASE(test_setting_console_type_to_readline)
{
  const data::config cfg = parse_config({"--console", "readline"}).cfg;

  JUST_ASSERT_EQUAL(data::console_type::readline, cfg.con_type);
}

JUST_TEST_CASE(test_setting_console_type_to_json)
{
  const data::config cfg = parse_config({"--console", "json"}).cfg;

  JUST_ASSERT_EQUAL(data::console_type::json, cfg.con_type);
}

JUST_TEST_CASE(test_splash_is_enabled_by_default)
{
  const data::config cfg = parse_config({}).cfg;

  JUST_ASSERT(cfg.splash_enabled);
}

JUST_TEST_CASE(test_disabling_splash)
{
  const data::config cfg = parse_config({"--nosplash"}).cfg;

  JUST_ASSERT(!cfg.splash_enabled);
}

JUST_TEST_CASE(test_logging_mode_is_none_by_default)
{
  const data::config cfg = parse_config({}).cfg;

  JUST_ASSERT_EQUAL(data::logging_mode::none, cfg.log_mode);
}

JUST_TEST_CASE(test_logging_to_console)
{
  const data::config cfg = parse_config({"--log", "-"}).cfg;

  JUST_ASSERT_EQUAL(data::logging_mode::console, cfg.log_mode);
}

JUST_TEST_CASE(test_logging_to_file)
{
  const data::config cfg = parse_config({"--log", "/tmp/foo.txt"}).cfg;

  JUST_ASSERT_EQUAL(data::logging_mode::file, cfg.log_mode);
  JUST_ASSERT_EQUAL("/tmp/foo.txt", cfg.log_file);
}

JUST_TEST_CASE(test_it_is_an_error_to_specify_log_twice)
{
  const parse_config_result r = parse_config({"--log", "-", "--log", "-"});

  JUST_ASSERT(!r.should_run_shell());
  JUST_ASSERT(r.should_error_at_exit());
}

JUST_TEST_CASE(test_decommissioned_arguments_provide_an_error_message)
{
  JUST_ASSERT(fails_and_displays_error({"-I/usr/include"}));
  JUST_ASSERT(fails_and_displays_error({"--include", "/usr/include"}));
  JUST_ASSERT(fails_and_displays_error({"-DFOO=bar"}));
  JUST_ASSERT(fails_and_displays_error({"--define", "FOO=bar"}));
  JUST_ASSERT(fails_and_displays_error({"--std", "c++11"}));
  JUST_ASSERT(fails_and_displays_error({"-w"}));
  JUST_ASSERT(fails_and_displays_error({"--no_warnings"}));
  JUST_ASSERT(fails_and_displays_error({"-ftemplate-depth=13"}));
  JUST_ASSERT(fails_and_displays_error({"-stdlib=libstdc++"}));
}

JUST_TEST_CASE(test_not_specifying_the_engine)
{
  const data::config cfg = parse_config({}).cfg;

  JUST_ASSERT_EQUAL("internal", cfg.engine);
}

JUST_TEST_CASE(test_specifying_the_engine)
{
  const data::config cfg = parse_config({"--engine", "foo"}).cfg;

  JUST_ASSERT_EQUAL("foo", cfg.engine);
}

JUST_TEST_CASE(test_metashell_path_is_filled)
{
  std::vector<const char*> args{"the_path"};
  mock_environment_detector env_detector;

  const metashell::data::config cfg =
      metashell::parse_config(args.size(), args.data(),
                              std::map<std::string, engine_entry>(),
                              env_detector, nullptr, nullptr)
          .cfg;

  JUST_ASSERT_EQUAL("the_path", cfg.metashell_binary);
}

JUST_TEST_CASE(test_preprocessor_mode_is_off_by_default)
{
  const metashell::data::config cfg = parse_config({}).cfg;

  JUST_ASSERT(!cfg.preprocessor_mode);
}

JUST_TEST_CASE(test_preprocessor_mode_is_set_from_command_line)
{
  const metashell::data::config cfg = parse_config({"--preprocessor"}).cfg;

  JUST_ASSERT(cfg.preprocessor_mode);
}
