// This file is a part of the IncludeOS unikernel - www.includeos.org
//
// Copyright 2015-2016 Oslo and Akershus University College of Applied Sciences
// and Alfred Bratterud
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include <os>
#include <platforms/unik.hpp>
#include <rapidjson/document.h>
#include <net/inet4>
#include <regex>
#include <info>

using namespace rapidjson;

unik::Client::Registered_event unik::Client::on_registered_{nullptr};

/**
 * UniK instance listener hearbeat / http registration
 **/
void unik::Client::register_instance(net::Inet4 &inet, const net::UDP::port_t port) {

//    let OS::start know it shouldn't start service yet
//    OS::ready_ = false;
    INFO("Unik client", "Turned off OS::ready_: %d", OS::ready_);

    INFO("Unik client", "Initializing Unik registration service");
    INFO("Unik client", "Listening for UDP hearbeat on %s:%i", inet.ip_addr().str().c_str(), port);
    INFO("Unik client", "IP is attached to interface %s ", inet.link_addr().str().c_str());

    // Set up an UDP port for receiving UniK heartbeat
    auto &sock = inet.udp().bind(port);
    CHECK(net::Inet4::stack<0>().udp().is_bound(port), "Unik UDP port is bound as expected");
    sock.on_read([&sock, &inet](auto addr, auto port, const char *data, size_t len) {

        static bool registered_with_unik = false;
        static const int max_attempts = 5;
        static int attempts_left = max_attempts;

        if (registered_with_unik or not attempts_left)
            return;

        std::string strdata(data, len);
        INFO("Unik client", "received UDP data from %s:%i: %s ", addr.str().c_str(), port, strdata.c_str());

        auto dotloc = strdata.find(":");

        if (dotloc == std::string::npos) {
            INFO("Unik client", "Unexpected UDP data format - no ':' in string.");
            return;
        }

        std::string prefix = strdata.substr(0, dotloc);
        std::string ip_str = strdata.substr(dotloc + 1);

        INFO("Unik client", "Prefix: %s , IP: '%s' \n", prefix.c_str(), ip_str.c_str());

        net::IP4::addr ip{ip_str};
        net::tcp::Socket unik_instance_listener{ip, 3000};

        attempts_left--;
        INFO("Unik client", "Connecting to UniK instance listener %s:%i (attempt %i / %i) ",
             ip.str().c_str(), 3000, max_attempts - attempts_left, max_attempts);

        // Connect to the instance listener
        auto http = inet.tcp().connect(unik_instance_listener);

        http->on_connect([&http, &inet](auto unik) {

            // Get our mac address
            auto mac_str = inet.link_addr().str();

            // Construct a HTTP request to the Unik instance listener, providing our mac-address in the query string
            std::string http_request = "POST /register?mac_address=" + std::string(mac_str) + " HTTP/1.1\r\n\n";
            INFO("Unik client", "Connected to UniK instance listener. Sending HTTP request: %s ", http_request.c_str());

            unik->write(http_request.c_str(), http_request.size());

            // Expect a response with meta data (which we ignore)
            unik->on_read(1024, [&http](auto buf, size_t n) {
                std::string response((char *) buf.get(), n);
                INFO("Unik client", "Unik reply: %s \n", response.c_str());

                if (response.find("200 OK") != std::string::npos) {
                    registered_with_unik = true;

                    size_t json_start = response.find_first_of("{");
                    std::string json_data = response.substr(json_start);
                    INFO("Unik Client", "json data: %s \n", json_data.c_str());

                    Document document;
                    document.Parse(json_data.c_str());
                    for (Value::ConstMemberIterator itr = document.MemberBegin(); itr != document.MemberEnd(); ++itr) {
                        printf("setting env %s=%s\n", itr->name.GetString(), itr->value.GetString());
                        setenv(itr->name.GetString(), itr->value.GetString(), 1);
                    }

                    // Call the optional user callback if any
                    if (on_registered_)
                        on_registered_();

                    //unblock OS::start()
                    OS::ready_ = true;

                    return;
                }

                http->close();

            });
        });
    });
} // register_instance


void unik::Client::register_instance_dhcp() {
    // Bring up a network device using DHCP
    static auto &&inet = net::Inet4::stack<0>();

    net::Inet4::ifconfig<0>(10.0, [](bool timeout) {
        if (timeout) {
            INFO("Unik client", "DHCP request timed out. Nothing to do.");
            return;
        }
        INFO("Unik client", "IP address updated: %s", inet.ip_addr().str().c_str());
        register_instance(inet);
    });
}

__attribute__((constructor))
void register_platform_unik() {
    OS::register_custom_init(unik::Client::register_instance_dhcp, "Unik register instance");
}
