#include <iostream>
#include <string>
#include <cstring>
#include <ranges>
#include <vector>
#include <filesystem>
#include <fstream>
#include <unistd.h>      // fork, execvp
#include <sys/wait.h>    // waitpid, WIFEXITED, etc.
#include <fcntl.h>

#include <readline/readline.h>

using std::string, std::vector;

namespace fs = std::filesystem;

constexpr auto pipe_in_filename = "/tmp/shell_pipe_in";
constexpr auto pipe_out_filename = "/tmp/shell_pipe_out";

class Command {
  std::ofstream out_file, err_file;
  std::ifstream in_file;
public:
  vector<string> args, args_trunc;
  string out_filename, err_filename, in_filename;
  bool out_append = false, err_append = false;
  // std::ostream *out_stream, *err_stream;
  // std::istream *in_stream;
  bool pipe_in, pipe_out;

  std::ostream& get_out_stream() {
    if (!out_filename.empty()) return out_file;
    return std::cout;
  }
  std::ostream& get_err_stream() {
    if (!err_filename.empty()) return err_file;
    return std::cerr;
  }
  std::istream& get_in_stream() {
    if (!in_filename.empty()) return in_file;
    return std::cin;
  }

  explicit Command(vector<string> args, const bool pipe_in=false, const bool pipe_out=false)
  : args(std::move(args)), pipe_in(pipe_in), pipe_out(pipe_out) {
    process();
  }

private:
  void process() {
    if (pipe_in) {
      in_filename = pipe_in_filename;
      in_file.open(in_filename, std::ios::in);
    }

    const auto redir_out_idx = std::min(
      std::ranges::find(args, ">"),
      std::ranges::find(args, "1>")
    );
    if (redir_out_idx < args.end() - 1)
      out_filename = *(redir_out_idx + 1);
    const auto redir_out_append_idx = std::min(
      std::ranges::find(args, ">>"),
      std::ranges::find(args, "1>>")
    );
    if (redir_out_append_idx < args.end() - 1) {
      out_filename = *(redir_out_append_idx + 1);
      out_append = true;
    }
    if (pipe_out && out_filename.empty())
      out_filename = pipe_out_filename;

    const auto redir_err_idx = std::ranges::find(args, "2>");
    if (redir_err_idx < args.end() - 1)
      err_filename = *(redir_err_idx + 1);
    const auto redir_err_append_idx = std::ranges::find(args, "2>>");
    if (redir_err_append_idx < args.end() - 1) {
      err_filename = *(redir_err_append_idx + 1);
      err_append = true;
    }

    if (!out_filename.empty()) {
      out_file.open(out_filename, out_append ? std::ios::app : std::ios::out);
      out_file << std::unitbuf;
    }
    if (!err_filename.empty()) {
      err_file.open(err_filename, err_append ? std::ios::app : std::ios::out);
      err_file << std::unitbuf;
    }

    auto end_idx = std::min(redir_out_idx, redir_out_append_idx);
    end_idx = std::min(end_idx, redir_err_idx);
    end_idx = std::min(end_idx, redir_err_append_idx);
    for (auto i = args.begin(); i < end_idx; ++i)
      args_trunc.emplace_back(*i);
  }
};

class ArgsParser {
  string args_str;
public:
  vector<string> args;
  vector<Command> pipeline;

private:
  void parse_args() {
    args.emplace_back("");
    auto it = args_str.begin();
    bool ongoing_single_quote = false;
    bool ongoing_double_quote = false;
    do {
      if (*it == '\\' && !ongoing_single_quote && !ongoing_double_quote) {
        args[args.size() - 1] += *(++it);
        ++it;
      }
      else if (*it == '\\' && ongoing_double_quote) {
        ++it;
        if (*it != '\"' && *it != '\\' && *it != '$' && *it != '`')
          args[args.size() - 1] += '\\';
        args[args.size() - 1] += *it;
        ++it;
      }
      else if (*it == '\'' && !ongoing_double_quote) {
        ongoing_single_quote = !ongoing_single_quote;
        ++it;
      }
      else if (*it == '"' && !ongoing_single_quote) {
        ongoing_double_quote = !ongoing_double_quote;
        ++it;
      }
      else if (*it == ' ' && !ongoing_single_quote && !ongoing_double_quote) {
        args.emplace_back("");
        while (it != args_str.end() && *it == ' ')
          ++it;
      }
      else {
        args[args.size() - 1] += *it;
        ++it;
      }
    } while (it != args_str.end());
  }
  void build_pipeline() {
    vector<vector<string>> pipeline_args;
    pipeline_args.emplace_back();
    for (const auto& arg_part: args) {
      if (arg_part == "|")
        pipeline_args.emplace_back();
      else
        pipeline_args.back().emplace_back(arg_part);
    }
    pipeline.reserve(pipeline_args.size());
    for (int i = 0; i < pipeline_args.size(); ++i) {
      pipeline.emplace_back(
        pipeline_args[i],
        i>0,
        i<pipeline_args.size()-1
      );
    }
  }
public:
  explicit ArgsParser(string args_str) : args_str(std::move(args_str)) {
    parse_args();
    build_pipeline();
  }
};

int run_external(Command& command) {
  pid_t pid = fork();
  if (pid < 0) {
    command.get_err_stream() << "fork failed\n";
    return 1;
  }
  if (pid == 0) {
    // 1) handle redirections
    if (!command.out_filename.empty()) {
      const int flags = O_WRONLY | O_CREAT | (command.out_append ? O_APPEND : O_TRUNC);
      const int fd = open(command.out_filename.c_str(), flags, 0666);
      if (fd < 0) {
        std::perror("open stdout");
        _exit(1);
      }
      if (dup2(fd, STDOUT_FILENO) < 0) {
        std::perror("dup2 stdout");
        _exit(1);
      }
      close(fd);
    }

    if (!command.err_filename.empty()) {
      const int flags = O_WRONLY | O_CREAT | (command.err_append ? O_APPEND : O_TRUNC);
      const int fd = open(command.err_filename.c_str(), flags, 0666);
      if (fd < 0) {
        std::perror("open stderr");
        _exit(1);
      }
      if (dup2(fd, STDERR_FILENO) < 0) {
        std::perror("dup2 stderr");
        _exit(1);
      }
      close(fd);
    }

    if (!command.in_filename.empty()) {
      const int fd = open(command.in_filename.c_str(), O_RDONLY);
      if (fd < 0) {
        std::perror("open stdin");
        _exit(1);
      }
      if (dup2(fd, STDIN_FILENO) < 0) {
        std::perror("dup2 stdin");
        _exit(1);
      }
      close(fd);
    }

    std::vector<char*> argv;
    argv.reserve(command.args_trunc.size() + 1);
    for (auto& s : command.args_trunc) {
      argv.push_back(const_cast<char*>(s.c_str()));
    }
    argv.push_back(nullptr);

    // use execvp so PATH is searched automatically
    execvp(argv[0], argv.data());

    // if we get here, exec failed
    std::perror("execvp");
    _exit(127); // POSIX convention for "command not found" / exec failed
  }
  // --- parent process ---
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    command.get_err_stream() << "waitpid failed\n";
    return 1;
  }

  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  if (WIFSIGNALED(status))
    return 128 + WTERMSIG(status);

  return 1;
}


vector<fs::directory_entry> find_all_exes() {
  vector<fs::directory_entry> result;

  const string path = std::getenv("PATH");
  vector<string> pathParts{""};
  for (const char pathChar: path) {
    if (pathChar == ':')
      pathParts.emplace_back("");
    else
      pathParts.back() += pathChar;
  }

  std::error_code ec;
  for (const auto& pathPart: pathParts) {
    if (!fs::exists(fs::path(pathPart)))
      continue;
    for (const auto & entry : fs::directory_iterator{pathPart}) {
      if (auto status = entry.status(ec); ec || (status.permissions() & fs::perms::owner_exec) == fs::perms::none) {
        ec.clear();
        continue;
      }
      result.emplace_back(entry);
    }
  }
  return result;
}

string find_exe(const string &stem) {
  for (const auto all_exes = find_all_exes(); const auto& entry : all_exes)
    if (entry.path().stem() == stem)
      return entry.path().string();
  return "";
}

void handle_echo(Command& command) {
  auto &os = command.get_out_stream();
  if (command.pipe_in) {
    string output;
    std::getline(command.get_in_stream(), output);
    os << output;
  }
  else {
    auto it = command.args_trunc.begin() + 1;
    while (it != command.args_trunc.end()) {
      os << *it << (it == command.args_trunc.end() - 1 ? "" : " ");
      ++it;
    }
  }
  os << '\n';
}

void handle_pwd(Command& command) {
  command.get_out_stream() << fs::current_path().string() << '\n';
}

void handle_cd(Command& args_parser) {
  string path_str = args_parser.args[1];
  if (const auto tilde_pos = path_str.find('~'); tilde_pos != string::npos) {
    const auto home_path_str = std::getenv("HOME");
    path_str = path_str.replace(tilde_pos, 1, home_path_str);
  }
  if (const fs::path cd_path(path_str); fs::exists(cd_path))
    fs::current_path(cd_path);
  else
    args_parser.get_err_stream() << "cd: " << path_str << ": No such file or directory\n";
}

void handle_type(Command& args_parser) {
  const string &arg = args_parser.args[1];
  if (arg == "exit" || arg == "echo" || arg == "type" || arg == "pwd") {
    args_parser.get_out_stream() << arg << " is a shell builtin\n";
    return;
  }

  if (const auto exe_path = find_exe(arg); exe_path.empty())
    args_parser.get_out_stream() << arg << ": not found\n";
  else
    args_parser.get_out_stream() << arg << " is " << exe_path << '\n';
}

static char* command_generator(const char* text, const int state) {
  static vector<string> commands;
  static vector<string>::iterator current_command;
  static size_t len;

  if (state == 0) {
    commands = {"echo", "exit", "type", "pwd"};
    for (const auto all_exes = find_all_exes(); const auto& exe : all_exes)
      if (std::ranges::find(commands, exe.path().stem().string()) == commands.end())
        commands.emplace_back(exe.path().stem());

    current_command = commands.begin();
    len = std::strlen(text);
  }

  vector<string>::iterator name;
  while ((name = current_command++) != commands.end())
    if (strncmp(name->c_str(), text, len) == 0)
      return strdup(name->c_str());

  return nullptr;
}

char** character_name_completion(const char* text, const int start, const int end) {
  (void)start;
  (void)end;

  rl_attempted_completion_over = 1;
  return rl_completion_matches(text, command_generator);
}

int main() {
  rl_attempted_completion_function = character_name_completion;

  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  while (true) {
    for (ArgsParser args_parser(readline("$ ")); auto& command: args_parser.pipeline) {
      if (command.args_trunc[0] == "exit") {
        int exit_status = 0;
        if (command.args_trunc.size() > 1)
          exit_status = std::stoi(command.args_trunc[1]);
        return exit_status;
      }

      if (command.args_trunc[0] == "pwd")
        handle_pwd(command);
      else if (command.args_trunc[0] == "cd")
        handle_cd(command);
      else if (command.args_trunc[0] == "echo")
        handle_echo(command);
      else if (command.args_trunc[0] == "type")
        handle_type(command);
      else if (!find_exe(command.args_trunc[0]).empty()) {
        run_external(command);
      }
      else
        command.get_out_stream() << command.args[0] << ": command not found\n";

      if (fs::exists(pipe_out_filename))
        fs::rename(pipe_out_filename, pipe_in_filename);
    }
  }
}