#include <iostream>
#include <string>
#include <cstring>
#include <ranges>
#include <utility>
#include <vector>
#include <filesystem>
#include <fstream>
#include <unistd.h>      // fork, execvp
#include <sys/wait.h>    // waitpid, WIFEXITED, etc.
#include <fcntl.h>
#include <tuple>
#include <algorithm>

#include <readline/readline.h>

using std::string, std::vector;

namespace fs = std::filesystem;

class Command {
public:
  vector<string> args, args_trunc;
  int in_fd = STDIN_FILENO, out_fd = STDOUT_FILENO, err_fd = STDERR_FILENO;
  int pipe_in_fd = -1, pipe_out_fd = -1;

  explicit Command(vector<string> args, const int pipe_in_fd=-1, const int pipe_out_fd=-1)
  : args(std::move(args)), pipe_in_fd(pipe_in_fd), pipe_out_fd(pipe_out_fd) {
    process();
  }

  void close_all_fds() noexcept {
    // Collect them in a temporary array.
    int fds[] = { in_fd, out_fd, err_fd, pipe_in_fd, pipe_out_fd };

    std::ranges::sort(fds);

    int last = -1;
    for (const int fd : fds) {
      if (fd < 0)          continue;        // invalid
      if (fd <= STDERR_FILENO) continue;    // 0,1,2 → leave them alone
      if (fd == last)      continue;        // avoid double close
      if (close(fd) == -1)
        std::perror("File close error");
      last = fd;
    }

    in_fd = out_fd = err_fd = pipe_in_fd = pipe_out_fd = -1;
  }

private:
  void process() {
    if (pipe_in_fd != -1) {
      in_fd = pipe_in_fd;
    }

    auto redir_idx = std::min(
      std::ranges::find(args, ">"),
      std::ranges::find(args, "1>")
    );
    auto min_redir_idx = redir_idx;
    if (redir_idx < args.end() - 1) {
      constexpr int flags = O_WRONLY | O_CREAT | O_TRUNC;
      out_fd = open((redir_idx + 1)->c_str(), flags, 0666);
    }
    redir_idx = std::min(
      std::ranges::find(args, ">>"),
      std::ranges::find(args, "1>>")
    );
    min_redir_idx = std::min(min_redir_idx, redir_idx);
    if (redir_idx < args.end() - 1) {
      constexpr int flags = O_WRONLY | O_CREAT | O_APPEND;
      out_fd = open((redir_idx + 1)->c_str(), flags, 0666);
    }
    if (pipe_out_fd != -1 && out_fd == 1)
      out_fd = pipe_out_fd;

    redir_idx = std::ranges::find(args, "2>");
    min_redir_idx = std::min(min_redir_idx, redir_idx);
    if (redir_idx < args.end() - 1) {
      constexpr int flags = O_WRONLY | O_CREAT | O_TRUNC;
      err_fd = open((redir_idx + 1)->c_str(), flags, 0666);
    }
    redir_idx = std::ranges::find(args, "2>>");
    min_redir_idx = std::min(min_redir_idx, redir_idx);
    if (redir_idx < args.end() - 1) {
      constexpr int flags = O_WRONLY | O_CREAT | O_APPEND;
      err_fd = open((redir_idx + 1)->c_str(), flags, 0666);
    }

    for (auto i = args.begin(); i < min_redir_idx; ++i)
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
    do {  // TODO: if args are empty this results to segfault
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

    vector<std::tuple<int, int>> fds_pipes;
    for (int i = 0; i < pipeline_args.size() - 1; i++) {
      int fds[2];
      pipe(fds);
      fds_pipes.emplace_back(fds[0], fds[1]);
    }
    // close(std::get<0>(fds_pipes.front()));
    // close(std::get<1>(fds_pipes.back()));

    pipeline.reserve(pipeline_args.size());
    for (int i = 0; i < pipeline_args.size(); ++i) {
      pipeline.emplace_back(
        pipeline_args[i],
        i > 0 ? std::get<0>(fds_pipes[i-1]) : -1,
        i < pipeline_args.size() - 1 ? std::get<1>(fds_pipes[i]) : -1
      );
    }
  }
public:
  explicit ArgsParser(string args_str) : args_str(std::move(args_str)) {
    parse_args();
    build_pipeline();
  }
};

int setup_stdio(const Command &command) {
  if (command.in_fd != STDIN_FILENO && dup2(command.in_fd, STDIN_FILENO) < 0) {
    std::perror("dup2 stdin");
    return -1;
  }
  if (command.out_fd != STDOUT_FILENO && dup2(command.out_fd, STDOUT_FILENO) < 0) {
    std::perror("dup2 stdout");
    return -1;
  }
  if (command.err_fd != STDERR_FILENO && dup2(command.err_fd, STDERR_FILENO) < 0) {
    std::perror("dup2 stderr");
    return -1;
  }
  return 0;
}

int run_external(Command& command) {
  setup_stdio(command);
  command.close_all_fds();

  std::vector<char*> argv;
  argv.reserve(command.args_trunc.size() + 1);
  for (auto& s : command.args_trunc) {
    argv.push_back(const_cast<char*>(s.c_str()));
  }
  argv.push_back(nullptr);

  // use execvp so PATH is searched automatically
  execvp(argv[0], argv.data());

  // if we get here, exec failed
  // std::perror("execvp");
  std::cerr << command.args[0] << ": command not found\n";
  _exit(127); // POSIX convention for "command not found" / exec failed
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
  setup_stdio(command);

  if (command.in_fd != STDIN_FILENO) {
    command.close_all_fds();
    string output;
    std::getline(std::cin, output);
    std::cout << output;
  }
  else {
    command.close_all_fds();
    auto it = command.args_trunc.begin() + 1;
    while (it != command.args_trunc.end()) {
      std::cout << *it << (it == command.args_trunc.end() - 1 ? "" : " ");
      ++it;
    }
  }
  std::cout << '\n';
}

void handle_pwd(Command& command) {
  setup_stdio(command);
  command.close_all_fds();
  std::cout << fs::current_path().string() << '\n';
}

void handle_cd(const Command& command) {
  string path_str = command.args[1];
  if (const auto tilde_pos = path_str.find('~'); tilde_pos != string::npos) {
    const auto home_path_str = std::getenv("HOME");
    path_str = path_str.replace(tilde_pos, 1, home_path_str);
  }
  if (const fs::path cd_path(path_str); fs::exists(cd_path))
    fs::current_path(cd_path);
  else
    std::cerr << "cd: " << path_str << ": No such file or directory\n";
}

void handle_type(Command& command) {
  setup_stdio(command);
  command.close_all_fds();

  const string &arg = command.args[1];
  // TODO: do the following in a more structured way
  if (arg == "exit" || arg == "echo" || arg == "type" || arg == "pwd" || arg == "history") {
    std::cout << arg << " is a shell builtin\n";
    return;
  }

  if (const auto exe_path = find_exe(arg); exe_path.empty())
    std::cout << arg << ": not found\n";
  else
    std::cout << arg << " is " << exe_path << '\n';
}

void handle_history(Command& command, const vector<string>& history) {
  setup_stdio(command);
  command.close_all_fds();

  auto last_n = history.size();
  if (command.args_trunc.size() > 1) {
    if (int arg; (arg = std::stoi(command.args_trunc[1]))) {
      last_n = arg;
    }
  }

  auto index = history.size() - last_n + 1;
  for (const auto& elem: history | std::views::drop(history.size() - last_n)) {
    std::cout << "    " << index++ << "  " <<  elem << '\n';
  }
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

  vector<string> history;

  while (true) {
    char* line = readline("$ ");
    if (!line) break; // EOF

    if (*line == '\0') { free(line); continue; }

    history.emplace_back(line);

    ArgsParser args_parser(line);
    free(line);

    vector<pid_t> pids_to_wait;
    for (auto& command: args_parser.pipeline) {
      if (command.args_trunc[0] == "exit") {
        int exit_status = 0;
        if (command.args_trunc.size() > 1)
          exit_status = std::stoi(command.args_trunc[1]);
        return exit_status;
      }
      if (command.args_trunc[0] == "cd") {
        handle_cd(command);
        continue;
      }

      pid_t pid = fork();
      if (pid < 0) std::cerr << "failed to fork\n";
      if (pid > 0) {
        pids_to_wait.emplace_back(pid);
        command.close_all_fds();
        continue;
      }

      if (command.args_trunc[0] == "pwd")
        handle_pwd(command);
      else if (command.args_trunc[0] == "echo")
        handle_echo(command);
      else if (command.args_trunc[0] == "history")
        handle_history(command, history);
      else if (command.args_trunc[0] == "type")
        handle_type(command);
      else
        run_external(command);

      return 0;
    }
    // wait for all pids to finish
    for (const auto curr_pid : pids_to_wait) {
      int status = 0;
      waitpid(curr_pid, &status, 0);
      if (status < 0) {
        std::cerr << "something run with process pipelining";
        return 1;
      }
    }
  }
}