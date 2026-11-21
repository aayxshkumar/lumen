// lumen: run a program, evaluate a snippet, or start a REPL.
//
//   lumen FILE.lm            run a file
//   lumen -e 'print(1 + 2);' run a snippet
//   lumen                    REPL
//   lumen --dump-ast FILE    print the parse tree
//   lumen --stats FILE       run, then print call and live-object counts
//
// Exit codes: 0 ok, 65 compile error, 70 runtime error, 64 bad usage,
// 66 unreadable file (the sysexits.h values).

#include <pthread.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "lumen/interpreter.hpp"

using namespace lumen;

namespace {

// Prints "file:line:col: error: msg", the source line and a caret.
void report(const std::string& file, const std::string& src, Span at, const char* kind,
            const std::string& msg) {
  std::cerr << file << ":" << at.line << ":" << at.col << ": " << kind << ": " << msg << "\n";
  std::istringstream lines(src);
  std::string line;
  for (int i = 1; std::getline(lines, line); ++i) {
    if (i != at.line) continue;
    std::cerr << "  " << line << "\n  " << std::string(static_cast<std::size_t>(std::max(0, at.col - 1)), ' ')
              << "^\n";
    break;
  }
}

struct Job {
  std::string file;
  std::string source;
  bool dump_ast = false;
  bool stats = false;
  int exit_code = 0;
};

int run_source(Job& job) {
  Program program;
  try {
    program = parse(job.source);
  } catch (const CompileError& e) {
    report(job.file, job.source, e.span, "error", e.what());
    return 65;
  }
  if (job.dump_ast) {
    std::cout << dump(program);
    return 0;
  }
  Interpreter in(std::cout);
  int code = 0;
  try {
    in.run(program);
  } catch (const RuntimeError& e) {
    std::cout.flush();
    report(job.file, job.source, e.span, "runtime error", e.what());
    for (const auto& t : e.trace) std::cerr << "  " << t << "\n";
    code = 70;
  }
  std::cout.flush();
  if (job.stats) {
    // Globals are about to be dropped; whatever is still alive after that
    // is held only by reference cycles.
    const std::uint64_t calls = in.calls;
    in.globals.reset();
    std::cerr << "stats: " << calls << " calls; leaked after exit: " << HeapStats::envs << " envs, "
              << HeapStats::closures << " closures, " << HeapStats::lists << " lists, " << HeapStats::maps
              << " maps\n";
  }
  return code;
}

void* thread_main(void* p) {
  auto* job = static_cast<Job*>(p);
  job->exit_code = run_source(*job);
  return nullptr;
}

// The tree-walker recurses on the C++ stack, so give it plenty.
int run_on_big_stack(Job& job) {
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 1024ull * 1024 * 1024);
  pthread_t t;
  if (pthread_create(&t, &attr, thread_main, &job) != 0) return run_source(job);
  pthread_join(t, nullptr);
  pthread_attr_destroy(&attr);
  return job.exit_code;
}

int repl() {
  Interpreter in(std::cout);
  std::vector<Program> keep;  // closures point into earlier inputs
  std::string line, buffer;
  const bool tty = isatty(STDIN_FILENO);
  if (tty) std::cout << "lumen REPL. Ctrl-D to exit.\n";
  while (true) {
    if (tty) std::cout << (buffer.empty() ? "> " : ". ") << std::flush;
    if (!std::getline(std::cin, line)) break;
    buffer += line + "\n";
    Program p;
    try {
      p = parse(buffer);
    } catch (const CompileError& e) {
      if (std::string(e.what()).find("found end of input") == std::string::npos) {
        report("<repl>", buffer, e.span, "error", e.what());
        buffer.clear();
        continue;
      }
      // Either a bare expression without ';' (print its value) or input
      // that isn't finished yet (keep reading).
      try {
        p = parse(buffer + ";");
      } catch (const CompileError&) {
        continue;
      }
    }
    try {
      const Value v = in.run_repl(p);
      if (!v.is_nil()) std::cout << to_repr(v) << "\n";
    } catch (const RuntimeError& e) {
      report("<repl>", buffer, e.span, "runtime error", e.what());
    }
    keep.push_back(std::move(p));
    buffer.clear();
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  Job job;
  std::string file;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "-e" && i + 1 < args.size()) {
      job.file = "<-e>";
      job.source = args[++i];
    } else if (args[i] == "--dump-ast") {
      job.dump_ast = true;
    } else if (args[i] == "--stats") {
      job.stats = true;
    } else if (args[i] == "-h" || args[i] == "--help") {
      std::cout << "usage: lumen [--dump-ast] [--stats] [FILE | -e CODE]\n";
      return 0;
    } else if (!args[i].empty() && args[i][0] == '-') {
      std::cerr << "unknown option " << args[i] << "\n";
      return 64;
    } else {
      file = args[i];
    }
  }
  if (!file.empty()) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
      std::cerr << "can't read " << file << "\n";
      return 66;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    job.file = file;
    job.source = ss.str();
  }
  if (job.file.empty()) return repl();
  return run_on_big_stack(job);
}
