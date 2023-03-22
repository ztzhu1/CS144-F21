#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <cassert>
#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to
// IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address,
                                   const Address &ip_address)
    : ethernet_address_(ethernet_address), ip_address_(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(ethernet_address_)
         << " and IP address " << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    EthernetFrame frame;
    auto &header = frame.header();
    header.src = ethernet_address_;
    /* look up table to find the ethernet address of next hop */
    auto it = ip_eth_map_.find(next_hop_ip);

    if (it != ip_eth_map_.end()) {
        /* found, send it directly */
        header.dst = it->second.addr;
        header.type = EthernetHeader::TYPE_IPv4;
        frame.payload() = dgram.serialize();
        frames_out_.push(frame);
    } else {
        /* not found, ARP */
        /* 1. enqueue the ip datagram */
        auto eth_it = waiting_dgrams_.find(next_hop_ip);
        if (eth_it != waiting_dgrams_.end()) {
            eth_it->second.push(dgram);
        } else {
            std::queue<InternetDatagram> waiting_queue;
            waiting_queue.push(dgram);
            waiting_dgrams_.emplace(next_hop_ip, waiting_queue);
        }
        /* 2. broadcast ARP */
        auto time_it = ip_waiting_time_map_.find(next_hop_ip);
        if (time_it != ip_waiting_time_map_.end()) {
            if (time_it->second <= 5000) {  // more than 5 sec since last broadcast
                return;
            } else {
                time_it->second = 0;
            }
        } else {
            ip_waiting_time_map_.emplace(next_hop_ip, 0);
        }
        /* set header */
        header.dst = ETHERNET_BROADCAST;
        header.type = EthernetHeader::TYPE_ARP;
        /* set ARP message */
        ARPMessage arp_msg;
        arp_msg.opcode = ARPMessage::OPCODE_REQUEST;
        arp_msg.sender_ethernet_address = ethernet_address_;
        arp_msg.sender_ip_address = ip_address_.ipv4_numeric();
        arp_msg.target_ip_address = next_hop_ip;
        frame.payload() = arp_msg.serialize();
        /* send */
        frames_out_.push(frame);
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    const auto &header = frame.header();
    if (header.dst != ethernet_address_ && header.dst != ETHERNET_BROADCAST) {
        /* nothing to do with me */
        return nullopt;
    }
    optional<InternetDatagram> ret = nullopt;
    if (header.type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram dgram;
        auto parse_result = dgram.parse(Buffer(frame.payload()));
        if (parse_result == ParseResult::NoError) {
            ret = dgram;
        }
    } else if (header.type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp_msg;
        auto parse_result = arp_msg.parse(Buffer(frame.payload()));
        if (parse_result != ParseResult::NoError) {
            return nullopt;
        }
        auto const sender_eth_addr = arp_msg.sender_ethernet_address;
        auto const sender_ip_addr = arp_msg.sender_ip_address;
        // Warning: Here, I didn't consider more corner cases.
        ip_eth_map_[sender_ip_addr] = RememberedEthAddr{sender_eth_addr, 0};
        auto it = ip_waiting_time_map_.find(sender_ip_addr);
        if (it != ip_waiting_time_map_.end()) {
            ip_waiting_time_map_.erase(it);
            auto dgram_it = waiting_dgrams_.find(sender_ip_addr);
            assert(dgram_it != waiting_dgrams_.end());
            while (!dgram_it->second.empty()) {
                auto &dgram = dgram_it->second.front();

                EthernetFrame waiting_frame;
                auto &waiting_header = waiting_frame.header();
                waiting_header.src = ethernet_address_;
                waiting_header.dst = sender_eth_addr;
                waiting_header.type = EthernetHeader::TYPE_IPv4;
                waiting_frame.payload() = dgram.serialize();
                frames_out_.push(waiting_frame);

                dgram_it->second.pop();
            }
            waiting_dgrams_.erase(dgram_it);
        }

        if (arp_msg.opcode == ARPMessage::OPCODE_REQUEST &&
            arp_msg.target_ip_address == ip_address_.ipv4_numeric()) {
            EthernetFrame reply_frame;
            auto &reply_header = reply_frame.header();
            reply_header.src = ethernet_address_;
            reply_header.dst = sender_eth_addr;
            reply_header.type = EthernetHeader::TYPE_ARP;
            ARPMessage reply_arp_msg;
            reply_arp_msg.opcode = ARPMessage::OPCODE_REPLY;
            reply_arp_msg.sender_ethernet_address = ethernet_address_;
            reply_arp_msg.sender_ip_address = ip_address_.ipv4_numeric();
            reply_arp_msg.target_ip_address = sender_ip_addr;
            reply_arp_msg.target_ethernet_address = sender_eth_addr;
            reply_frame.payload() = reply_arp_msg.serialize();
            frames_out_.push(reply_frame);
        }
    } else {
        assert(false);
    }
    return ret;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    for (auto &[ip, waiting_time] : ip_waiting_time_map_) {
        waiting_time += ms_since_last_tick;
    }

    auto it = ip_eth_map_.begin();
    while (it != ip_eth_map_.end()) {
        it->second.time += ms_since_last_tick;
        if (it->second.time > 30000) {
            // out-of-date
            it = ip_eth_map_.erase(it);
        } else {
            ++it;
        }
    }
}
