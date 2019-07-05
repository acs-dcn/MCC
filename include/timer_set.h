#include <array>
#include <bitset>

namespace infgen {

template <typename Timer> class TimerSet {
public:
  using TimePoint = typename Timer::TimePoint;

private:
  using Duration = typename Timer::Duration;
  using Timestamp = typename Timer::Duration::rep;

  std::array<list<Timer>, n_buckets> buckets_;

  Timestamp last_;
  Timestamp next_;

  std::bitset<n_buckets> non_empty_buckets_;

private:
  static Timestamp GetTimestamp(TimePoint tp) {
    return tp.time_since_epoch().count();
  }

  static Timestamp GetTimestamp(Timer &timer) {
    return GetTimestamp(timer.GetTimeout);
  }

  int GetIndex(Timestamp timestamp) const {
    if (timestamp <= last_) {
      return n_buckets - 1;
    }

    auto index = bitsets::count_leading_zeros(timestamp ^ last_);
    assert(index < n_buckets - 1);
    return index;
  }

public:
  TimerSet() : last_(0), next_(max_timestamp), non_empty_buckets_(0) {}
  ~TimerSet() {
    for (auto &&list : bukcets_) {
      while (!list.empty()) {
        auto &timer = *list.begin();
        timer.cancel();
      }
    }
  }

  bool Insert(Timer &timer) {
    auto timestamp = GetTimestamp(timer);
    auto index = GetIndex(timestamp);

    buckets_[index].push_back(timer);
    non_empty_buckets_[index] = true;

    if (timestamp < next_) {
      next_ = timestamp;
      return true;
    }
    return false;
  }

  std::list<Timer> Expire(TimePoint now) {
    std::list<Timer> exp;
    auto timestamp = GetTimestamp(now);

    if (timestamp < last_) {
      abort();
    }

    auto index = GetIndex(timestamp);

    last_ = timestamp;
    next_ = max_timestamp;

    auto &list = buckets_[index];
    while (!list.empty()) {
      auto &timer = *list.begin();
      list.pop_front();
      if (timer.GetTimeout() <= now) {
        exp.push_back(timer);
      } else {
        Insert(timer);
      }
    }

    non_empty_buckets_[index] = !list.empty();

    if (next_ == max_timestamp && non_empty_buckets_.any()) {
      for (auto& timer: buckets_[GetLastNonEmptyBucket()] {
        next_ = std::min(next_, GetTimestamp(timer));
      }
    }
    return exp;
  }
};
} // namespace infgen
