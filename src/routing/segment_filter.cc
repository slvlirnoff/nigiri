#include "nigiri/routing/segment_filter.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <map>

#include "utl/verify.h"

#include "nigiri/logging.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

namespace {

std::uint64_t pair_key(segment_tags::station_key_t const a,
                       segment_tags::station_key_t const b) {
  return (std::uint64_t{a} << 32U) | std::uint64_t{b};
}

std::vector<std::string_view> split(std::string_view s, char const sep) {
  auto parts = std::vector<std::string_view>{};
  while (true) {
    auto const pos = s.find(sep);
    parts.push_back(s.substr(0, pos));
    if (pos == std::string_view::npos) {
      break;
    }
    s.remove_prefix(pos + 1U);
  }
  return parts;
}

segment_tags::station_key_t parse_key(std::string_view const s) {
  auto v = segment_tags::station_key_t{0U};
  std::from_chars(s.data(), s.data() + s.size(), v);
  return v;
}

}  // namespace

segment_tags::segment_tags(
    timetable const& tt,
    std::filesystem::path const& path,
    std::function<station_key_t(location_idx_t)> const& station_key) {
  auto in = std::ifstream{path};
  utl::verify(in.good(), "segment tags: cannot read {}", path.string());

  tagsets_.emplace_back();  // kNoTags
  using groups_t = std::vector<std::vector<tag_idx_t>>;
  auto tagset_idx = std::map<groups_t, tagset_idx_t>{};
  tagset_idx.emplace(groups_t{}, kNoTags);

  auto line = std::string{};
  auto n_rows = 0U;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    auto const cols = split(line, ',');
    if (cols.size() != 3U || cols[0].empty() || cols[0][0] < '0' ||
        cols[0][0] > '9') {
      continue;  // header, empty or malformed line
    }
    auto set = groups_t{};
    for (auto const group : split(cols[2], ';')) {
      auto g = std::vector<tag_idx_t>{};
      for (auto const tag : split(group, '|')) {
        if (tag.empty()) {
          continue;
        }
        auto const [it, is_new] = tag_idx_.emplace(
            std::string{tag}, static_cast<tag_idx_t>(tag_names_.size()));
        if (is_new) {
          tag_names_.emplace_back(tag);
        }
        g.push_back(it->second);
      }
      if (g.empty()) {
        continue;  // an empty group would allow the section for everyone
      }
      std::sort(begin(g), end(g));
      g.erase(std::unique(begin(g), end(g)), end(g));
      set.push_back(std::move(g));
    }
    std::sort(begin(set), end(set));
    set.erase(std::unique(begin(set), end(set)), end(set));
    auto const [it, is_new] =
        tagset_idx.emplace(set, static_cast<tagset_idx_t>(tagsets_.size()));
    if (is_new) {
      tagsets_.emplace_back(std::move(set));
    }
    pair_tagset_[pair_key(parse_key(cols[0]),
                          parse_key(cols[1]))] = it->second;
    ++n_rows;
  }

  location_station_.resize(tt.n_locations());
  for (auto l = location_idx_t{0U}; l != tt.n_locations(); ++l) {
    location_station_[to_idx(l)] = station_key(l);
  }

  route_section_offset_.reserve(tt.n_routes() + 1U);
  for (auto r = route_idx_t{0U}; r != tt.n_routes(); ++r) {
    route_section_offset_.push_back(
        static_cast<std::uint32_t>(section_tagset_.size()));
    auto const seq = tt.route_location_seq_[r];
    for (auto i = 1U; i < seq.size(); ++i) {
      auto const ts = get(stop{seq[i - 1U]}.location_idx(),
                          stop{seq[i]}.location_idx());
      n_tagged_sections_ += ts != kNoTags ? 1U : 0U;
      section_tagset_.push_back(ts);
    }
  }
  route_section_offset_.push_back(
      static_cast<std::uint32_t>(section_tagset_.size()));

  log(log_lvl::info, "nigiri.segment_tags",
      "{}: {} rows, {} tags, {} tag sets, {}/{} route sections tagged",
      path.string(), n_rows, tag_names_.size(), tagsets_.size(),
      n_tagged_sections_, section_tagset_.size());
}

segment_tags::tagset_idx_t segment_tags::get(location_idx_t const from,
                                             location_idx_t const to) const {
  auto const a = location_station_[to_idx(from)];
  auto const b = location_station_[to_idx(to)];
  if (a == 0U || b == 0U) {
    return kNoTags;
  }
  auto const it = pair_tagset_.find(pair_key(a, b));
  return it == end(pair_tagset_) ? kNoTags : it->second;
}

segment_filter::segment_filter(
    segment_tags const& tags, std::vector<std::string_view> const& allowed_tags)
    : tags_{tags}, allowed_tagset_(tags.tagsets_.size(), 0U) {
  auto allowed = std::vector<std::uint8_t>(tags.tag_names_.size(), 0U);
  for (auto const t : allowed_tags) {
    if (auto const it = tags.tag_idx_.find(std::string{t});
        it != end(tags.tag_idx_)) {
      allowed[it->second] = 1U;
    }
  }
  for (auto i = 1U; i != tags.tagsets_.size(); ++i) {
    allowed_tagset_[i] =
        std::any_of(begin(tags.tagsets_[i]), end(tags.tagsets_[i]),
                    [&](std::vector<segment_tags::tag_idx_t> const& group) {
                      return std::all_of(
                          begin(group), end(group),
                          [&](segment_tags::tag_idx_t const t) {
                            return allowed[t] != 0U;
                          });
                    })
            ? 1U
            : 0U;
  }
}

}  // namespace nigiri::routing
