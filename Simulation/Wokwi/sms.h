// SAATHI chair firmware - SMS through the SIM800L (no internet needed; messages are queued and retried until the network confirms them).
#pragma once
#include <Arduino.h>

void smsBegin();
bool smsSend(const char* number, const char* text);   // queues in flash, tries now; true if already sent
void smsService();                                     // call from loop(): retries queued messages every 5 min
int smsPending();                                      // messages not yet confirmed by the network
bool networkTime(uint32_t* epochOut);                  // time from the mobile network, if available
