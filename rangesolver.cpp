#include <stdio.h>
#include <unistd.h>
#include <chrono>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>

const int FRAME_WIDTH = 45;
const double PRINT_DELAY = 3.0;
const long long CLOCK_CHECK_INTERVAL = 1024; // only query the clock every this many nodes

auto g_start_time = std::chrono::steady_clock::now();
auto g_last_print_time = g_start_time;
long long g_solutions_found = 0;
long long g_node_count = 0;
std::string g_log_filename = "Solutions/soln_log_range.txt";
bool g_inplace_status = false;   // overwrite each status line instead of scrolling
bool g_printed_status = false;   // whether any status line has been printed yet

class Grid
{
public:
  bool is_occupied[FRAME_WIDTH][FRAME_WIDTH];
  int occ_size[FRAME_WIDTH][FRAME_WIDTH];
  int occ_offset_x[FRAME_WIDTH][FRAME_WIDTH];
  int occ_offset_y[FRAME_WIDTH][FRAME_WIDTH];

  void init_blank() {
    for (int x = 0; x < FRAME_WIDTH; ++x) {
      for (int y = 0; y < FRAME_WIDTH; ++y) {
        is_occupied[x][y] = false;
        occ_size[x][y] = 0;
        occ_offset_x[x][y] = 0;
        occ_offset_y[x][y] = 0;
      }
    }
  }

  bool is_location_occupied(int x, int y) {
    return is_occupied[x][y];
  }

  bool can_place_piece_at_location(int size, int x, int y) {
    if ((x+size > FRAME_WIDTH) || (y+size > FRAME_WIDTH)) {
      return false;
    }
    for (int x_off = 0; x_off < size; ++x_off) {
      for (int y_off = 0; y_off < size; ++y_off) {
        if (is_location_occupied(x + x_off, y + y_off)) {
          return false;
        }
      }
    }
    return true;
  }

  // Resumes the raster scan from (hint_x, hint_y) instead of restarting at (0,0). Safe because
  // a piece is always placed at-or-after the previous free location in raster order, so nothing
  // before that point can become free again until backtracking undoes it.
  bool get_first_free_loc(int hint_x, int hint_y, int &out_x, int &out_y) {
    for (int x = hint_x; x < FRAME_WIDTH; ++x) {
      if (!is_location_occupied(x, hint_y)) {
        out_x = x;
        out_y = hint_y;
        return true;
      }
    }
    for (int y = hint_y + 1; y < FRAME_WIDTH; ++y) {
      for (int x = 0; x < FRAME_WIDTH; ++x) {
        if (!is_location_occupied(x, y)) {
          out_x = x;
          out_y = y;
          return true;
        }
      }
    }
    return false;
  }

  void insert_piece_at_location(int sz, int x, int y) {
    for (int x_off = 0; x_off < sz; ++x_off) {
      for (int y_off = 0; y_off < sz; ++y_off) {
        int cx = x + x_off;
        int cy = y + y_off;
        is_occupied[cx][cy] = true;
        occ_size[cx][cy] = sz;
        occ_offset_x[cx][cy] = x_off;
        occ_offset_y[cx][cy] = y_off;
      }
    }
  }

  void remove_piece_at_location(int sz, int x, int y) {
    for (int x_off = 0; x_off < sz; ++x_off) {
      for (int y_off = 0; y_off < sz; ++y_off) {
        is_occupied[x + x_off][y + y_off] = false;
      }
    }
  }

  void print() {
    for (int y = 0; y < FRAME_WIDTH; ++y) {
      char line[FRAME_WIDTH + 1];
      for (int x = 0; x < FRAME_WIDTH; ++x) {
        if (!is_occupied[x][y]) {
          line[x] = '.';
        } else if (occ_offset_x[x][y] == 0) {
          line[x] = (occ_offset_y[x][y] == 0) ? '+' : '|';
        } else {
          line[x] = (occ_offset_y[x][y] == 0) ? '-' : ('0' + occ_size[x][y]);
        }
      }
      line[FRAME_WIDTH] = '\0';
      printf("%s\n", line);
    }
    printf("---\n");
  }

  // Writes to an already-open file handle rather than opening/closing one per call, since this
  // runs once per solution found and re-opening the file each time is wasted syscall overhead
  // once solutions start coming frequently.
  void log_to_file(FILE* f) {
    for (int y = 0; y < FRAME_WIDTH; ++y) {
      char line[FRAME_WIDTH + 1];
      for (int x = 0; x < FRAME_WIDTH; ++x) {
        if (!is_occupied[x][y]) {
          line[x] = '.';
        } else if (occ_offset_x[x][y] == 0) {
          line[x] = (occ_offset_y[x][y] == 0) ? '+' : '|';
        } else {
          line[x] = (occ_offset_y[x][y] == 0) ? '-' : ('0' + occ_size[x][y]);
        }
      }
      line[FRAME_WIDTH] = '\0';
      fprintf(f, "%s\n", line);
    }
    fprintf(f, "---\n\n");
    fflush(f); // keep the file readable in real time (e.g. by `tail -f`) despite staying open
  }
};

Grid g_grid;
int g_path[FRAME_WIDTH];
FILE* g_log_file = nullptr;

class PieceSet
{
public:
  int count[10];

  void init_full() {
    for (int sz = 0; sz < 10; ++sz) {
      count[sz] = sz;
    }
  }

  bool is_empty() {
    for (int sz = 1; sz < 10; ++sz) {
      if (count[sz] > 0) {
        return false;
      }
    }
    return true;
  }
};

std::string path_to_string(int depth) {
  std::string s;
  s.reserve(depth);
  for (int i = 0; i < depth; ++i) {
    s += char('0' + g_path[i]);
  }
  return s;
}

void print_progress_if_due(int depth) {
  // Querying the clock on every single node is wasteful given how often this is called; only
  // do it once every CLOCK_CHECK_INTERVAL nodes, and rely on that batch of nodes being fast
  // relative to PRINT_DELAY so the status line still fires close to on schedule.
  ++g_node_count;
  if (g_node_count % CLOCK_CHECK_INTERVAL != 0) {
    return;
  }

  auto now = std::chrono::steady_clock::now();
  std::chrono::duration<double> since_last = now - g_last_print_time;
  if (since_last.count() > PRINT_DELAY) {
    std::chrono::duration<double> elapsed = now - g_start_time;
    std::string line;
    char header[64];
    snprintf(header, sizeof(header), "[t=%.1fs] depth=%2d solutions=%lld path=",
              elapsed.count(), depth, g_solutions_found);
    line = std::string(header) + path_to_string(depth);

    if (g_inplace_status) {
      // \r returns to column 0; \033[K then clears to end of line so a shorter line doesn't
      // leave trailing characters from a longer previous one.
      printf("\r\033[K%s", line.c_str());
    } else {
      printf("%s\n", line.c_str());
    }
    fflush(stdout);
    g_printed_status = true;
    g_last_print_time = now;
  }
}

// Parses a string of digits '1'-'9' into a vector of tile sizes. Returns false on invalid input.
bool parse_digit_string(const std::string& s, std::vector<int>& out) {
  if (s.size() > FRAME_WIDTH) {
    return false;
  }
  out.clear();
  for (char c : s) {
    if (c < '1' || c > '9') {
      return false;
    }
    out.push_back(c - '0');
  }
  return true;
}

// True if `start` is strictly after `end` in the solver's search order (largest tile first at
// each position), meaning the requested [start, end] range is empty. A prefix relationship
// between the two (one contains the other) is not considered "after".
bool start_after_end(const std::vector<int>& start, const std::vector<int>& end) {
  size_t n = std::min(start.size(), end.size());
  for (size_t i = 0; i < n; ++i) {
    int rank_start = 9 - start[i];
    int rank_end = 9 - end[i];
    if (rank_start != rank_end) {
      return rank_start > rank_end;
    }
  }
  return false;
}

// Backtracking search identical to CppSolver/solver.cpp (largest remaining tile first, into the
// first free cell in raster order), except restricted to the slice of the search tree whose path
// of chosen tile sizes falls between `start_digits` and `end_digits` (both inclusive, and both
// optionally shorter than 45 digits, in which case they denote an entire subtree rather than a
// single leaf). This lets independent runs cover disjoint prefix ranges in parallel, or resume a
// run from a specific point.
void full_solver(PieceSet* remain, int depth, bool lower_tight, bool upper_tight,
                  const std::vector<int>& start_digits, const std::vector<int>& end_digits,
                  int hint_x, int hint_y) {
  print_progress_if_due(depth);

  if (remain->is_empty()) {
    ++g_solutions_found;
    g_grid.log_to_file(g_log_file);
    return;
  }

  // Once we've matched a bound's prefix exactly and gone deeper, we're entirely inside that
  // bound's subtree, so the bound is satisfied and no longer restricts anything below here.
  if (lower_tight && depth >= (int)start_digits.size()) {
    lower_tight = false;
  }
  if (upper_tight && depth >= (int)end_digits.size()) {
    upper_tight = false;
  }

  int free_x, free_y;
  g_grid.get_first_free_loc(hint_x, hint_y, free_x, free_y);

  int lower_digit = lower_tight ? start_digits[depth] : 0;
  int upper_digit = upper_tight ? end_digits[depth] : 0;

  for (int sz = 9; sz > 0; --sz) {
    if (remain->count[sz] < 1) {
      continue;
    }

    bool next_lower_tight = lower_tight;
    if (lower_tight) {
      if (sz > lower_digit) continue;               // visited before the start bound: excluded
      if (sz < lower_digit) next_lower_tight = false; // visited after the start bound: unrestricted
    }

    bool next_upper_tight = upper_tight;
    if (upper_tight) {
      if (sz < upper_digit) continue;                // visited after the end bound: excluded
      if (sz > upper_digit) next_upper_tight = false; // visited before the end bound: unrestricted
    }

    if (g_grid.can_place_piece_at_location(sz, free_x, free_y)) {
      g_path[depth] = sz;
      g_grid.insert_piece_at_location(sz, free_x, free_y);
      remain->count[sz]--;
      full_solver(remain, depth + 1, next_lower_tight, next_upper_tight,
                  start_digits, end_digits, free_x, free_y);
      remain->count[sz]++;
      g_grid.remove_piece_at_location(sz, free_x, free_y);
    }
  }
}

void print_usage(const char* prog) {
  printf("Usage: %s [--start=DIGITS] [--end=DIGITS] [--log=FILE] [--status=MODE]\n", prog);
  printf("\n");
  printf("  DIGITS is a string of 1-45 characters, each '1'-'9', giving the tile size\n");
  printf("  placed at each successive raster-scan position (the same format produced\n");
  printf("  by tools/solution.py's get_hash()). A string shorter than 45 characters\n");
  printf("  is a prefix naming an entire subtree of the search, rather than one leaf.\n");
  printf("\n");
  printf("  --start bounds the beginning of the search (inclusive); omit to start\n");
  printf("  from the very first branch. --end bounds the end (inclusive); omit to\n");
  printf("  search to the very last branch. Use both to cover a slice of the search\n");
  printf("  space, e.g. to split work across processes/machines, or to resume a run\n");
  printf("  from a previously printed progress path.\n");
  printf("\n");
  printf("  --log overrides the output file solutions are appended to (default: %s).\n",
         g_log_filename.c_str());
  printf("\n");
  printf("  --status controls how periodic progress lines are printed: 'inplace'\n");
  printf("  overwrites the previous line (tidy for an interactive terminal), 'scroll'\n");
  printf("  prints each on its own line (better when output is redirected to a file\n");
  printf("  or viewed by another process). Default: inplace when stdout is a terminal,\n");
  printf("  scroll otherwise.\n");
}

int main(int argc, char** argv) {
  std::string start_str, end_str, status_str;
  bool have_start = false, have_end = false, have_status = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      return 0;
    } else if (arg.rfind("--start=", 0) == 0) {
      start_str = arg.substr(8);
      have_start = true;
    } else if (arg.rfind("--end=", 0) == 0) {
      end_str = arg.substr(6);
      have_end = true;
    } else if (arg.rfind("--log=", 0) == 0) {
      g_log_filename = arg.substr(6);
    } else if (arg.rfind("--status=", 0) == 0) {
      status_str = arg.substr(9);
      have_status = true;
    } else {
      fprintf(stderr, "Unrecognized argument: %s\n\n", arg.c_str());
      print_usage(argv[0]);
      return 1;
    }
  }

  if (have_status && status_str != "inplace" && status_str != "scroll") {
    fprintf(stderr, "Invalid --status value '%s': expected 'inplace' or 'scroll'\n", status_str.c_str());
    return 1;
  }
  g_inplace_status = have_status ? (status_str == "inplace") : (isatty(fileno(stdout)) != 0);

  std::vector<int> start_digits, end_digits;
  if (have_start && !parse_digit_string(start_str, start_digits)) {
    fprintf(stderr, "Invalid --start value '%s': expected 1-45 digits, each 1-9\n", start_str.c_str());
    return 1;
  }
  if (have_end && !parse_digit_string(end_str, end_digits)) {
    fprintf(stderr, "Invalid --end value '%s': expected 1-45 digits, each 1-9\n", end_str.c_str());
    return 1;
  }
  if (have_start && have_end && start_after_end(start_digits, end_digits)) {
    fprintf(stderr, "Warning: --start (%s) appears to come after --end (%s) in search order; "
                     "the requested range is likely empty.\n", start_str.c_str(), end_str.c_str());
  }

  g_log_file = fopen(g_log_filename.c_str(), "a");
  if (!g_log_file) {
    fprintf(stderr, "Failed to open --log file '%s' for appending\n", g_log_filename.c_str());
    return 1;
  }

  printf("Welcome to CppSolver/Claude rangesolver\n");
  printf("start=%s end=%s log=%s status=%s\n",
         have_start ? start_str.c_str() : "(none)",
         have_end ? end_str.c_str() : "(none)",
         g_log_filename.c_str(),
         g_inplace_status ? "inplace" : "scroll");

  g_grid.init_blank();
  PieceSet init_piece_set;
  init_piece_set.init_full();

  g_start_time = std::chrono::steady_clock::now();
  g_last_print_time = g_start_time;

  full_solver(&init_piece_set, 0, have_start, have_end, start_digits, end_digits, 0, 0);

  fclose(g_log_file);

  if (g_inplace_status && g_printed_status) {
    printf("\n");
  }
  std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - g_start_time;
  printf("Done. %lld solution(s) found in %.1fs.\n", g_solutions_found, elapsed.count());
  return 0;
}
