#pragma once
// Phase timing instrumentation for the assets export pipeline (--timing).
// Separated from assets.cpp so that measurement is not interleaved with the
// export logic it measures.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <vector>

namespace maiconv {

struct PhaseTiming {
  std::vector<double> samples_ms;
  double total_ms = 0.0;

  void add(std::chrono::steady_clock::duration duration) {
    const auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(duration);
    const double ms = static_cast<double>(us.count()) / 1000.0;
    total_ms += ms;
    samples_ms.push_back(ms);
  }

  void merge(const PhaseTiming &other) {
    total_ms += other.total_ms;
    samples_ms.insert(samples_ms.end(), other.samples_ms.begin(),
                      other.samples_ms.end());
  }

  [[nodiscard]] double avg_ms() const {
    if (samples_ms.empty()) {
      return 0.0;
    }
    return total_ms / static_cast<double>(samples_ms.size());
  }

  [[nodiscard]] double p95_ms() const {
    if (samples_ms.empty()) {
      return 0.0;
    }
    std::vector<double> sorted = samples_ms;
    std::sort(sorted.begin(), sorted.end());
    const std::size_t idx = (sorted.size() - 1) * 95 / 100;
    return sorted[idx];
  }
};

struct AssetsTimingSummary {
  PhaseTiming source_scan;
  PhaseTiming index_build;
  PhaseTiming xml_parse;
  PhaseTiming ma2_parse_compose;
  PhaseTiming media;
  PhaseTiming write_zip;
  std::size_t metadata_cache_hits = 0;
  std::size_t metadata_cache_misses = 0;
  std::size_t asset_index_cache_hits = 0;
  std::size_t asset_index_cache_misses = 0;

  void merge(const AssetsTimingSummary &other) {
    source_scan.merge(other.source_scan);
    index_build.merge(other.index_build);
    xml_parse.merge(other.xml_parse);
    ma2_parse_compose.merge(other.ma2_parse_compose);
    media.merge(other.media);
    write_zip.merge(other.write_zip);
    metadata_cache_hits += other.metadata_cache_hits;
    metadata_cache_misses += other.metadata_cache_misses;
    asset_index_cache_hits += other.asset_index_cache_hits;
    asset_index_cache_misses += other.asset_index_cache_misses;
  }
};

// Accumulates elapsed time into a duration when it leaves scope.
//
// It targets a duration rather than a PhaseTiming on purpose: a loop that
// measures several charts contributes ONE sample per track, and timing each
// iteration directly into the PhaseTiming would change what avg/p95 mean.
//
// The manual begin/now() pairs this replaces had to be repeated on every exit
// path, so the chart loop stopped its clock at four separate points, one of
// them immediately before a `continue`. Adding a fifth exit silently
// under-reported the phase.
class ScopedDuration {
public:
  explicit ScopedDuration(std::chrono::steady_clock::duration &sink)
      : sink_(&sink), begin_(std::chrono::steady_clock::now()) {}

  ScopedDuration(const ScopedDuration &) = delete;
  ScopedDuration &operator=(const ScopedDuration &) = delete;
  ScopedDuration(ScopedDuration &&) = delete;
  ScopedDuration &operator=(ScopedDuration &&) = delete;

  ~ScopedDuration() { stop(); }

  // Stops the clock early, so that work done after this point is excluded.
  // Calling it more than once has no further effect.
  void stop() {
    if (sink_ != nullptr) {
      *sink_ += std::chrono::steady_clock::now() - begin_;
      sink_ = nullptr;
    }
  }

private:
  std::chrono::steady_clock::duration *sink_;
  std::chrono::steady_clock::time_point begin_;
};

} // namespace maiconv
