#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";
    // 按顺序插入, 前缀长的放前面
    bool insert_success = false;
    for(auto it=_router_table.begin();it!=_router_table.end();it++){
        if(prefix_length>it->prefix_length){
            _router_table.insert(it,router_record{
                route_prefix,
                prefix_length,
                next_hop,
                interface_num
            });
            insert_success = true;
            break;
        }
    }
    // 没有插入, 放最后面
    if(!insert_success){
        _router_table.push_back(router_record{
            route_prefix,
            prefix_length,
            next_hop,
            interface_num
        });
    }
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    // ttl已经到0了, 或者减一次后到零了
    if(dgram.header().ttl == 0 || (--dgram.header().ttl)==0){
        return;
    }
    for(auto it=_router_table.begin();it!=_router_table.end();it++){
        // 匹配所有的
        if(it->prefix_length==0){
            interface(it->interface_num).send_datagram(dgram,it->next_hop.value());
            break;
        }else{
            //根据前缀长度左移
            uint32_t prefix_mask = uint32_t(~0)<<(32-it->prefix_length);
            if((dgram.header().dst&prefix_mask) == (it->route_prefix&prefix_mask)){
                if(it->next_hop.has_value()){
                    // 发给其他网络接口的,下一跳
                    interface(it->interface_num).send_datagram(dgram,it->next_hop.value());
                }else{
                    // 就在这里,不用继续转发了
                    interface(it->interface_num).send_datagram(dgram,Address::from_ipv4_numeric(dgram.header().dst));
                }
                break;
            }
        }
    }
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
