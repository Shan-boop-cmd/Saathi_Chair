// SAATHI chair firmware - SMS outbox logic (pure C++, no hardware; also compiled on a PC by host_test/test_outbox.cpp).
// Rules:
//  * ALERT and SOS messages are never evicted. When the queue holds 12 messages, the oldest daily summary is
//    dropped to make room; if all 12 are alerts, the queue grows to 24. Only beyond 24 is a new message refused,
//    and that refusal is counted and reported to the family by SMS as soon as the network returns.
//  * Long messages go out in 150-character parts. The number of parts already confirmed by the network is stored
//    with the message, so a retry after a failure (or a power cut) resumes at the next part: no part is sent twice.
//  * A message leaves the queue only after every part is confirmed.
#pragma once
#include <string>
#include <vector>
#include <functional>

struct OutMsg { std::string number, text; int partsSent = 0; };

class Outbox {
 public:
  static const int SOFT_MAX = 12, HARD_MAX = 24, PART = 150;
  std::vector<OutMsg> q;
  int refused = 0;               // messages that could not be queued (reported when the network returns)
  std::string log;               // last event, printed on the serial console

  static bool priority(const std::string& text);
  static std::vector<std::string> parts(const std::string& text);
  bool enqueue(const std::string& number, const std::string& text);    // false only when refused (logged)
  // send(number, part) -> true when the network confirms. Returns the number of parts confirmed in this call.
  // onProgress() is called after every confirmed part, so the caller can save the queue at once.
  int flush(const std::function<bool(const std::string&, const std::string&)>& send,
            const std::function<void()>& onProgress = nullptr);
  std::string serialize() const;
  void deserialize(const std::string& s);
};
