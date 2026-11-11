#pragma once

#include <cinttypes>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "nigiri/types.h"

namespace nigiri {
struct timetable;
}

namespace nigiri::routing {

// PROTOTYPE ("allowed segments" filter).
//
// Static part, built once (server start) from a CSV side file
//   from_station,to_station,tag|tag|...[;tag|tag|...]
// The third column holds one or more tag groups separated by ';' (e.g. one
// group per fare network). Every route section (two consecutive stops of a
// route stop sequence, mapped to station level) gets the index of its tag set
// (= its list of groups). Tag set 0 = "no tags".
struct segment_tags {
  using tagset_idx_t = std::uint32_t;
  using tag_idx_t = std::uint32_t;
  using station_key_t = std::uint32_t;  // 0 = unknown station

  static constexpr auto kNoTags = tagset_idx_t{0U};

  // station_key: maps a timetable location to the station id used in the file
  // (0 if the location has none).
  segment_tags(timetable const&,
               std::filesystem::path const&,
               std::function<station_key_t(location_idx_t)> const& station_key);

  tagset_idx_t get(route_idx_t const r, stop_idx_t const section) const {
    return section_tagset_[route_section_offset_[to_idx(r)] + section];
  }

  // Slow path (hash lookup): realtime transports + journey reconstruction.
  tagset_idx_t get(location_idx_t from, location_idx_t to) const;

  std::vector<std::string> tag_names_;
  hash_map<std::string, tag_idx_t> tag_idx_;
  // [tag set][group] -> tags; [0] = {}
  std::vector<std::vector<std::vector<tag_idx_t>>> tagsets_;
  hash_map<std::uint64_t, tagset_idx_t> pair_tagset_;
  std::vector<station_key_t> location_station_;
  std::vector<std::uint32_t> route_section_offset_;
  std::vector<tagset_idx_t> section_tagset_;
  std::uint64_t n_tagged_sections_{0U};
};

// Per request part: which tag sets have at least one group that is fully
// contained in the allowed tags.
struct segment_filter {
  segment_filter(segment_tags const&,
                 std::vector<std::string_view> const& allowed_tags);

  bool allowed(route_idx_t const r, stop_idx_t const section) const {
    return allowed_tagset_[tags_.get(r, section)] != 0U;
  }

  bool allowed(location_idx_t const from, location_idx_t const to) const {
    return allowed_tagset_[tags_.get(from, to)] != 0U;
  }

  segment_tags const& tags_;
  std::vector<std::uint8_t> allowed_tagset_;  // [0] = 0: untagged = forbidden
};

}  // namespace nigiri::routing
