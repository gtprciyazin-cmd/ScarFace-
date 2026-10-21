#pragma once
#include <vector>
#include <memory>
#include <string>

namespace network {
enum class DnsStatus {
    NoError,
    FormatError,
    ServerFail,
    NameError,
    NotImplemented,
    Refused,
    YXDomain,
    YXRRSet,
    NXRRSet,
    NotAuth,
    NotZone
};

struct DnsResult {
    DnsStatus status;
    std::string ip;
};

class DnsProvider {
public:
    virtual ~DnsProvider() = default;
    [[nodiscard]] virtual std::string get_host() const = 0;
    [[nodiscard]] virtual std::string get_path() const = 0;
};

class CloudflareDnsProvider final : public DnsProvider {
public:
    [[nodiscard]] std::string get_host() const override { return "cloudflare-dns.com"; }
    [[nodiscard]] std::string get_path() const override { return "/dns-query"; }
};

class GoogleDnsProvider final : public DnsProvider {
public:
    [[nodiscard]] std::string get_host() const override { return "dns.google"; }
    [[nodiscard]] std::string get_path() const override { return "/resolve"; }
};

// The same services reached by IP, for networks where their names are
// blocked or do not resolve.
class CloudflareIpDnsProvider final : public DnsProvider {
public:
    [[nodiscard]] std::string get_host() const override { return "1.1.1.1"; }
    [[nodiscard]] std::string get_path() const override { return "/dns-query"; }
};

class GoogleIpDnsProvider final : public DnsProvider {
public:
    [[nodiscard]] std::string get_host() const override { return "8.8.8.8"; }
    [[nodiscard]] std::string get_path() const override { return "/resolve"; }
};

[[nodiscard]] std::unique_ptr<DnsProvider> create_dns_provider(const std::string& name);

// Resolves names over DNS-over-HTTPS, so the hosts file that points
// Growtopia at the proxy is bypassed. Tries the chosen service first, then
// the others, since some networks block one of them.
class DnsResolver {
public:
    explicit DnsResolver(std::unique_ptr<DnsProvider> provider);

    [[nodiscard]] DnsResult resolve_domain(const std::string& domain) const;
    [[nodiscard]] std::string resolve_ip(const std::string& host) const;

private:
    [[nodiscard]] static DnsResult resolve_with(const DnsProvider& provider, const std::string& domain);

    std::vector<std::unique_ptr<DnsProvider>> providers_;
};
}
