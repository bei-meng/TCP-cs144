#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

void NetworkInterface::send_arp(const uint32_t target_ip){
    // 准备ARP信息
    ARPMessage arp;
    arp.opcode = ARPMessage::OPCODE_REQUEST;
    arp.sender_ethernet_address = _ethernet_address;
    arp.sender_ip_address = _ip_address.ipv4_numeric();
    // arp.target_ethernet_address = ETHERNET_BROADCAST;
    arp.target_ip_address = target_ip;

    // 发送ARP帧
    EthernetFrame frame_arp;
    frame_arp.header().type = EthernetHeader::TYPE_ARP;
    frame_arp.header().src = _ethernet_address;
    frame_arp.header().dst = ETHERNET_BROADCAST;
    frame_arp.payload().append(arp.serialize());
    _frames_out.push(frame_arp);

    // 记录此ip的发送时间
    arp_send[target_ip] = time_now;
}

void NetworkInterface::reply_arp(const uint32_t target_ip,const EthernetAddress &target_ethernet_address){
    // 准备ARP信息
    ARPMessage arp;
    arp.opcode = ARPMessage::OPCODE_REPLY;
    arp.sender_ethernet_address = _ethernet_address;
    arp.sender_ip_address = _ip_address.ipv4_numeric();
    arp.target_ethernet_address = target_ethernet_address;
    arp.target_ip_address = target_ip;

    // 发送ARP帧
    EthernetFrame frame_arp;
    frame_arp.header().type = EthernetHeader::TYPE_ARP;
    frame_arp.header().src = _ethernet_address;
    frame_arp.header().dst = target_ethernet_address;
    frame_arp.payload().append(arp.serialize());
    _frames_out.push(frame_arp);
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    EthernetFrame frame;
    frame.header().type = EthernetHeader::TYPE_IPv4;
    frame.header().src = _ethernet_address;
    frame.payload().append(dgram.serialize());

    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    auto cache_it = cache.find(next_hop_ip);
    // 如果有缓存, 就直接生成以太网帧发送
    if(cache_it!=cache.end()){
        frame.header().dst = cache_it->second.addr;
        _frames_out.push(frame);
    }else{
        auto arp_it = arp_send.find(next_hop_ip);
        // 如果为空或者上次发送arp的时间已经超过5秒了
        if(arp_it == arp_send.end() || arp_it->second + ARP_RESEND_TIME < time_now){
            send_arp(next_hop_ip);
        }
        
        // 将要发送的帧存下来
        auto unsend_it = _frames_out_unsend.find(next_hop_ip);
        if(unsend_it!=_frames_out_unsend.end()){
            unsend_it->second.push_back(frame);
        }else{
            _frames_out_unsend[next_hop_ip] = std::vector<EthernetFrame>{frame};
        }
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    EthernetHeader header = frame.header();
    BufferList payload = frame.payload();
    // 是IPV4, 并且是发给自己的
    if(header.type == EthernetHeader::TYPE_IPv4 && header.dst == _ethernet_address){
        InternetDatagram dgram;
        if(dgram.parse(payload)==ParseResult::NoError){
            return dgram;
        }
    // 是arp协议
    }else if(header.type == EthernetHeader::TYPE_ARP){
        ARPMessage arp;
        if(arp.parse(payload)==ParseResult::NoError){
            // 记下来映射关系
            cache[arp.sender_ip_address]=ip_map_ethernet{arp.sender_ethernet_address,time_now};
            // 是找自己的ip的mac地址的
            if(arp.target_ip_address == _ip_address.ipv4_numeric() && arp.opcode==ARPMessage::OPCODE_REQUEST){
                reply_arp(arp.sender_ip_address,arp.sender_ethernet_address);
            }
            // 把需要发的以太网帧发送出去
            auto unsend_it = _frames_out_unsend.find(arp.sender_ip_address);
            if(unsend_it!=_frames_out_unsend.end()){
                for(auto it=unsend_it->second.begin();it!=unsend_it->second.end();it++){
                    it->header().dst = arp.sender_ethernet_address;
                    _frames_out.push(*it);
                }
                _frames_out_unsend.erase(unsend_it);
            }
            // 清除查询记录
            auto arp_it = arp_send.find(arp.sender_ip_address);
            // 如果为空或者上次发送arp的时间已经超过5秒了
            if(arp_it != arp_send.end()){
                arp_send.erase(arp_it);
            }
        }
    }
    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    time_now += ms_since_last_tick;
    // 清除cache
    auto it=cache.begin();
    while(it!=cache.end()){
        if(it->second.time + ARP_REMAIN_TIME < time_now){
            it=cache.erase(it);
        }else{
            it++;
        }
    }
    // 重复发送arp
    auto arp_it=arp_send.begin();
    while(arp_it!=arp_send.end()){
        if(arp_it->second + ARP_RESEND_TIME < time_now){
            send_arp(arp_it->first);
        }
        arp_it++;
    }
}
