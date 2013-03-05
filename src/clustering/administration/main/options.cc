// Copyright 2010-2013 RethinkDB, all rights reserved.
#include "clustering/administration/main/options.hpp"

#include "errors.hpp"
#include "utils.hpp"

namespace options {

option_t::option_t(const names_t _names, const appearance_t appearance)
    : names(_names.names),
      default_values() {
    switch (appearance) {
    case MANDATORY:
        min_appearances = 1;
        max_appearances = 1;
        no_parameter = false;
        break;
    case MANDATORY_REPEAT:
        min_appearances = 1;
        max_appearances = SIZE_MAX;
        no_parameter = false;
        break;
    case OPTIONAL:
        min_appearances = 0;
        max_appearances = 1;
        no_parameter = false;
        break;
    case OPTIONAL_REPEAT:
        min_appearances = 0;
        max_appearances = SIZE_MAX;
        no_parameter = false;
        break;
    case OPTIONAL_NO_PARAMETER:
        min_appearances = 0;
        max_appearances = 1;
        no_parameter = true;
        break;
    default:
        unreachable();
    }
}


option_t::option_t(const names_t _names, const appearance_t appearance, std::string default_value)
    : names(_names.names),
      default_values(1, default_value) {
    switch (appearance) {
    case OPTIONAL:
        min_appearances = 0;
        max_appearances = 1;
        no_parameter = false;
        break;
    case OPTIONAL_REPEAT:
        min_appearances = 0;
        max_appearances = SIZE_MAX;
        no_parameter = false;
        break;
    case MANDATORY:
    case MANDATORY_REPEAT:
    case OPTIONAL_NO_PARAMETER:
        // fall through
    default:
        unreachable();
    }
}

bool looks_like_option_name(const char *const str) {
    return str[0] == '-';
}

const option_t *find_option(const char *const option_name, const std::vector<option_t> &options) {
    for (auto it = options.begin(); it != options.end(); ++it) {
        for (auto name_it = it->names.begin(); name_it != it->names.end(); ++name_it) {
            if (*name_it == option_name) {
                return &*it;
            }
        }
    }
    return NULL;
}

void do_parse_command_line(const int argc, const char *const *const argv, const std::vector<option_t> &options,
                           std::vector<std::string> *const unrecognized_out,
                           std::map<std::string, std::vector<std::string> > *const names_by_values_out) {
    guarantee(argc >= 0);

    std::map<std::string, std::vector<std::string> > names_by_values;
    std::vector<std::string> unrecognized;

    for (int i = 0; i < argc; ) {
        // The option name as seen _in the command line_.  We output this in
        // help messages (because it's what the user typed in) instead of the
        // official name for the option.
        const char *const option_name = argv[i];
        ++i;

        const option_t *const option = find_option(option_name, options);
        if (!option) {
            if (unrecognized_out != NULL) {
                unrecognized.push_back(option_name);
                continue;
            } else if (looks_like_option_name(option_name)) {
                throw parse_error_t(strprintf("unrecognized option '%s'", option_name));
            } else {
                throw parse_error_t(strprintf("unexpected unnamed value '%s' (did you forget "
                                              "the option name, or forget to quote a parameter list?)",
                                              option_name));
            }
        }

        const std::string official_name = option->names[0];

        std::vector<std::string> *const option_parameters = &names_by_values[official_name];
        if (option_parameters->size() == static_cast<size_t>(option->max_appearances)) {
            throw parse_error_t(strprintf("option '%s' appears too many times (i.e. more than %zu times)",
                                          option_name, option->max_appearances));
        }

        if (option->no_parameter) {
            // Push an empty parameter value -- in particular, this makes our
            // duplicate checking work.
            option_parameters->push_back("");
        } else {
            if (i == argc) {
                throw parse_error_t(strprintf("option '%s' is missing its parameter", option_name));
            }

            const char *const option_parameter = argv[i];
            ++i;

            if (looks_like_option_name(option_parameter)) {
                throw parse_error_t(strprintf("option '%s' is missing its parameter (because '%s' looks like another option name)", option_name, option_parameter));
            }

            option_parameters->push_back(option_parameter);
        }
    }

    // For all options, insert the default value into the map if it does not already exist.
    for (auto it = options.begin(); it != options.end(); ++it) {
        if (it->min_appearances == 0) {
            names_by_values.insert(std::make_pair(it->names[0], it->default_values));
        }
    }

    names_by_values_out->swap(names_by_values);
    if (unrecognized_out != NULL) {
        unrecognized_out->swap(unrecognized);
    }
}

void parse_command_line(const int argc, const char *const *const argv, const std::vector<option_t> &options,
                        std::map<std::string, std::vector<std::string> > *const names_by_values_out) {
    do_parse_command_line(argc, argv, options, NULL, names_by_values_out);
}

void parse_command_line_and_collect_unrecognized(int argc, const char *const *argv, const std::vector<option_t> &options,
                                                 std::vector<std::string> *unrecognized_out,
                                                 std::map<std::string, std::vector<std::string> > *names_by_values_out) {
    // We check that unrecognized_out is not NULL because do_parse_command_line
    // throws some exceptions depending on the nullness of that value.
    guarantee(unrecognized_out != NULL);
    do_parse_command_line(argc, argv, options, unrecognized_out, names_by_values_out);
}

std::vector<std::string> split_by_spaces(const std::string &s) {
    std::vector<std::string> ret;

    auto it = s.begin();
    const auto end = s.end();

    for (;;) {
        while (it != end && isspace(*it)) {
            ++it;
        }

        if (it == end) {
            return ret;
        }

        auto jt = it;
        while (jt != end && !isspace(*jt)) {
            ++jt;
        }

        ret.push_back(std::string(it, jt));
        it = jt;
    }

    return ret;
}

std::vector<std::string> word_wrap(const std::string &s, const size_t width) {
    const std::vector<std::string> words = split_by_spaces(s);

    std::vector<std::string> ret;

    std::string current_line;

    for (auto it = words.begin(); it != words.end(); ++it) {
        if (current_line.empty()) {
            current_line = *it;
        } else {
            if (current_line.size() + 1 + it->size() <= width) {
                current_line += ' ';
                current_line += *it;
            } else {
                ret.push_back(current_line);
                current_line = *it;
            }
        }
    }

    // If words.empty(), then current_line == "" and we want one empty line returned.
    // If !words.empty(), then current_line != "" and it's worth pushing.
    ret.push_back(current_line);

    return ret;
}

std::string format_help(const std::vector<help_section_t> &help) {
    size_t max_syntax_description_length = 0;
    for (auto section = help.begin(); section != help.end(); ++section) {
        for (auto line = section->help_lines.begin(); line != section->help_lines.end(); ++line) {
            max_syntax_description_length = std::max(max_syntax_description_length,
                                                     line->syntax_description.size());
        }
    }

    const size_t summary_width = std::max<ssize_t>(30, 79 - static_cast<ssize_t>(max_syntax_description_length));

    // Two spaces before summary description, two spaces after.  2 + 2 = 4.
    const size_t indent_width = 4 + max_syntax_description_length;

    std::string ret;
    for (auto section = help.begin(); section != help.end(); ++section) {
        ret += section->section_name;
        ret += ":\n";

        for (auto line = section->help_lines.begin(); line != section->help_lines.end(); ++line) {
            std::vector<std::string> parts = word_wrap(line->blurb, summary_width);

            for (size_t i = 0; i < parts.size(); ++i) {
                if (i == 0) {
                    ret += "  ";  // 2 spaces
                    ret += line->syntax_description;
                    ret += std::string(indent_width - (2 + line->syntax_description.size()), ' ');
                } else {
                    ret += std::string(indent_width, ' ');
                }

                ret += parts[i];
                ret += "\n";
            }
        }
        ret += "\n";
    }

    return ret;
}



}  // namespace options
