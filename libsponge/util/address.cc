#include "address.hh"

#include "util.hh"

#include <arpa/inet.h>
#include <cstring>
#include <memory>
#include <netdb.h>
#include <stdexcept>
#include <system_error>

using namespace std;

//! Converts Raw to `sockaddr *`.
//! reinterpret_cast 用于将 storage 的地址转换为 sockaddr* 类型的指针。
//! 使用 reinterpret_cast 需要谨慎，因为它不进行任何值的转换，只是重新解释内存中的位模式
Address::Raw::operator sockaddr *() { return reinterpret_cast<sockaddr *>(&storage); }

//! Converts Raw to `const sockaddr *`.
Address::Raw::operator const sockaddr *() const { return reinterpret_cast<const sockaddr *>(&storage); }

//! \param[in] addr points to a raw socket address
//! \param[in] size is `addr`'s length
Address::Address(const sockaddr *addr, const size_t size) : _size(size) {
    // make sure proposed sockaddr can fit
    if (size > sizeof(_address.storage)) {
        throw runtime_error("invalid sockaddr size");
    }

    memcpy(&_address.storage, addr, size);
}

//! Error category for getaddrinfo and getnameinfo failures.
class gai_error_category : public error_category {
  public:
    //! The name of the wrapped error
    const char *name() const noexcept override { return "gai_error_category"; }
    //! \brief An error message
    //! \param[in] return_value the error return value from [getaddrinfo(3)](\ref man3::getaddrinfo)
    //!                         or [getnameinfo(3)](\ref man3::getnameinfo)
    string message(const int return_value) const noexcept override { return gai_strerror(return_value); }
};

//! \param[in] node is the hostname or dotted-quad address
//! \param[in] service is the service name or numeric string
//! \param[in] hints are criteria for resolving the supplied name
Address::Address(const string &node, const string &service, const addrinfo &hints) : _size() {
    // prepare for the answer
    addrinfo *resolved_address = nullptr;

    // look up the name or names
    // 解析ip地址
    const int gai_ret = getaddrinfo(node.c_str(), service.c_str(), &hints, &resolved_address);
    if (gai_ret != 0) {
        throw tagged_error(gai_error_category(), "getaddrinfo(" + node + ", " + service + ")", gai_ret);
    }

    // if success, should always have at least one entry
    if (resolved_address == nullptr) {
        throw runtime_error("getaddrinfo returned successfully but with no results");
    }

    // put resolved_address in a wrapper so it will get freed if we have to throw an exception
    // 匿名函数
    auto addrinfo_deleter = [](addrinfo *const x) { freeaddrinfo(x); };
    // 独占所有权的智能指针
    unique_ptr<addrinfo, decltype(addrinfo_deleter)> wrapped_address(resolved_address, move(addrinfo_deleter));

    // assign to our private members (making sure size fits)
    // *this 仍然指向原来的 Address 对象，但是通过调用构造函数，对象的状态被重新设置为新的状态
    *this = Address(wrapped_address->ai_addr, wrapped_address->ai_addrlen);
}

//! \brief Build a `struct addrinfo` containing hints for [getaddrinfo(3)](\ref man3::getaddrinfo)
//! \param[in] ai_flags is the value of the `ai_flags` field in the [struct addrinfo](\ref man3::getaddrinfo)
//! \param[in] ai_family is the value of the `ai_family` field in the [struct addrinfo](\ref man3::getaddrinfo)
static inline addrinfo make_hints(const int ai_flags, const int ai_family) {
    addrinfo hints{};  // value initialized to all zeros
    hints.ai_flags = ai_flags;
    hints.ai_family = ai_family;
    return hints;
}

//! \param[in] hostname to resolve
//! \param[in] service name (from `/etc/services`, e.g., "http" is port 80)
//! 返回所有地址，AF_INET表示Internet地址族，通常用于IPv4地址
Address::Address(const string &hostname, const string &service)
    : Address(hostname, service, make_hints(AI_ALL, AF_INET)) {}

//! \param[in] ip address as a dotted quad ("1.1.1.1")
//! \param[in] port number
Address::Address(const string &ip, const uint16_t port)
    // tell getaddrinfo that we don't want to resolve anything
    : Address(ip, ::to_string(port), make_hints(AI_NUMERICHOST | AI_NUMERICSERV, AF_INET)) {}

// accessors
pair<string, uint16_t> Address::ip_port() const {
    array<char, NI_MAXHOST> ip{};
    array<char, NI_MAXSERV> port{};

    const int gni_ret =
        getnameinfo(_address, _size, ip.data(), ip.size(), port.data(), port.size(), NI_NUMERICHOST | NI_NUMERICSERV);
    if (gni_ret != 0) {
        throw tagged_error(gai_error_category(), "getnameinfo", gni_ret);
    }

    return {ip.data(), stoi(port.data())};
}

string Address::to_string() const {
    const auto ip_and_port = ip_port();
    return ip_and_port.first + ":" + ::to_string(ip_and_port.second);
}

uint32_t Address::ipv4_numeric() const {
    if (_address.storage.ss_family != AF_INET or _size != sizeof(sockaddr_in)) {
        throw runtime_error("ipv4_numeric called on non-IPV4 address");
    }

    sockaddr_in ipv4_addr{};
    memcpy(&ipv4_addr, &_address.storage, _size);

    return be32toh(ipv4_addr.sin_addr.s_addr);
}

Address Address::from_ipv4_numeric(const uint32_t ip_address) {
    sockaddr_in ipv4_addr{};
    ipv4_addr.sin_family = AF_INET;
    ipv4_addr.sin_addr.s_addr = htobe32(ip_address);

    return {reinterpret_cast<sockaddr *>(&ipv4_addr), sizeof(ipv4_addr)};
}

// equality
bool Address::operator==(const Address &other) const {
    if (_size != other._size) {
        return false;
    }

    return 0 == memcmp(&_address, &other._address, _size);
}
