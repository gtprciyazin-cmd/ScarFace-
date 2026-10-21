#pragma once

namespace core {
// Lets the proxy through Windows Defender Firewall. Clicking "Cancel" on the
// firewall prompt the first time the exe runs makes Windows block it, and the
// game then never gets past "Connecting to Growtopia server". This replaces
// every rule for this exe (block rules win over allow rules) with one that
// allows it. Needs administrator rights, which the proxy already asks for, and
// must run before the proxy opens its sockets. Does nothing off Windows.
void allow_through_firewall();
}
