#include "wtr/watcher.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <thread>

using namespace wtr;
using namespace std;
using namespace chrono;
namespace fs = filesystem;

// clang-format off

struct Args {
  static constexpr auto usage =
    "wtr.watcher <PATH=. [-UNIT <TIME>]>\n"
    "wtr.watcher <-h | --help>\n"
    "\n"
    "  PATH\n"
    "    Any path. Relative or absolute.\n"
    "    Defaults to the current directory.\n"
    "    If the given path doesn't exist,\n"
    "    the current directory is used.\n"
    "\n"
    "  UNIT\n"
    "    One of:\n"
    "    -nanoseconds,  -ns,\n"
    "    -microseconds, -us,\n"
    "    -milliseconds, -ms,\n"
    "    -seconds,      -s,\n"
    "    -minutes,      -m,\n"
    "    -hours,        -h,\n"
    "    -days,         -d,\n"
    "    -weeks,        -w,\n"
    "    -months,       -mts,\n"
    "    -years,        -y\n"
    "\n"
    "  TIME\n"
    "    Any positive integer, as long as it's\n"
    "    less than ULONG_MAX. Which is large.\n";
  optional<char const*> help;
  optional<fs::path> path;
  optional<nanoseconds> time;
  static auto try_parse(int argc, char const* const* const argv) -> optional<Args>
  {
    auto argis = [&](auto const i, char const* a)
    { return argc > i ? strcmp(a, argv[i]) == 0 : false; };

    auto help = [&]() -> optional<char const*> {
      if (argis(1, "-h") || argis(1, "--help")) return usage;
      else return nullopt;
    }();

    auto path = [&]() -> optional<fs::path>
    {
      if (help.has_value()) return nullopt;
      else if (argc < 2) return fs::current_path();
      else if (fs::exists(argv[1])) return fs::path(argv[1]);
      else return nullopt;
    }();

    auto time = [&]() -> optional<nanoseconds> {
      char const* st = argc > 3 ? argv[3] : "0";
      char const* stend = st + strlen(st);
      auto targis = [&](char const* a) { return argis(2, a); };
      auto ttons =
          targis("-nanoseconds")  || targis("-ns")  ?                      1e0
        : targis("-microseconds") || targis("-us")  ?                      1e3
        : targis("-milliseconds") || targis("-ms")  ?                      1e6
        : targis("-seconds")      || targis("-s")   ?                      1e9
        : targis("-minutes")      || targis("-m")   ?                 60 * 1e9
        : targis("-hours")        || targis("-h")   ?            60 * 60 * 1e9
        : targis("-days")         || targis("-d")   ?       24 * 60 * 60 * 1e9
        : targis("-weeks")        || targis("-w")   ? 7   * 24 * 60 * 60 * 1e9
        : targis("-months")       || targis("-mts") ? 30  * 24 * 60 * 60 * 1e9
        : targis("-years")        || targis("-y")   ? 365 * 24 * 60 * 60 * 1e9
                                                    :                      1e6;
      auto td = strtod(st, (char**)&stend);
      if (help.has_value()
          || td == HUGE_VAL
          || td < 0)
        return nullopt;
      else
        return nanoseconds(llroundl(td * ttons));
    }();

    if (help.has_value() || path.has_value() || time.has_value())
      return Args{help, path, time};
    else
      return nullopt;
  }
};

auto watch_for = [](auto path, auto time) -> bool
{
  auto w = watch(path, [](auto ev) { cout << "{" << ev << "}" << endl; });
  if (! time.has_value()) cin.get();
  else this_thread::sleep_for(time.value());
  return w.close();
};

/*  Watch a path for some time.
    Or watch a path forever.
    Show what happens, or show help. */
int main(int const argc, char const** const argv)
{
  auto args = Args::try_parse(argc, argv);
  return ! args.has_value() ? (cerr << Args::usage, 1)
       : args->help.has_value() ? (cout << Args::usage, 0)
       : ! args->path.has_value() ? (cerr << Args::usage, 1)
       : watch_for(args->path.value(), args->time);
};

// clang-format on
