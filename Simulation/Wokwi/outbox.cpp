// SAATHI chair firmware - SMS outbox logic. See outbox.h.
#include "outbox.h"
#include <cstdio>
#include <cstdlib>

bool Outbox::priority(const std::string& t) { return t.find("ALERT") != std::string::npos || t.find("SOS") != std::string::npos; }

std::vector<std::string> Outbox::parts(const std::string& t) {
  std::vector<std::string> p;
  for (size_t i = 0; i < t.size(); i += PART) p.push_back(t.substr(i, PART));
  if (p.empty()) p.push_back("");
  return p;
}

bool Outbox::enqueue(const std::string& number, const std::string& text) {
  if ((int)q.size() >= SOFT_MAX) {
    int drop = -1;
    for (int i = 0; i < (int)q.size(); i++) if (!priority(q[i].text) && q[i].partsSent == 0) { drop = i; break; }
    if (drop >= 0) {
      log = "queue full: dropped the oldest daily summary to make room";
      q.erase(q.begin() + drop);
    } else if (!priority(text) || (int)q.size() >= HARD_MAX) {
      refused++;
      log = priority(text) ? "queue at 24 alerts: new alert refused and counted" : "queue full of alerts: daily summary refused";
      return false;
    } else {
      log = "queue holds only alerts: growing beyond 12 so none is lost";
    }
  }
  q.push_back({number, text, 0});
  return true;
}

int Outbox::flush(const std::function<bool(const std::string&, const std::string&)>& send,
                  const std::function<void()>& onProgress) {
  int sent = 0;
  while (!q.empty()) {
    OutMsg& m = q.front();
    std::vector<std::string> p = parts(m.text);
    while (m.partsSent < (int)p.size()) {
      if (!send(m.number, p[m.partsSent])) return sent;      // keep the message and its progress; retry later
      m.partsSent++; sent++;
      if (onProgress) onProgress();
    }
    std::string number = m.number;
    q.erase(q.begin());
    if (onProgress) onProgress();
    if (q.empty() && refused > 0) {                           // tell the family that messages were lost
      char note[120];
      snprintf(note, sizeof(note), "SAATHI: %d message(s) could not be stored while the network was down. Please check the chair.", refused);
      refused = 0;
      q.push_back({number, note, 0});
    }
  }
  return sent;
}

std::string Outbox::serialize() const {
  std::string s = "R\t" + std::to_string(refused) + "\n";
  for (auto& m : q) s += m.number + "\t" + std::to_string(m.partsSent) + "\t" + m.text + "\n";
  return s;
}

void Outbox::deserialize(const std::string& s) {
  q.clear(); refused = 0;
  size_t pos = 0;
  while (pos < s.size()) {
    size_t e = s.find('\n', pos); if (e == std::string::npos) e = s.size();
    std::string line = s.substr(pos, e - pos); pos = e + 1;
    size_t a = line.find('\t'); if (a == std::string::npos) continue;
    if (line.substr(0, a) == "R") { refused = atoi(line.c_str() + a + 1); continue; }
    size_t b = line.find('\t', a + 1); if (b == std::string::npos) continue;
    q.push_back({line.substr(0, a), line.substr(b + 1), atoi(line.substr(a + 1, b - a - 1).c_str())});
  }
}
